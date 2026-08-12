#!/usr/bin/env python3
# Project publication and maintenance: 路灯同学创业笔记
# X: https://x.com/LDstartupnotes
# Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
# https://github.com/streetlightstartupnotes
"""Lightweight macOS settings UI for the Codex Micro companion."""

from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

import tkinter as tk
from tkinter import messagebox, ttk

from codex_usage_bridge import (
    AgentMetadata,
    BridgeController,
    BridgeSettings,
    STATUS_LABELS,
)


APP_NAME = "Codex Micro Companion"
SETTINGS_PATH = (
    Path.home()
    / "Library"
    / "Application Support"
    / "Codex Micro Companion"
    / "settings.json"
)
INTERVALS = (30, 60, 120, 300, 600, 900, 1800, 3600)


def load_settings() -> BridgeSettings:
    try:
        value = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise ValueError("settings root is not an object")
        return BridgeSettings(
            interval=int(value.get("interval", 300)),
            sound_enabled=bool(value.get("sound_enabled", True)),
            auto_select_usb_mic=bool(value.get("auto_select_usb_mic", True)),
        ).normalized()
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return BridgeSettings()


def save_settings(settings: BridgeSettings) -> None:
    normalized = settings.normalized()
    value = {
        "interval": normalized.interval,
        "sound_enabled": normalized.sound_enabled,
        "auto_select_usb_mic": normalized.auto_select_usb_mic,
    }
    SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
    temporary = SETTINGS_PATH.with_suffix(".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    os.replace(temporary, SETTINGS_PATH)


def system_uses_dark_mode() -> bool:
    if sys.platform != "darwin":
        return False
    try:
        result = subprocess.run(
            ["defaults", "read", "-g", "AppleInterfaceStyle"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=2,
        )
        return "dark" in result.stdout.lower()
    except (OSError, subprocess.SubprocessError):
        return False


def format_timestamp(value: int) -> str:
    if value <= 0:
        return "—"
    try:
        return datetime.fromtimestamp(value).strftime("%m-%d %H:%M")
    except (OSError, OverflowError, ValueError):
        return "—"


class CompanionWindow:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title(APP_NAME)
        self.root.geometry("920x650")
        self.root.minsize(780, 560)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.events: "queue.Queue[tuple[str, dict[str, Any]]]" = queue.Queue()
        settings = load_settings()
        self.interval_var = tk.StringVar(value=str(settings.interval))
        self.sound_var = tk.BooleanVar(value=settings.sound_enabled)
        self.auto_mic_var = tk.BooleanVar(value=settings.auto_select_usb_mic)
        self.connection_var = tk.StringVar(value="正在检查连接…")
        self.sync_var = tk.StringVar(value="尚未同步")
        self.quota_value_var = tk.StringVar(value="—")
        self.metadata_note_var = tk.StringVar(
            value="任务状态只显示 app-server 明确返回的值，不依据时间猜测。"
        )
        self.path_var = tk.StringVar(value="选择一项查看完整工作目录")
        self.detail_var = tk.StringVar(value="")
        self.audio_input_var = tk.StringVar(value="有线麦克风自动选择已开启")
        self._connected = False
        self._closing = False
        self._agents_by_item: dict[str, AgentMetadata] = {}

        self._build_ui()
        self.apply_theme()
        self.controller = BridgeController(settings, self._bridge_event)
        self.controller.start()
        self.root.after(100, self._drain_events)

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, padding=20)
        outer.pack(fill=tk.BOTH, expand=True)
        outer.columnconfigure(0, weight=1)
        outer.rowconfigure(2, weight=1)

        header = ttk.Frame(outer)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(1, weight=1)
        ttk.Label(header, text="CODEX MICRO", style="Title.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        self.connection_label = ttk.Label(
            header, textvariable=self.connection_var, style="Status.TLabel"
        )
        self.connection_label.grid(row=0, column=1, sticky="e")

        settings_box = ttk.LabelFrame(outer, text="同步设置", padding=14)
        settings_box.grid(row=1, column=0, sticky="ew", pady=(16, 14))
        for column in range(6):
            settings_box.columnconfigure(column, weight=1 if column == 2 else 0)

        ttk.Label(settings_box, text="额度同步间隔").grid(row=0, column=0, sticky="e")
        interval = ttk.Combobox(
            settings_box,
            textvariable=self.interval_var,
            values=[str(value) for value in INTERVALS],
            width=7,
            state="readonly",
        )
        interval.grid(row=0, column=1, sticky="w", padx=(6, 4))
        interval.bind("<<ComboboxSelected>>", self.settings_changed)
        ttk.Label(settings_box, text="秒").grid(row=0, column=2, sticky="w")

        ttk.Checkbutton(
            settings_box,
            text="完成提示音",
            variable=self.sound_var,
            command=self.settings_changed,
        ).grid(row=0, column=3, sticky="w", padx=(18, 14))

        ttk.Checkbutton(
            settings_box,
            text="插线自动选择设备麦克风",
            variable=self.auto_mic_var,
            command=self.settings_changed,
        ).grid(row=0, column=4, sticky="w", padx=(4, 14))

        self.sync_button = ttk.Button(
            settings_box, text="立即同步", command=self.sync_now, style="Accent.TButton"
        )
        self.sync_button.grid(row=0, column=5, sticky="e")

        content = ttk.Frame(outer)
        content.grid(row=2, column=0, sticky="nsew")
        content.columnconfigure(0, weight=1)
        content.rowconfigure(1, weight=1)

        summary = ttk.Frame(content)
        summary.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        summary.columnconfigure(1, weight=1)
        ttk.Label(summary, text="周额度剩余", style="Muted.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        ttk.Label(summary, textvariable=self.quota_value_var, style="Quota.TLabel").grid(
            row=1, column=0, sticky="w", padx=(0, 30)
        )
        ttk.Label(summary, text="Agent 元数据预览", style="Section.TLabel").grid(
            row=0, column=1, sticky="w"
        )
        ttk.Label(
            summary,
            textvariable=self.metadata_note_var,
            style="Muted.TLabel",
            wraplength=650,
        ).grid(row=1, column=1, sticky="w")

        columns = ("slot", "title", "workspace", "status", "created", "updated")
        self.tree = ttk.Treeview(content, columns=columns, show="headings", height=10)
        self.tree.grid(row=1, column=0, sticky="nsew")
        labels = {
            "slot": "Agent",
            "title": "任务标题",
            "workspace": "项目 / 工作区",
            "status": "状态",
            "created": "创建",
            "updated": "更新",
        }
        widths = {
            "slot": 62,
            "title": 260,
            "workspace": 160,
            "status": 82,
            "created": 100,
            "updated": 100,
        }
        for column in columns:
            self.tree.heading(column, text=labels[column])
            self.tree.column(
                column,
                width=widths[column],
                minwidth=50,
                stretch=column in {"title", "workspace"},
                anchor="w" if column not in {"slot", "status"} else "center",
            )
        self.tree.bind("<<TreeviewSelect>>", self._show_selected_path)
        scrollbar = ttk.Scrollbar(content, orient="vertical", command=self.tree.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.tree.configure(yscrollcommand=scrollbar.set)

        path_box = ttk.Frame(content, padding=(0, 10, 0, 0))
        path_box.grid(row=2, column=0, sticky="ew")
        path_box.columnconfigure(0, weight=1)
        ttk.Label(
            path_box,
            textvariable=self.path_var,
            style="Path.TLabel",
            wraplength=850,
        ).grid(row=0, column=0, sticky="w")

        footer = ttk.Separator(outer, orient="horizontal")
        footer.grid(row=3, column=0, sticky="ew", pady=(14, 10))
        footer_row = ttk.Frame(outer)
        footer_row.grid(row=4, column=0, sticky="ew")
        footer_row.columnconfigure(1, weight=1)
        ttk.Label(footer_row, textvariable=self.sync_var, style="Muted.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        ttk.Label(
            footer_row,
            textvariable=self.audio_input_var,
            style="Muted.TLabel",
        ).grid(row=0, column=1, sticky="e", padx=(12, 12))
        ttk.Label(
            footer_row,
            textvariable=self.detail_var,
            style="Muted.TLabel",
            anchor="e",
        ).grid(row=0, column=2, sticky="e")

    def current_settings(self) -> BridgeSettings:
        try:
            interval = int(self.interval_var.get())
        except ValueError:
            interval = 300
        return BridgeSettings(
            interval=interval,
            sound_enabled=self.sound_var.get(),
            auto_select_usb_mic=self.auto_mic_var.get(),
        ).normalized()

    def settings_changed(self, _event: object = None) -> None:
        settings = self.current_settings()
        self.interval_var.set(str(settings.interval))
        try:
            save_settings(settings)
        except OSError as error:
            messagebox.showwarning(APP_NAME, f"设置无法保存：{error}")
        self.controller.update_settings(settings)
        self.sync_var.set("设置已保存；连接存在时将立即同步")

    def apply_theme(self) -> None:
        dark = True
        accent = "#21b8ff"
        colors = (
            {
                "bg": "#10161d",
                "panel": "#18222d",
                "fg": "#e8f3ff",
                "muted": "#8aa0b7",
                "accent": accent,
                "select": "#174b68",
                "border": "#2a3a49",
            }
            if dark
            else {
                "bg": "#eef4f8",
                "panel": "#ffffff",
                "fg": "#112331",
                "muted": "#617585",
                "accent": "#0077b8",
                "select": "#b9e6fb",
                "border": "#c7d5df",
            }
        )
        self.root.configure(background=colors["bg"])
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", background=colors["bg"], foreground=colors["fg"])
        style.configure("TFrame", background=colors["bg"])
        style.configure("TLabel", background=colors["bg"], foreground=colors["fg"])
        style.configure(
            "TLabelframe",
            background=colors["panel"],
            bordercolor=colors["border"],
        )
        style.configure(
            "TLabelframe.Label", background=colors["bg"], foreground=colors["fg"]
        )
        style.configure("Title.TLabel", font=("Helvetica Neue", 20, "bold"))
        style.configure("Status.TLabel", foreground=colors["accent"])
        style.configure("Section.TLabel", font=("Helvetica Neue", 13, "bold"))
        style.configure("Quota.TLabel", font=("Menlo", 25, "bold"), foreground=colors["accent"])
        style.configure("Muted.TLabel", foreground=colors["muted"])
        style.configure("Path.TLabel", foreground=colors["muted"], font=("Menlo", 10))
        style.configure(
            "Treeview",
            background=colors["panel"],
            fieldbackground=colors["panel"],
            foreground=colors["fg"],
            bordercolor=colors["border"],
            rowheight=30,
        )
        style.configure(
            "Treeview.Heading",
            background=colors["bg"],
            foreground=colors["fg"],
            bordercolor=colors["border"],
        )
        style.map(
            "Treeview",
            background=[("selected", colors["select"])],
            foreground=[("selected", colors["fg"])],
        )
        style.configure(
            "Accent.TButton",
            background=colors["accent"],
            foreground="#ffffff",
            padding=(12, 7),
        )
        style.map("Accent.TButton", background=[("active", colors["select"])])

    def sync_now(self) -> None:
        self.controller.sync_now()
        if self._connected:
            self.sync_var.set("正在同步…")
        else:
            self.sync_var.set("已排队；连接设备后同步（不会主动扫描）")

    def _bridge_event(self, event: str, payload: dict[str, Any]) -> None:
        self.events.put((event, payload))

    def _drain_events(self) -> None:
        if self._closing:
            return
        try:
            while True:
                event, payload = self.events.get_nowait()
                self._handle_event(event, payload)
        except queue.Empty:
            pass
        self.root.after(100, self._drain_events)

    def _handle_event(self, event: str, payload: dict[str, Any]) -> None:
        if event == "audio_input":
            self.audio_input_var.set(str(payload.get("detail", "")))
            return

        if event == "connection":
            state = str(payload.get("state", ""))
            self._connected = state == "connected"
            prefix = {
                "connected": "●",
                "connecting": "◐",
                "busy": "◐",
                "disconnected": "○",
                "error": "!",
                "stopped": "○",
            }.get(state, "○")
            self.connection_var.set(f"{prefix} {payload.get('detail', '')}")
            return

        if event == "snapshot":
            agents = payload.get("agents") or []
            usage = payload.get("usage")
            errors = payload.get("errors") or []
            if isinstance(usage, dict):
                self.quota_value_var.set(f"{usage.get('weekly_left', '—')}%")
            else:
                self.quota_value_var.set("—")
            self._show_agents(agents)
            synced_at = int(payload.get("synced_at") or 0)
            self.sync_var.set(
                "最近同步 " + datetime.fromtimestamp(synced_at).strftime("%H:%M:%S")
                if synced_at
                else "同步完成"
            )
            packet_sizes = payload.get("packet_sizes") or []
            self.detail_var.set(
                f"{len(agents)} 项 · GATT {len(packet_sizes)} 包"
                + (f" · {errors[0]}" if errors else "")
            )
            if agents and all(agent.status == "unknown" for agent in agents):
                self.metadata_note_var.set(
                    "标题、目录和时间来自 thread/list；独立 app-server 无法观察 "
                    "Desktop 进程实时状态，因此显示“不可用”。"
                )
            elif errors:
                self.metadata_note_var.set("；".join(str(error) for error in errors))
            else:
                self.metadata_note_var.set("任务信息来自 Codex app-server thread/list。")
            return

        if event == "completed":
            try:
                self.root.bell()
            except tk.TclError:
                pass

    def _show_agents(self, agents: list[AgentMetadata]) -> None:
        self.tree.delete(*self.tree.get_children())
        self._agents_by_item.clear()
        for index in range(6):
            if index < len(agents):
                agent = agents[index]
                title = agent.title + (" *" if agent.title_from_preview else "")
                item = self.tree.insert(
                    "",
                    "end",
                    values=(
                        f"{index + 1}",
                        title,
                        agent.workspace or "—",
                        STATUS_LABELS.get(agent.status, "不可用"),
                        format_timestamp(agent.created_at),
                        format_timestamp(agent.updated_at),
                    ),
                )
                self._agents_by_item[item] = agent
            else:
                self.tree.insert(
                    "", "end", values=(f"{index + 1}", "—", "—", "—", "—", "—")
                )

    def _show_selected_path(self, _event: object = None) -> None:
        selection = self.tree.selection()
        if not selection:
            return
        agent = self._agents_by_item.get(selection[0])
        if agent is None:
            self.path_var.set("该槽位暂无任务")
            return
        source = "首条消息摘要" if agent.title_from_preview else "任务标题"
        self.path_var.set(f"{source} · {agent.cwd or '工作目录不可用'}")

    def close(self) -> None:
        self._closing = True
        self.controller.stop()
        self.controller.join(timeout=1.5)
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    CompanionWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()

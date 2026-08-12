#!/usr/bin/env python3
# Project publication and maintenance: 路灯同学创业笔记
# X: https://x.com/LDstartupnotes
# Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
# https://github.com/streetlightstartupnotes
"""Codex Micro companion bridge.

The bridge only retrieves peripherals that macOS already considers connected.
It never starts a BLE scan, and it starts Codex app-server only after a GATT
connection has been established.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import queue
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Callable, Iterable, Optional

from bleak import BleakClient
from process_lock import ProcessLock


CHARACTERISTIC_UUID = "df2b7c01-76b6-4b6c-a8c7-c653e4342010"
SERVICE_UUID = "df2b7c00-76b6-4b6c-a8c7-c653e4342010"
PROTOCOL_VERSION = 11
MAX_AGENTS = 6
# macOS commonly negotiates a 185-byte ATT MTU. Leave room for ATT framing.
MAX_GATT_PACKET_BYTES = 180
SESSION_TAIL_BYTES = 512 * 1024
SESSION_FILES_TO_CHECK = 12

STATUS_LABELS = {
    "running": "运行中",
    "waiting": "等待中",
    "idle": "已空闲",
    "error": "异常",
    "unknown": "不可用",
}
STATUS_CODES = {
    "running": "r",
    "waiting": "w",
    "idle": "i",
    "error": "e",
    "unknown": "u",
}


@dataclass(frozen=True)
class BridgeSettings:
    interval: int = 300
    sound_enabled: bool = True
    auto_select_usb_mic: bool = True

    def normalized(self) -> "BridgeSettings":
        return replace(
            self,
            interval=max(15, min(3600, int(self.interval))),
            sound_enabled=bool(self.sound_enabled),
            auto_select_usb_mic=bool(self.auto_select_usb_mic),
        )


@dataclass(frozen=True)
class AgentMetadata:
    thread_id: str
    title: str
    workspace: str
    cwd: str
    status: str
    created_at: int
    updated_at: int
    title_from_preview: bool = False


async def connected_device(device_name: str):
    """Return an already-connected macOS peripheral without scanning."""
    if sys.platform != "darwin":
        return None

    from CoreBluetooth import CBUUID
    from bleak.backends.corebluetooth.CentralManagerDelegate import (
        CentralManagerDelegate,
    )
    from bleak.backends.device import BLEDevice

    manager = CentralManagerDelegate.alloc().init()
    service = CBUUID.UUIDWithString_(SERVICE_UUID)
    peripherals = manager.central_manager.retrieveConnectedPeripheralsWithServices_(
        [service]
    )
    for peripheral in peripherals:
        if peripheral.name() == device_name:
            return BLEDevice(
                peripheral.identifier().UUIDString(),
                peripheral.name(),
                (peripheral, manager),
                0,
            )
    return None


class AppServer:
    """Small serialized JSON-RPC client for ``codex app-server``."""

    def __init__(self, executable: str) -> None:
        self._process = subprocess.Popen(
            [executable, "app-server"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._messages: "queue.Queue[dict[str, Any]]" = queue.Queue()
        self._responses: dict[int, dict[str, Any]] = {}
        self._request_lock = threading.Lock()
        self._close_lock = threading.Lock()
        self._next_id = 1
        threading.Thread(target=self._reader, daemon=True).start()
        self._send(
            {
                "method": "initialize",
                "id": 0,
                "params": {
                    "clientInfo": {
                        "name": "codex_micro_companion",
                        "title": "Codex Micro Companion",
                        "version": "1.1.0",
                    }
                },
            }
        )
        self._wait_for(0, timeout=15)
        self._send({"method": "initialized", "params": {}})

    def _reader(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            try:
                self._messages.put(json.loads(line))
            except json.JSONDecodeError:
                continue
        self._messages.put({"_closed": True})

    def _send(self, message: dict[str, Any]) -> None:
        if self._process.poll() is not None:
            raise RuntimeError("Codex app-server 已退出")
        assert self._process.stdin is not None
        self._process.stdin.write(
            json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n"
        )
        self._process.stdin.flush()

    def _wait_for(self, request_id: int, timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            cached = self._responses.pop(request_id, None)
            if cached is not None:
                message = cached
            else:
                try:
                    message = self._messages.get(
                        timeout=max(0.1, deadline - time.monotonic())
                    )
                except queue.Empty:
                    break
            if message.get("_closed"):
                raise RuntimeError("Codex app-server 已关闭输出")
            message_id = message.get("id")
            if message_id != request_id:
                if isinstance(message_id, int):
                    self._responses[message_id] = message
                # Notifications are deliberately ignored. This bridge does not
                # load or mutate threads, so polling is sufficient and safer.
                continue
            if "error" in message:
                raise RuntimeError(str(message["error"]))
            result = message.get("result", {})
            return result if isinstance(result, dict) else {}
        raise TimeoutError(f"等待 app-server 请求 {request_id} 超时")

    def request(
        self, method: str, params: Optional[dict[str, Any]], timeout: float = 20
    ) -> dict[str, Any]:
        with self._request_lock:
            request_id = self._next_id
            self._next_id += 1
            self._send(
                {
                    "method": method,
                    "id": request_id,
                    "params": params,
                }
            )
            return self._wait_for(request_id, timeout=timeout)

    def rate_limits(self) -> dict[str, Any]:
        try:
            return self.request("account/rateLimits/read", None)
        except RuntimeError:
            # A standalone app-server authenticated with an API key cannot
            # read ChatGPT plan limits. Codex Desktop still records the same
            # official rate-limit snapshot in local token_count events, so use
            # the newest snapshot instead of guessing or displaying bad data.
            local_snapshot = local_session_rate_limits()
            if local_snapshot is None:
                raise
            return local_snapshot

    def recent_threads(self, limit: int = MAX_AGENTS) -> list[AgentMetadata]:
        count = max(1, min(MAX_AGENTS, int(limit)))
        params = {
            "limit": count,
            "archived": False,
            "sortKey": "updated_at",
            "sortDirection": "desc",
        }
        try:
            response = self.request("thread/list", params)
        except RuntimeError as first_error:
            # Older app-server builds accepted thread/list but not its sorting
            # fields. A simple list still carries timestamps, so sort locally.
            try:
                response = self.request("thread/list", {"limit": count})
            except Exception:
                raise first_error

        values = response.get("data")
        if not isinstance(values, list):
            raise RuntimeError("thread/list 未返回 data 数组")
        rows = [value for value in values if isinstance(value, dict)]
        rows.sort(key=lambda value: _integer(value.get("updatedAt")), reverse=True)
        return [_agent_from_thread(value) for value in rows[:count]]

    def close(self) -> None:
        with self._close_lock:
            if self._process.poll() is None:
                self._process.terminate()
                try:
                    self._process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self._process.kill()
                    self._process.wait(timeout=2)


def _integer(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    if isinstance(value, (int, float)):
        return int(value)
    return default


def _session_window(value: Any) -> Optional[dict[str, Any]]:
    if not isinstance(value, dict):
        return None
    used = value.get("used_percent")
    duration = value.get("window_minutes")
    resets_at = value.get("resets_at")
    if isinstance(used, bool) or not isinstance(used, (int, float)):
        return None
    if isinstance(duration, bool) or not isinstance(duration, (int, float)):
        return None
    normalized: dict[str, Any] = {
        "usedPercent": max(0, min(100, int(used))),
        "windowDurationMins": max(0, int(duration)),
    }
    if isinstance(resets_at, int) and not isinstance(resets_at, bool):
        normalized["resetsAt"] = resets_at
    return normalized


def local_session_rate_limits() -> Optional[dict[str, Any]]:
    """Return the newest official Codex rate-limit snapshot stored locally.

    Only the tail of recent JSONL files is inspected, and only the structured
    ``payload.rate_limits`` object is retained. Conversation text is ignored.
    """
    configured_home = os.environ.get("CODEX_HOME")
    codex_root = Path(configured_home).expanduser() if configured_home else Path.home() / ".codex"
    sessions_root = codex_root / "sessions"
    try:
        files = sorted(
            sessions_root.rglob("*.jsonl"),
            key=lambda path: path.stat().st_mtime_ns,
            reverse=True,
        )[:SESSION_FILES_TO_CHECK]
    except OSError:
        return None

    newest_score: tuple[int, str] = (-1, "")
    newest_limits: Optional[dict[str, Any]] = None
    for path in files:
        try:
            with path.open("rb") as handle:
                size = handle.seek(0, os.SEEK_END)
                start = max(0, size - SESSION_TAIL_BYTES)
                handle.seek(start)
                if start > 0:
                    handle.readline()
                lines = handle.read().splitlines()
        except OSError:
            continue

        for raw_line in reversed(lines):
            try:
                record = json.loads(raw_line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if not isinstance(record, dict):
                continue
            payload = record.get("payload")
            if not isinstance(payload, dict):
                continue
            rate_limits = payload.get("rate_limits")
            if not isinstance(rate_limits, dict):
                continue
            if rate_limits.get("limit_id") not in {None, "codex"}:
                continue
            primary = _session_window(rate_limits.get("primary"))
            secondary = _session_window(rate_limits.get("secondary"))
            if primary is None and secondary is None:
                continue
            timestamp = record.get("timestamp")
            score = (
                1 if isinstance(timestamp, str) and timestamp else 0,
                timestamp if isinstance(timestamp, str) else str(path.stat().st_mtime_ns),
            )
            if score > newest_score:
                newest_score = score
                newest_limits = {"primary": primary, "secondary": secondary}
            break

    if newest_limits is None:
        return None
    return {"rateLimitsByLimitId": {"codex": newest_limits}}


def _clean_text(value: Any) -> str:
    if not isinstance(value, str):
        return ""
    return " ".join(value.replace("\x00", "").split())


def normalize_thread_status(value: Any) -> str:
    """Map documented status values; never infer activity from timestamps."""
    if isinstance(value, dict):
        status_type = value.get("type")
        flags = value.get("activeFlags")
    else:
        status_type = value
        flags = None
    if status_type == "active":
        if isinstance(flags, list) and any(
            flag in {"waitingOnApproval", "waitingOnUserInput"} for flag in flags
        ):
            return "waiting"
        return "running"
    if status_type == "idle":
        return "idle"
    if status_type == "systemError":
        return "error"
    # A separate app-server reports Desktop-owned threads as notLoaded. That
    # means live status is unavailable, not that the task is idle/completed.
    return "unknown"


def _agent_from_thread(value: dict[str, Any]) -> AgentMetadata:
    explicit_title = _clean_text(value.get("name"))
    preview = _clean_text(value.get("preview"))
    # Empty metadata stays empty. The device hides the whole dynamic line
    # instead of inventing a title or risking unsupported glyph placeholders.
    title = explicit_title or preview
    cwd = _clean_text(value.get("cwd"))
    workspace = Path(cwd).name if cwd else ""
    return AgentMetadata(
        thread_id=_clean_text(value.get("id")),
        title=title,
        workspace=workspace,
        cwd=cwd,
        status=normalize_thread_status(value.get("status")),
        created_at=_integer(value.get("createdAt")),
        updated_at=_integer(value.get("updatedAt")),
        title_from_preview=not bool(explicit_title) and bool(preview),
    )


def find_weekly_window(response: dict[str, Any]) -> Optional[dict[str, Any]]:
    buckets = response.get("rateLimitsByLimitId") or {}
    snapshot = buckets.get("codex") or response.get("rateLimits") or {}
    windows = [snapshot.get("primary"), snapshot.get("secondary")]
    valid = [window for window in windows if isinstance(window, dict)]
    if not valid:
        return None
    return max(valid, key=lambda value: value.get("windowDurationMins") or 0)


def compact_usage(response: dict[str, Any]) -> dict[str, int]:
    window = find_weekly_window(response)
    if window is None:
        raise RuntimeError("Codex 未返回额度窗口")
    used = max(0, min(100, _integer(window.get("usedPercent"))))
    resets_at = window.get("resetsAt")
    reset_seconds = -1
    if isinstance(resets_at, int):
        reset_seconds = max(0, resets_at - int(time.time()))
    return {"weekly_left": 100 - used, "reset_seconds": reset_seconds}


def _truncate_utf8(value: str, max_bytes: int, keep_end: bool = False) -> str:
    value = _clean_text(value)
    encoded = value.encode("utf-8")
    if len(encoded) <= max_bytes:
        return value
    if max_bytes <= 3:
        return ""
    room = max_bytes - len("…".encode("utf-8"))
    if keep_end:
        tail = encoded[-room:].decode("utf-8", errors="ignore")
        return "…" + tail
    head = encoded[:room].decode("utf-8", errors="ignore")
    return head + "…"


def _json_bytes(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), allow_nan=False
    ).encode("utf-8")


def _agent_packet(index: int, agent: AgentMetadata) -> bytes:
    fields: list[Any] = [
        index,
        _truncate_utf8(agent.title, 54),
        _truncate_utf8(agent.workspace, 28),
        _truncate_utf8(agent.cwd, 72, keep_end=True),
        STATUS_CODES.get(agent.status, "u"),
        agent.created_at,
        agent.updated_at,
    ]
    minimums = {1: 9, 2: 5, 3: 12}
    while True:
        encoded = _json_bytes({"v": PROTOCOL_VERSION, "a": fields})
        if len(encoded) <= MAX_GATT_PACKET_BYTES:
            return encoded
        candidates = [
            position
            for position, minimum in minimums.items()
            if len(str(fields[position]).encode("utf-8")) > minimum
        ]
        if not candidates:
            raise ValueError("Agent 元数据无法压缩到 GATT 包限制")
        position = max(
            candidates, key=lambda item: len(str(fields[item]).encode("utf-8"))
        )
        current_size = len(str(fields[position]).encode("utf-8"))
        target_size = max(minimums[position], current_size - 8)
        fields[position] = _truncate_utf8(
            str(fields[position]), target_size, keep_end=position == 3
        )


def build_wire_packets(
    settings: BridgeSettings,
    usage: Optional[dict[str, int]],
    agents: Iterable[AgentMetadata],
) -> list[bytes]:
    """Build backwards-compatible usage plus compact per-agent JSON packets."""
    normalized = settings.normalized()
    agent_list = list(agents)[:MAX_AGENTS]
    header: dict[str, Any] = {
        "v": PROTOCOL_VERSION,
        "ts": int(time.time()),
        "c": [
            1,
            normalized.interval,
            0,
            int(normalized.sound_enabled),
        ],
        "n": len(agent_list),
    }
    if usage is not None:
        # Keep the V1 field names so existing firmware still updates quota.
        header["weekly_left"] = _integer(usage.get("weekly_left"), -1)
        header["reset_seconds"] = _integer(usage.get("reset_seconds"), -1)
    packets = [_json_bytes(header)]
    packets.extend(_agent_packet(index, agent) for index, agent in enumerate(agent_list))
    if any(len(packet) > MAX_GATT_PACKET_BYTES for packet in packets):
        raise AssertionError("GATT packet exceeded byte budget")
    return packets


def default_codex_path() -> str:
    bundled = Path("/Applications/ChatGPT.app/Contents/Resources/codex")
    if bundled.exists():
        return str(bundled)
    return os.environ.get("CODEX_BIN", "codex")


BridgeCallback = Callable[[str, dict[str, Any]], None]


class BridgeController:
    """Thread-safe lifecycle wrapper shared by the CLI and Tk GUI."""

    def __init__(
        self,
        settings: BridgeSettings,
        callback: BridgeCallback,
        device_name: str = "Codex Micro",
        codex_path: Optional[str] = None,
    ) -> None:
        self._settings = settings.normalized()
        self._callback = callback
        self._device_name = device_name
        self._codex_path = codex_path or default_codex_path()
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._force_sync = True
        self._thread: Optional[threading.Thread] = None
        self._active_server: Optional[AppServer] = None
        self._last_state = ""
        self._audio_selector = None
        self._ble_writer_lock = ProcessLock()
        self._owns_process_lock = False

    def start(self) -> bool:
        if self._thread is not None and self._thread.is_alive():
            return True
        if not self._ble_writer_lock.acquire():
            self._emit_state("busy", "已有 Companion 正在运行，当前实例未启动")
            return False
        self._owns_process_lock = True
        try:
            if sys.platform == "darwin":
                from mac_audio_input import AutomaticInputSelector

                self._audio_selector = AutomaticInputSelector(
                    enabled=self._settings.auto_select_usb_mic,
                    callback=self._callback,
                )
                self._audio_selector.start()
            self._thread = threading.Thread(target=self._thread_main, daemon=True)
            self._thread.start()
        except Exception:
            self._stop_audio_selector()
            self._release_process_lock()
            raise
        return True

    def stop(self) -> None:
        self._stop.set()
        if self._audio_selector is not None:
            self._audio_selector.stop()
        with self._lock:
            server = self._active_server
        if server is not None:
            server.close()

    def join(self, timeout: float = 5) -> None:
        thread = self._thread
        if thread is not None:
            thread.join(timeout=timeout)
        self._stop_audio_selector(timeout=min(timeout, 3))
        if thread is None or not thread.is_alive():
            self._release_process_lock()

    def _stop_audio_selector(self, timeout: float = 3) -> None:
        selector = self._audio_selector
        if selector is None:
            return
        self._audio_selector = None
        selector.stop()
        try:
            selector.join(timeout=timeout)
        except RuntimeError:
            # Thread.start() itself may have failed before the selector thread
            # became joinable. The BLE writer lock must still be released.
            pass

    def _release_process_lock(self) -> None:
        with self._lock:
            if not self._owns_process_lock:
                return
            self._owns_process_lock = False
        self._ble_writer_lock.release()

    def update_settings(self, settings: BridgeSettings) -> None:
        normalized = settings.normalized()
        with self._lock:
            self._settings = normalized
            self._force_sync = True
        if self._audio_selector is not None:
            self._audio_selector.set_enabled(normalized.auto_select_usb_mic)

    def sync_now(self) -> None:
        with self._lock:
            self._force_sync = True

    def _snapshot_settings(self) -> BridgeSettings:
        with self._lock:
            return self._settings

    def _take_force_sync(self) -> bool:
        with self._lock:
            result = self._force_sync
            self._force_sync = False
            return result

    def _emit(self, event: str, **payload: Any) -> None:
        try:
            self._callback(event, payload)
        except Exception:
            pass

    def _emit_state(self, state: str, detail: str) -> None:
        if state == self._last_state:
            return
        self._last_state = state
        self._emit("connection", state=state, detail=detail)

    async def _pause(self, seconds: float) -> None:
        deadline = time.monotonic() + seconds
        while not self._stop.is_set() and time.monotonic() < deadline:
            if self._take_force_sync():
                return
            await asyncio.sleep(min(0.25, max(0.0, deadline - time.monotonic())))

    def _thread_main(self) -> None:
        try:
            asyncio.run(self._run_forever())
        except Exception as error:
            self._emit_state("error", str(error))
        finally:
            self._stop_audio_selector()
            self._release_process_lock()

    async def _run_forever(self) -> None:
        while not self._stop.is_set():
            server: Optional[AppServer] = None
            try:
                device = await connected_device(self._device_name)
                if device is None:
                    detail = (
                        "仅支持 macOS 已连接设备"
                        if sys.platform != "darwin"
                        else "等待系统中已连接的 Codex Micro（未扫描）"
                    )
                    self._emit_state("disconnected", detail)
                    await self._pause(3)
                    continue

                self._emit_state("connecting", "正在复用系统已有蓝牙连接")
                async with BleakClient(device) as client:
                    self._emit_state("connected", f"已连接 {device.name}")
                    next_sync = 0.0
                    previous_status: dict[str, str] = {}
                    while client.is_connected and not self._stop.is_set():
                        forced = self._take_force_sync()
                        now = time.monotonic()
                        if not forced and now < next_sync:
                            await self._pause(min(1.0, next_sync - now))
                            continue

                        settings = self._snapshot_settings()
                        usage: Optional[dict[str, int]] = None
                        agents: list[AgentMetadata] = []
                        errors: list[str] = []

                        # Important invariant: app-server is constructed only
                        # inside a confirmed BLE client connection.
                        if server is None:
                            try:
                                server = AppServer(self._codex_path)
                                with self._lock:
                                    self._active_server = server
                            except Exception as error:
                                errors.append(f"app-server：{error}")

                        if server is not None:
                            try:
                                usage = compact_usage(server.rate_limits())
                            except Exception as error:
                                errors.append(f"额度：{error}")
                            try:
                                agents = server.recent_threads(MAX_AGENTS)
                            except Exception as error:
                                errors.append(f"Agent 元数据：{error}")

                        packets = build_wire_packets(settings, usage, agents)
                        for packet in packets:
                            await client.write_gatt_char(
                                CHARACTERISTIC_UUID, packet, response=True
                            )
                            # Yield between acknowledged writes. This keeps
                            # CoreBluetooth and Bluedroid from batching a
                            # complete six-Agent snapshot into one callback
                            # burst on slower BLE connection intervals.
                            await asyncio.sleep(0.05)

                        current_status = {
                            agent.thread_id: agent.status
                            for agent in agents
                            if agent.thread_id
                        }
                        completed = [
                            agent
                            for agent in agents
                            if agent.thread_id
                            and previous_status.get(agent.thread_id)
                            in {"running", "waiting"}
                            and agent.status == "idle"
                        ]
                        previous_status = current_status
                        self._emit(
                            "snapshot",
                            agents=agents,
                            usage=usage,
                            errors=errors,
                            synced_at=int(time.time()),
                            packet_sizes=[len(packet) for packet in packets],
                        )
                        if completed and settings.sound_enabled:
                            self._emit("completed", agents=completed)
                        next_sync = time.monotonic() + settings.interval
                        await self._pause(min(1.0, settings.interval))
            except Exception as error:
                if not self._stop.is_set():
                    self._emit_state("error", f"连接已结束：{error}")
                    await self._pause(3)
            finally:
                if server is not None:
                    with self._lock:
                        if self._active_server is server:
                            self._active_server = None
                    server.close()
        self._emit_state("stopped", "同步已停止")


async def run(args: argparse.Namespace) -> None:
    stopped = asyncio.Event()

    def callback(event: str, payload: dict[str, Any]) -> None:
        if event == "connection":
            print(payload.get("detail", ""), flush=True)
        elif event == "snapshot":
            usage = payload.get("usage")
            agents = payload.get("agents") or []
            if usage:
                print(
                    f"周额度剩余 {usage['weekly_left']}%，"
                    f"{len(agents)} 个任务元数据已同步",
                    flush=True,
                )
            else:
                print(f"{len(agents)} 个任务元数据已同步", flush=True)
            for error in payload.get("errors") or []:
                print(error, file=sys.stderr, flush=True)

    controller = BridgeController(
        BridgeSettings(
            interval=args.interval,
            sound_enabled=not args.silent,
            auto_select_usb_mic=not args.no_auto_usb_mic,
        ),
        callback,
        device_name=args.device,
        codex_path=args.codex,
    )
    if not controller.start():
        return
    try:
        while not stopped.is_set():
            await asyncio.sleep(1)
    finally:
        controller.stop()
        controller.join()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="Codex Micro")
    parser.add_argument("--codex", default=default_codex_path())
    parser.add_argument("--interval", type=int, default=300)
    parser.add_argument("--silent", action="store_true")
    parser.add_argument(
        "--no-auto-usb-mic",
        action="store_true",
        help="do not select Codex Macro32 Mic as the macOS default input",
    )
    try:
        asyncio.run(run(parser.parse_args()))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

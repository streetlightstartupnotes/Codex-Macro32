#!/usr/bin/env python3
# Project publication and maintenance: 路灯同学创业笔记
# X: https://x.com/LDstartupnotes
# Xiaohongshu: https://www.xiaohongshu.com/user/profile/63fd97c1000000001400d0ea
# https://github.com/streetlightstartupnotes
"""Sync Codex account rate-limit data to the portable Codex Micro display."""

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
from pathlib import Path
from typing import Any

from bleak import BleakClient


CHARACTERISTIC_UUID = "df2b7c01-76b6-4b6c-a8c7-c653e4342010"
SERVICE_UUID = "df2b7c00-76b6-4b6c-a8c7-c653e4342010"


async def connected_device(device_name: str):
    """Return an already-connected macOS peripheral even when it no longer advertises."""
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
    def __init__(self, executable: str) -> None:
        self._process = subprocess.Popen(
            [executable, "app-server"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self._messages: queue.Queue[dict[str, Any]] = queue.Queue()
        self._next_id = 1
        threading.Thread(target=self._reader, daemon=True).start()
        self._send(
            {
                "method": "initialize",
                "id": 0,
                "params": {
                    "clientInfo": {
                        "name": "codex_micro_185b_bridge",
                        "title": "Codex Micro 1.85B Usage Bridge",
                        "version": "0.2.0",
                    }
                },
            }
        )
        self._send({"method": "initialized", "params": {}})
        self._wait_for(0, timeout=15)

    def _reader(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            try:
                self._messages.put(json.loads(line))
            except json.JSONDecodeError:
                continue

    def _send(self, message: dict[str, Any]) -> None:
        if self._process.poll() is not None:
            raise RuntimeError("Codex app-server exited")
        assert self._process.stdin is not None
        self._process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
        self._process.stdin.flush()

    def _wait_for(self, request_id: int, timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        deferred: list[dict[str, Any]] = []
        try:
            while time.monotonic() < deadline:
                message = self._messages.get(timeout=max(0.1, deadline - time.monotonic()))
                if message.get("id") == request_id:
                    if "error" in message:
                        raise RuntimeError(str(message["error"]))
                    return message.get("result", {})
                deferred.append(message)
        finally:
            for message in deferred:
                self._messages.put(message)
        raise TimeoutError(f"Timed out waiting for app-server request {request_id}")

    def rate_limits(self) -> dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        self._send({"method": "account/rateLimits/read", "id": request_id, "params": None})
        return self._wait_for(request_id, timeout=20)

    def close(self) -> None:
        if self._process.poll() is None:
            self._process.terminate()


def find_weekly_window(response: dict[str, Any]) -> dict[str, Any] | None:
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
        raise RuntimeError("Codex did not return a rate-limit window")
    used = max(0, min(100, int(window.get("usedPercent", 0))))
    resets_at = window.get("resetsAt")
    reset_seconds = -1
    if isinstance(resets_at, int):
        reset_seconds = max(0, resets_at - int(time.time()))
    return {"weekly_left": 100 - used, "reset_seconds": reset_seconds}


def default_codex_path() -> str:
    bundled = Path("/Applications/ChatGPT.app/Contents/Resources/codex")
    if bundled.exists():
        return str(bundled)
    return os.environ.get("CODEX_BIN", "codex")


async def run(args: argparse.Namespace) -> None:
    waiting_printed = False
    while True:
        device = await connected_device(args.device)
        if device is None:
            if not waiting_printed:
                print("Waiting for an existing Codex Micro Bluetooth connection...")
                waiting_printed = True
            # No active scan: disconnected devices consume no bridge BLE traffic.
            await asyncio.sleep(5)
            continue

        waiting_printed = False
        server = None
        try:
            async with BleakClient(device) as client:
                print(f"Connected: {device.name}")
                server = AppServer(args.codex)
                while client.is_connected:
                    payload = compact_usage(server.rate_limits())
                    encoded = json.dumps(payload, separators=(",", ":")).encode()
                    await client.write_gatt_char(CHARACTERISTIC_UUID, encoded, response=True)
                    print(
                        f"Weekly left {payload['weekly_left']}%, "
                        f"reset in {payload['reset_seconds']} seconds"
                    )
                    await asyncio.sleep(args.interval)
        except Exception as error:  # return to idle after a transient disconnect
            print(f"Connection ended: {error}")
            await asyncio.sleep(3)
        finally:
            if server is not None:
                server.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="Codex Micro")
    parser.add_argument("--codex", default=default_codex_path())
    parser.add_argument("--interval", type=int, default=60)
    asyncio.run(run(parser.parse_args()))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Select a Codex Macro32 serial port and enter the ESP32-S3 ROM loader."""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass

import serial
from serial.tools import list_ports


ESPRESSIF_VID = 0x303A
CODEX_MIC_PID = 0x8361
ESP32S3_ROM_PID = 0x1001


@dataclass(frozen=True)
class Port:
    device: str
    vid: int | None
    pid: int | None
    serial_number: str | None


def available_ports() -> list[Port]:
    return [
        Port(item.device, item.vid, item.pid, item.serial_number)
        for item in list_ports.comports()
    ]


def native_candidates(ports: list[Port]) -> list[Port]:
    return [
        port
        for port in ports
        if port.vid == ESPRESSIF_VID
        and port.pid in {CODEX_MIC_PID, ESP32S3_ROM_PID}
    ]


def select_port(requested: str | None, ports: list[Port]) -> Port:
    if requested:
        for port in ports:
            if port.device == requested:
                return port
        raise RuntimeError(f"串口不存在：{requested}")

    candidates = native_candidates(ports)
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise RuntimeError(
            "没有检测到 Codex Macro32 维护串口或 ESP32-S3 ROM 串口。"
            "不含维护串口的旧固件首次迁移时，需要先进入一次 ROM 下载模式。"
        )
    names = "、".join(port.device for port in candidates)
    raise RuntimeError(f"检测到多个 Codex/ESP32-S3 串口，请明确指定其中一个：{names}")


def wait_for_rom(previous: Port, timeout: float = 12.0) -> Port:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        candidates = [
            port
            for port in available_ports()
            if port.vid == ESPRESSIF_VID and port.pid == ESP32S3_ROM_PID
        ]
        if previous.serial_number:
            matching = [
                port
                for port in candidates
                if port.serial_number == previous.serial_number
            ]
            if len(matching) == 1:
                return matching[0]
        if len(candidates) == 1:
            return candidates[0]
        time.sleep(0.2)
    raise RuntimeError("维护串口已触发，但等待 ESP32-S3 ROM 串口超时")


def prepare(port: Port) -> tuple[str, str]:
    if port.vid == ESPRESSIF_VID and port.pid == ESP32S3_ROM_PID:
        return port.device, "no-reset"
    if port.vid == ESPRESSIF_VID and port.pid == CODEX_MIC_PID:
        # Arduino's native TinyUSB CDC enters the ROM downloader when the host
        # selects 1200 baud. The audio interface remains unrelated to this path.
        connection = None
        try:
            connection = serial.Serial(port.device, baudrate=1200, timeout=0.1)
            connection.dtr = False
            connection.rts = False
        except (OSError, serial.SerialException):
            # A successful 1200-baud touch disconnects the CDC interface
            # immediately, which pyserial may surface as an I/O error. The ROM
            # port appearing below is the authoritative success condition.
            pass
        finally:
            if connection is not None:
                try:
                    connection.close()
                except (OSError, serial.SerialException):
                    pass
        rom = wait_for_rom(port)
        return rom.device, "no-reset"

    # An explicitly selected third-party UART bridge still uses its normal
    # DTR/RTS reset circuit. It is never selected implicitly.
    return port.device, "default-reset"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?")
    args = parser.parse_args()
    try:
        device, before = prepare(select_port(args.port, available_ports()))
    except (RuntimeError, OSError, serial.SerialException) as error:
        print(f"刷写端口准备失败：{error}", file=sys.stderr)
        return 2
    print(f"{device}|{before}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

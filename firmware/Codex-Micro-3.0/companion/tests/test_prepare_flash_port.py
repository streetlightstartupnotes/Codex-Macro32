from __future__ import annotations

import importlib.util
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).parents[2] / "scripts" / "prepare-flash-port.py"

# The public Companion environment does not need pyserial; flash.command uses
# PlatformIO's own Python, which already provides it. Supply a tiny import stub
# so these pure selection/state-machine tests stay dependency-free.
serial_stub = types.ModuleType("serial")
serial_tools_stub = types.ModuleType("serial.tools")
list_ports_stub = types.ModuleType("serial.tools.list_ports")


class SerialException(Exception):
    pass


serial_stub.SerialException = SerialException
serial_stub.Serial = object
list_ports_stub.comports = lambda: []
serial_tools_stub.list_ports = list_ports_stub
serial_stub.tools = serial_tools_stub
sys.modules.setdefault("serial", serial_stub)
sys.modules.setdefault("serial.tools", serial_tools_stub)
sys.modules.setdefault("serial.tools.list_ports", list_ports_stub)

SPEC = importlib.util.spec_from_file_location("prepare_flash_port", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
prepare_flash_port = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = prepare_flash_port
SPEC.loader.exec_module(prepare_flash_port)


class PrepareFlashPortTests(unittest.TestCase):
    def port(self, device: str, vid: int | None, pid: int | None):
        return prepare_flash_port.Port(device, vid, pid, "SERIAL")

    def test_selects_only_codex_maintenance_port_implicitly(self):
        ports = [
            self.port("/dev/cu.other", 0x10C4, 0xEA60),
            self.port("/dev/cu.codex", 0x303A, 0x8361),
        ]
        selected = prepare_flash_port.select_port(None, ports)
        self.assertEqual(selected.device, "/dev/cu.codex")

    def test_rejects_ambiguous_espressif_ports(self):
        ports = [
            self.port("/dev/cu.codex", 0x303A, 0x8361),
            self.port("/dev/cu.rom", 0x303A, 0x1001),
        ]
        with self.assertRaises(RuntimeError):
            prepare_flash_port.select_port(None, ports)

    def test_rom_port_needs_no_reset(self):
        rom = self.port("/dev/cu.rom", 0x303A, 0x1001)
        self.assertEqual(prepare_flash_port.prepare(rom), (rom.device, "no-reset"))

    def test_explicit_third_party_uart_uses_default_reset(self):
        uart = self.port("/dev/cu.uart", 0x10C4, 0xEA60)
        self.assertEqual(
            prepare_flash_port.prepare(uart), (uart.device, "default-reset")
        )

    @patch.object(prepare_flash_port, "wait_for_rom")
    @patch.object(prepare_flash_port.serial, "Serial")
    def test_maintenance_port_uses_1200_baud_then_waits_for_rom(
        self, serial_constructor, wait_for_rom
    ):
        maintenance = self.port("/dev/cu.codex", 0x303A, 0x8361)
        rom = self.port("/dev/cu.rom", 0x303A, 0x1001)
        wait_for_rom.return_value = rom

        self.assertEqual(
            prepare_flash_port.prepare(maintenance), (rom.device, "no-reset")
        )
        serial_constructor.assert_called_once_with(
            maintenance.device, baudrate=1200, timeout=0.1
        )
        wait_for_rom.assert_called_once_with(maintenance)


if __name__ == "__main__":
    unittest.main()

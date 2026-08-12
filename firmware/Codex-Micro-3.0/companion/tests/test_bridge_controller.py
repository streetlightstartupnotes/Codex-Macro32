from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from codex_usage_bridge import BridgeController, BridgeSettings
from process_lock import ProcessLock


class FailingAudioSelector:
    def __init__(self, **_: object) -> None:
        pass

    def start(self) -> None:
        raise RuntimeError("selector start failed")

    def stop(self) -> None:
        pass

    def join(self, timeout: float = 3) -> None:
        del timeout
        raise RuntimeError("thread was never started")


class RunningThread:
    def join(self, timeout: float = 5) -> None:
        del timeout

    def is_alive(self) -> bool:
        return True


class BridgeControllerTests(unittest.TestCase):
    def controller_with_lock(self, path: Path) -> BridgeController:
        controller = BridgeController(BridgeSettings(), lambda *_: None)
        controller._ble_writer_lock = ProcessLock(path)
        return controller

    def test_start_failure_releases_process_lock(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "writer.lock"
            controller = self.controller_with_lock(path)
            with (
                patch("codex_usage_bridge.sys.platform", "darwin"),
                patch(
                    "mac_audio_input.AutomaticInputSelector",
                    FailingAudioSelector,
                ),
                self.assertRaisesRegex(RuntimeError, "selector start failed"),
            ):
                controller.start()

            contender = ProcessLock(path)
            self.assertTrue(contender.acquire())
            contender.release()

    def test_join_keeps_lock_while_worker_is_alive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "writer.lock"
            controller = self.controller_with_lock(path)
            self.assertTrue(controller._ble_writer_lock.acquire())
            controller._owns_process_lock = True
            controller._thread = RunningThread()

            controller.join(timeout=0)

            contender = ProcessLock(path)
            self.assertFalse(contender.acquire())
            controller._release_process_lock()
            self.assertTrue(contender.acquire())
            contender.release()


if __name__ == "__main__":
    unittest.main()

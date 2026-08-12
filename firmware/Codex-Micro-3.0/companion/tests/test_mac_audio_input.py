from __future__ import annotations

import unittest
import time

from mac_audio_input import AudioInputDevice, AutomaticInputSelector


class FakeBackend:
    def __init__(self) -> None:
        self.builtin = AudioInputDevice(1, "builtin", "Mac 麦克风")
        self.codex = AudioInputDevice(2, "codex", "Codex Macro32 Mic")
        self.available = [self.builtin]
        self.current = self.builtin

    def devices(self):
        return list(self.available)

    def default_input(self):
        return self.current

    def set_default_input(self, device):
        self.current = device


class AutomaticInputSelectorTests(unittest.TestCase):
    def test_arrival_selects_and_removal_restores(self) -> None:
        backend = FakeBackend()
        selector = AutomaticInputSelector(backend=backend)
        selector.poll_once()
        backend.available.append(backend.codex)
        selector.poll_once()
        self.assertEqual(backend.current, backend.codex)
        backend.available.remove(backend.codex)
        selector.poll_once()
        self.assertEqual(backend.current, backend.builtin)

    def test_manual_override_is_preserved(self) -> None:
        backend = FakeBackend()
        selector = AutomaticInputSelector(backend=backend)
        selector.poll_once()
        backend.available.append(backend.codex)
        selector.poll_once()
        backend.current = backend.builtin
        selector.poll_once()
        backend.available.remove(backend.codex)
        selector.poll_once()
        self.assertEqual(backend.current, backend.builtin)

    def test_disabling_restores_previous_input(self) -> None:
        backend = FakeBackend()
        selector = AutomaticInputSelector(backend=backend)
        selector.poll_once()
        backend.available.append(backend.codex)
        selector.poll_once()
        selector.set_enabled(False)
        selector.poll_once()
        self.assertEqual(backend.current, backend.builtin)

    def test_stopping_background_selector_restores_previous_input(self) -> None:
        backend = FakeBackend()
        backend.available.append(backend.codex)
        selector = AutomaticInputSelector(backend=backend, poll_seconds=0.01)
        selector.start()
        for _ in range(50):
            if backend.current == backend.codex:
                break
            time.sleep(0.01)
        self.assertEqual(backend.current, backend.codex)
        selector.stop()
        selector.join()
        self.assertEqual(backend.current, backend.builtin)


if __name__ == "__main__":
    unittest.main()

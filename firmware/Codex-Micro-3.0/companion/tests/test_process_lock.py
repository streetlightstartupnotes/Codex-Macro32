from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from process_lock import ProcessLock


class ProcessLockTests(unittest.TestCase):
    def test_second_writer_waits_until_first_releases(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "writer.lock"
            first = ProcessLock(path)
            second = ProcessLock(path)
            self.assertTrue(first.acquire())
            self.assertFalse(second.acquire())
            first.release()
            self.assertTrue(second.acquire())
            second.release()

    def test_acquire_is_idempotent_for_owner(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            lock = ProcessLock(Path(directory) / "writer.lock")
            self.assertTrue(lock.acquire())
            self.assertTrue(lock.acquire())
            lock.release()


if __name__ == "__main__":
    unittest.main()

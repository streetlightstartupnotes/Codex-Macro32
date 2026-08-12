#!/usr/bin/env python3
"""Cross-process guard for the single Codex Micro BLE writer."""

from __future__ import annotations

import fcntl
import os
from pathlib import Path
from typing import IO, Optional


DEFAULT_LOCK_PATH = (
    Path.home()
    / "Library"
    / "Application Support"
    / "CodexMicro"
    / "ble-writer.lock"
)


class ProcessLock:
    """Hold a non-blocking advisory lock until ``release`` is called."""

    def __init__(self, path: Path = DEFAULT_LOCK_PATH) -> None:
        self.path = path
        self._handle: Optional[IO[str]] = None

    def acquire(self) -> bool:
        if self._handle is not None:
            return True
        self.path.parent.mkdir(parents=True, exist_ok=True)
        handle = self.path.open("a+", encoding="utf-8")
        try:
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            handle.close()
            return False
        handle.seek(0)
        handle.truncate()
        handle.write(f"{os.getpid()}\n")
        handle.flush()
        self._handle = handle
        return True

    def release(self) -> None:
        handle = self._handle
        self._handle = None
        if handle is None:
            return
        try:
            fcntl.flock(handle.fileno(), fcntl.LOCK_UN)
        finally:
            handle.close()

    def __enter__(self) -> "ProcessLock":
        if not self.acquire():
            raise RuntimeError("Codex Micro BLE writer is already in use")
        return self

    def __exit__(self, *_: object) -> None:
        self.release()

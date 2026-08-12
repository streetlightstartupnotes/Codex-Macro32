#!/usr/bin/env python3
"""macOS default-input switching for the Codex Macro32 USB microphone."""

from __future__ import annotations

import ctypes
import ctypes.util
import sys
import threading
from dataclasses import dataclass
from typing import Any, Callable, Optional


TARGET_MIC_NAME = "Codex Macro32 Mic"


def _fourcc(value: str) -> int:
    return int.from_bytes(value.encode("ascii"), byteorder="big")


class _PropertyAddress(ctypes.Structure):
    _fields_ = [
        ("selector", ctypes.c_uint32),
        ("scope", ctypes.c_uint32),
        ("element", ctypes.c_uint32),
    ]


@dataclass(frozen=True)
class AudioInputDevice:
    object_id: int
    uid: str
    name: str


class CoreAudioInputBackend:
    """Small ctypes wrapper around the stable CoreAudio HAL C API."""

    _SYSTEM_OBJECT = 1
    _DEVICES = _fourcc("dev#")
    _DEFAULT_INPUT = _fourcc("dIn ")
    _NAME = _fourcc("lnam")
    _UID = _fourcc("uid ")
    _GLOBAL = _fourcc("glob")
    _MAIN = 0
    _UTF8 = 0x08000100

    def __init__(self) -> None:
        if sys.platform != "darwin":
            raise RuntimeError("CoreAudio input switching is only available on macOS")
        coreaudio_path = ctypes.util.find_library("CoreAudio")
        corefoundation_path = ctypes.util.find_library("CoreFoundation")
        if not coreaudio_path or not corefoundation_path:
            raise RuntimeError("macOS CoreAudio frameworks are unavailable")
        self._audio = ctypes.CDLL(coreaudio_path)
        self._cf = ctypes.CDLL(corefoundation_path)

        self._audio.AudioObjectGetPropertyDataSize.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(_PropertyAddress),
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self._audio.AudioObjectGetPropertyDataSize.restype = ctypes.c_int32
        self._audio.AudioObjectGetPropertyData.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(_PropertyAddress),
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_uint32),
            ctypes.c_void_p,
        ]
        self._audio.AudioObjectGetPropertyData.restype = ctypes.c_int32
        self._audio.AudioObjectSetPropertyData.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(_PropertyAddress),
            ctypes.c_uint32,
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_void_p,
        ]
        self._audio.AudioObjectSetPropertyData.restype = ctypes.c_int32

        self._cf.CFStringGetLength.argtypes = [ctypes.c_void_p]
        self._cf.CFStringGetLength.restype = ctypes.c_long
        self._cf.CFStringGetMaximumSizeForEncoding.argtypes = [
            ctypes.c_long,
            ctypes.c_uint32,
        ]
        self._cf.CFStringGetMaximumSizeForEncoding.restype = ctypes.c_long
        self._cf.CFStringGetCString.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_long,
            ctypes.c_uint32,
        ]
        self._cf.CFStringGetCString.restype = ctypes.c_bool
        self._cf.CFRelease.argtypes = [ctypes.c_void_p]
        self._cf.CFRelease.restype = None

    @staticmethod
    def _address(selector: int) -> _PropertyAddress:
        return _PropertyAddress(selector, CoreAudioInputBackend._GLOBAL,
                                CoreAudioInputBackend._MAIN)

    @staticmethod
    def _check(status: int, operation: str) -> None:
        if status != 0:
            raise RuntimeError(f"CoreAudio {operation} failed ({status})")

    def _cfstring_property(self, object_id: int, selector: int) -> str:
        address = self._address(selector)
        value = ctypes.c_void_p()
        size = ctypes.c_uint32(ctypes.sizeof(value))
        status = self._audio.AudioObjectGetPropertyData(
            object_id, ctypes.byref(address), 0, None,
            ctypes.byref(size), ctypes.byref(value)
        )
        self._check(status, "read string")
        if not value.value:
            return ""
        try:
            length = self._cf.CFStringGetLength(value)
            capacity = self._cf.CFStringGetMaximumSizeForEncoding(
                length, self._UTF8
            ) + 1
            buffer = ctypes.create_string_buffer(max(1, capacity))
            if not self._cf.CFStringGetCString(
                value, buffer, capacity, self._UTF8
            ):
                return ""
            return buffer.value.decode("utf-8", errors="replace")
        finally:
            self._cf.CFRelease(value)

    def devices(self) -> list[AudioInputDevice]:
        address = self._address(self._DEVICES)
        size = ctypes.c_uint32()
        status = self._audio.AudioObjectGetPropertyDataSize(
            self._SYSTEM_OBJECT, ctypes.byref(address), 0, None,
            ctypes.byref(size)
        )
        self._check(status, "list devices")
        count = size.value // ctypes.sizeof(ctypes.c_uint32)
        values = (ctypes.c_uint32 * count)()
        status = self._audio.AudioObjectGetPropertyData(
            self._SYSTEM_OBJECT, ctypes.byref(address), 0, None,
            ctypes.byref(size), values
        )
        self._check(status, "read devices")

        result: list[AudioInputDevice] = []
        for raw_id in values:
            object_id = int(raw_id)
            try:
                name = self._cfstring_property(object_id, self._NAME)
                uid = self._cfstring_property(object_id, self._UID)
            except RuntimeError:
                continue
            if name and uid:
                result.append(AudioInputDevice(object_id, uid, name))
        return result

    def default_input(self) -> Optional[AudioInputDevice]:
        address = self._address(self._DEFAULT_INPUT)
        object_id = ctypes.c_uint32()
        size = ctypes.c_uint32(ctypes.sizeof(object_id))
        status = self._audio.AudioObjectGetPropertyData(
            self._SYSTEM_OBJECT, ctypes.byref(address), 0, None,
            ctypes.byref(size), ctypes.byref(object_id)
        )
        self._check(status, "read default input")
        return next(
            (device for device in self.devices()
             if device.object_id == object_id.value),
            None,
        )

    def set_default_input(self, device: AudioInputDevice) -> None:
        address = self._address(self._DEFAULT_INPUT)
        object_id = ctypes.c_uint32(device.object_id)
        status = self._audio.AudioObjectSetPropertyData(
            self._SYSTEM_OBJECT, ctypes.byref(address), 0, None,
            ctypes.sizeof(object_id), ctypes.byref(object_id)
        )
        self._check(status, f"select input {device.name}")


AudioEventCallback = Callable[[str, dict[str, Any]], None]


class AutomaticInputSelector:
    """Select the USB mic on arrival and restore the prior input on removal."""

    def __init__(
        self,
        enabled: bool = True,
        callback: Optional[AudioEventCallback] = None,
        backend: Optional[CoreAudioInputBackend] = None,
        poll_seconds: float = 1.0,
    ) -> None:
        self._enabled = bool(enabled)
        self._callback = callback
        self._backend = backend
        self._poll_seconds = max(0.1, poll_seconds)
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._target_present = False
        self._owns_selection = False
        self._previous_uid: Optional[str] = None
        self._last_error = ""

    def start(self) -> None:
        if self._thread is not None and self._thread.is_alive():
            return
        self._thread = threading.Thread(
            target=self._run, name="codex-macro32-audio-input", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()

    def join(self, timeout: float = 3.0) -> None:
        if self._thread is not None:
            self._thread.join(timeout=timeout)

    def set_enabled(self, enabled: bool) -> None:
        with self._lock:
            changed = self._enabled != bool(enabled)
            self._enabled = bool(enabled)
            if changed and self._enabled:
                self._target_present = False
        if changed:
            self._wake.set()

    def _emit(self, state: str, detail: str) -> None:
        if self._callback is None:
            return
        try:
            self._callback("audio_input", {"state": state, "detail": detail})
        except Exception:
            pass

    def _restore(self, devices: list[AudioInputDevice]) -> None:
        if not self._owns_selection or not self._previous_uid:
            return
        previous = next(
            (device for device in devices if device.uid == self._previous_uid), None
        )
        if previous is not None:
            self._backend.set_default_input(previous)  # type: ignore[union-attr]
            self._emit("restored", f"已恢复麦克风：{previous.name}")

    def poll_once(self) -> None:
        if self._backend is None:
            self._backend = CoreAudioInputBackend()
        devices = self._backend.devices()
        current = self._backend.default_input()
        target = next(
            (device for device in devices if device.name == TARGET_MIC_NAME), None
        )
        with self._lock:
            enabled = self._enabled

        if not enabled:
            self._restore(devices)
            self._target_present = target is not None
            self._owns_selection = False
            self._previous_uid = None
            return

        if target is not None:
            if not self._target_present:
                if current is not None and current.uid != target.uid:
                    self._previous_uid = current.uid
                    self._backend.set_default_input(target)
                    self._owns_selection = True
                    self._emit("selected", f"已自动选择：{TARGET_MIC_NAME}")
                else:
                    self._owns_selection = False
                    self._previous_uid = None
            elif (
                self._owns_selection
                and current is not None
                and current.uid != target.uid
            ):
                # A manual choice made while connected always wins.
                self._owns_selection = False
                self._previous_uid = None
                self._emit("manual", f"已保留手动选择：{current.name}")
            self._target_present = True
            return

        if self._target_present:
            self._restore(devices)
            self._emit("removed", "Codex Macro32 Mic 已拔出")
        self._target_present = False
        self._owns_selection = False
        self._previous_uid = None

    def _run(self) -> None:
        if sys.platform != "darwin":
            return
        try:
            while not self._stop.is_set():
                try:
                    self.poll_once()
                    self._last_error = ""
                except Exception as error:
                    detail = str(error)
                    if detail != self._last_error:
                        self._last_error = detail
                        self._emit("error", f"自动选择麦克风失败：{detail}")
                self._wake.wait(self._poll_seconds)
                self._wake.clear()
        finally:
            try:
                if self._backend is not None:
                    self._restore(self._backend.devices())
            except Exception:
                pass

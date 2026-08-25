#!/usr/bin/env python3
"""Console-free Windows entry point for the USB system status bridge."""

from __future__ import annotations

import ctypes
import os
import sys
import traceback
from datetime import datetime
from pathlib import Path
from typing import TextIO


APP_NAME = "SmallDesktopDisplay Bridge"
MUTEX_NAME = "Local\\SmallDesktopDisplayBridge-9E5B3921"
ERROR_ALREADY_EXISTS = 183


def application_data_directory() -> Path:
    local_app_data = os.getenv("LOCALAPPDATA")
    base = Path(local_app_data) if local_app_data else Path.home() / "AppData" / "Local"
    return base / "SmallDesktopDisplay"


def open_log() -> tuple[TextIO, Path]:
    log_directory = application_data_directory() / "logs"
    log_directory.mkdir(parents=True, exist_ok=True)
    log_path = log_directory / "bridge.log"
    stream = log_path.open("a", encoding="utf-8", buffering=1)
    return stream, log_path


def show_message(message: str, *, error: bool = False) -> None:
    if os.name != "nt":
        return
    icon = 0x10 if error else 0x40  # MB_ICONERROR / MB_ICONINFORMATION
    ctypes.windll.user32.MessageBoxW(None, message, APP_NAME, icon | 0x00040000)


def acquire_single_instance_mutex() -> tuple[object | None, bool]:
    if os.name != "nt":
        return None, True

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
    kernel32.CreateMutexW.restype = ctypes.c_void_p
    handle = kernel32.CreateMutexW(None, False, MUTEX_NAME)
    if not handle:
        raise OSError(ctypes.get_last_error(), "CreateMutexW failed")
    if ctypes.get_last_error() == ERROR_ALREADY_EXISTS:
        kernel32.CloseHandle(handle)
        return None, False
    return handle, True


def release_mutex(handle: object | None) -> None:
    if os.name == "nt" and handle:
        ctypes.windll.kernel32.CloseHandle(handle)


def run() -> int:
    log_stream, log_path = open_log()
    sys.stdout = log_stream
    sys.stderr = log_stream
    mutex_handle: object | None = None

    try:
        mutex_handle, acquired = acquire_single_instance_mutex()
        if not acquired:
            show_message("系统状态屏桥接已经在后台运行。")
            return 0

        print()
        print(f"[{datetime.now().isoformat(timespec='seconds')}] starting Windows EXE bridge")
        print(f"log: {log_path}")

        from desktop_display_bridge import main as bridge_main

        return bridge_main(sys.argv[1:])
    except BaseException:
        traceback.print_exc(file=log_stream)
        show_message(f"桥接启动失败。\n\n请查看日志：\n{log_path}", error=True)
        return 1
    finally:
        release_mutex(mutex_handle)
        log_stream.flush()
        log_stream.close()


if __name__ == "__main__":
    raise SystemExit(run())

#!/usr/bin/env python3
"""Background macOS entry point for the USB system status bridge."""

from __future__ import annotations

import fcntl
import os
import platform
import sys
import traceback
from datetime import datetime
from pathlib import Path
from typing import IO, TextIO


APP_DIRECTORY_NAME = "SmallDesktopDisplay"
LOCK_FILE_NAME = "bridge.lock"


def application_support_directory() -> Path:
    return Path.home() / "Library" / "Application Support" / APP_DIRECTORY_NAME


def log_directory() -> Path:
    return Path.home() / "Library" / "Logs" / APP_DIRECTORY_NAME


def open_log() -> tuple[TextIO, Path]:
    directory = log_directory()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "bridge.log"
    return path.open("a", encoding="utf-8", buffering=1), path


def acquire_single_instance_lock() -> tuple[IO[str], bool]:
    directory = application_support_directory()
    directory.mkdir(parents=True, exist_ok=True)
    stream = (directory / LOCK_FILE_NAME).open("a+", encoding="utf-8")
    try:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        stream.close()
        return stream, False
    stream.seek(0)
    stream.truncate()
    stream.write(str(os.getpid()))
    stream.flush()
    return stream, True


def run() -> int:
    log_stream, log_path = open_log()
    sys.stdout = log_stream
    sys.stderr = log_stream
    lock_stream: IO[str] | None = None

    try:
        lock_stream, acquired = acquire_single_instance_lock()
        if not acquired:
            return 0

        print()
        print(f"[{datetime.now().isoformat(timespec='seconds')}] starting macOS app bridge")
        print(f"architecture: {platform.machine()}")
        print(f"log: {log_path}")

        from desktop_display_bridge import main as bridge_main

        return bridge_main(sys.argv[1:])
    except Exception:
        traceback.print_exc(file=log_stream)
        return 1
    finally:
        if lock_stream is not None and not lock_stream.closed:
            try:
                fcntl.flock(lock_stream.fileno(), fcntl.LOCK_UN)
            finally:
                lock_stream.close()
        log_stream.flush()
        log_stream.close()


if __name__ == "__main__":
    raise SystemExit(run())

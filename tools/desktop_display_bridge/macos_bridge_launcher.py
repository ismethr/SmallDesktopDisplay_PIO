#!/usr/bin/env python3
"""Background macOS entry point for the USB system status bridge."""

from __future__ import annotations

import fcntl
import io
import os
import platform
import sys
import threading
import traceback
from datetime import datetime
from pathlib import Path
from typing import IO, TextIO


APP_DIRECTORY_NAME = "SmallDesktopDisplay"
LOCK_FILE_NAME = "bridge.lock"
MAX_LOG_BYTES = 1_000_000
KEPT_LOGS = 3


def application_support_directory() -> Path:
    return Path.home() / "Library" / "Application Support" / APP_DIRECTORY_NAME


def log_directory() -> Path:
    return Path.home() / "Library" / "Logs" / APP_DIRECTORY_NAME


class RotatingLog(io.TextIOBase):
    """Line-oriented log stream shared by every worker thread.

    Rotates bridge.log past MAX_LOG_BYTES (keeping bridge.log.1 to .KEPT_LOGS)
    and collapses consecutive identical lines, such as a proxy outage logging
    the same request failure every refresh, into one "repeated" note.
    """

    def __init__(self, path: Path, max_bytes: int = MAX_LOG_BYTES, kept: int = KEPT_LOGS) -> None:
        super().__init__()
        self.path = path
        self.max_bytes = max_bytes
        self.kept = kept
        self._lock = threading.Lock()
        self._partial = ""
        self._last: str | None = None
        self._repeats = 0
        if path.exists() and path.stat().st_size >= max_bytes:
            self._rotate_files()
        self._stream = path.open("a", encoding="utf-8", buffering=1)

    def _rotate_files(self) -> None:
        for index in range(self.kept - 1, 0, -1):
            older = self.path.with_name(f"{self.path.name}.{index}")
            if older.exists():
                older.replace(self.path.with_name(f"{self.path.name}.{index + 1}"))
        self.path.replace(self.path.with_name(f"{self.path.name}.1"))

    def writable(self) -> bool:
        return True

    def write(self, text: str) -> int:
        with self._lock:
            self._partial += text
            *lines, self._partial = self._partial.split("\n")
            for line in lines:
                self._emit(line)
        return len(text)

    def _emit(self, line: str) -> None:
        if line.strip() and line == self._last:
            self._repeats += 1
            return
        self._write_repeats()
        self._stream.write(line + "\n")
        self._last = line
        if self._stream.tell() >= self.max_bytes:
            self._stream.close()
            self._rotate_files()
            self._stream = self.path.open("a", encoding="utf-8", buffering=1)

    def _write_repeats(self) -> None:
        if self._repeats:
            stamp = datetime.now().isoformat(timespec="seconds")
            self._stream.write(f"    (previous line repeated {self._repeats} more times, last {stamp})\n")
            self._repeats = 0

    def flush(self) -> None:
        with self._lock:
            if not self._stream.closed:
                self._stream.flush()

    def close(self) -> None:
        with self._lock:
            if not self._stream.closed:
                if self._partial:
                    self._emit(self._partial)
                    self._partial = ""
                self._write_repeats()
                self._stream.close()
        super().close()


def open_log() -> tuple[TextIO, Path]:
    directory = log_directory()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "bridge.log"
    return RotatingLog(path), path


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

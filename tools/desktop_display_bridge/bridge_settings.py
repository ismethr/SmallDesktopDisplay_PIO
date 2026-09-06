"""Validated, atomically persisted settings shared by HTTP and USB workers."""

from __future__ import annotations

import json
import os
import re
import secrets
import sys
import tempfile
import threading
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from typing import Any, Mapping


def default_settings_path() -> Path:
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support" / "SmallDesktopDisplay"
    elif sys.platform == "win32":
        base = Path(os.getenv("LOCALAPPDATA") or Path.home() / "AppData" / "Local") / "SmallDesktopDisplay"
    else:
        base = Path(os.getenv("XDG_CONFIG_HOME") or Path.home() / ".config") / "SmallDesktopDisplay"
    return base / "settings.json"


@dataclass(frozen=True)
class DisplaySettings:
    day_brightness: int = 50
    night_brightness: int = 10
    offline_brightness: int = 5
    night_start_hour: int = 0
    night_end_hour: int = 7
    serial_port: str | None = None

    def brightness(self, hour: int) -> tuple[int, int, bool]:
        start, end = self.night_start_hour, self.night_end_hour
        night = start != end and (start <= hour < end if start < end else hour >= start or hour < end)
        current = min(self.day_brightness, self.night_brightness) if night else self.day_brightness
        return current, min(current, self.offline_brightness), night


def validate_settings(values: Mapping[str, Any], base: DisplaySettings) -> DisplaySettings:
    if not isinstance(values, dict) or set(values) - set(asdict(base)):
        raise ValueError("设置包含未知字段")
    for name, value in values.items():
        if name == "serial_port":
            # Never permit the settings endpoint to open a file or arbitrary
            # device. USB ports may be disconnected when a profile is loaded.
            if value is not None and (not isinstance(value, str) or not re.fullmatch(
                r"(?:COM[1-9]\d*|/dev/(?:cu|tty)\.[A-Za-z0-9_-]+|/dev/tty(?:USB|ACM)\d+)", value
            )):
                raise ValueError("USB 端口格式无效")
        elif type(value) is not int or not 0 <= value <= (23 if name.endswith("_hour") else 100):
            raise ValueError("亮度必须为 0–100 的整数，小时必须为 0–23 的整数")
    return replace(base, **values)


class RevisionConflict(ValueError):
    pass


class SettingsState:
    def __init__(self, path: Path, defaults: DisplaySettings = DisplaySettings(),
                 overrides: dict[str, Any] | None = None) -> None:
        self.path = path
        self._lock = threading.Lock()
        self._value = defaults
        self._revision = 0
        self._connection_generation = 0
        self.token = secrets.token_urlsafe(32)
        self.load_error: str | None = None
        try:
            if path.stat().st_size > 8192:
                raise ValueError("settings file too large")
            document = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(document, dict) or type(document.get("schema")) is not int or document["schema"] != 1:
                raise ValueError("unknown settings schema")
            self._value = validate_settings(document["settings"], defaults)
        except FileNotFoundError:
            pass
        except (OSError, ValueError, TypeError, KeyError):
            self.load_error = "无法读取已保存的设置，正在使用启动配置；原文件未改动。"
        self._value = validate_settings(overrides or {}, self._value)

    def get(self) -> DisplaySettings:
        with self._lock:
            return self._value

    def connection(self) -> tuple[str | None, int]:
        with self._lock:
            return self._value.serial_port, self._connection_generation

    def public(self) -> dict[str, Any]:
        with self._lock:
            return {"ok": True, "settings": asdict(self._value), "revision": self._revision,
                    "token": self.token, "warning": self.load_error}

    def update(self, values: Mapping[str, Any], revision: int) -> None:
        with self._lock:
            if type(revision) is not int or revision != self._revision:
                raise RevisionConflict("设置已被其他窗口更改，请重新加载后再保存。")
            updated = validate_settings(values, self._value)
            self.path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            temporary: str | None = None
            try:
                with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=self.path.parent,
                                                 prefix=".settings-", delete=False) as stream:
                    temporary = stream.name
                    json.dump({"schema": 1, "settings": asdict(updated)}, stream, ensure_ascii=False, indent=2)
                    stream.write("\n")
                    stream.flush()
                    os.fsync(stream.fileno())
                os.replace(temporary, self.path)
                temporary = None
            finally:
                if temporary is not None:
                    Path(temporary).unlink(missing_ok=True)
            if updated.serial_port != self._value.serial_port:
                self._connection_generation += 1
            self._value = updated
            self._revision += 1
            self.load_error = None

    def reconnect(self) -> None:
        with self._lock:
            self._connection_generation += 1

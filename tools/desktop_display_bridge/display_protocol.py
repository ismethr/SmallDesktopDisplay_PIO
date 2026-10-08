"""USB frame encoders for the MiniDisplay status screen.

Mirrors ``mac_status_display/include/*_protocol.h``. Every frame is one line,
``$<ASCII payload>*HHHH\\n``, where HHHH is the CRC-16/CCITT-FALSE of the
payload. The firmware ignores frame types it does not know, so auxiliary
frames can be added without breaking older screens:

``MSD4``  system status; the only frame that keeps the status page alive
``MSC1``  local seconds since midnight
``MSC2``  local wall-clock time encoded as a UTC epoch (date for the clock)
``MSA1``  Claude 5-hour and weekly remaining quota
``MSA2``  Codex 5-hour and weekly remaining quota (MSD4 keeps the weekly value)
``MSW1``  cached weather JSON for the offline clock page
"""

from __future__ import annotations

import calendar
import math
import re
import time
from typing import Any, Mapping, Protocol

STATUS_FRAME_PREFIX = "MSD4"
MISSING_CODEX_USAGE = -1
MISSING_TEMPERATURE = -1
MISSING_NETWORK_LOCATION = "--"
MAX_WEATHER_PAYLOAD_BYTES = 900


class StatusSnapshot(Protocol):
    valid: bool
    sequence: int
    cpu_percent: float
    memory_percent: float
    cpu_temperature_celsius: float | None
    gpu_temperature_celsius: float | None
    codex_remaining_percent: int | None
    codex_usage_stale: bool
    download_bps: int
    upload_bps: int
    network_location: str | None
    network_location_stale: bool
    display_brightness_percent: int
    offline_brightness_percent: int


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE used by the desktop bridge and ESP8266."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def wrap_frame(payload: bytes) -> bytes:
    """Frame a payload for the USB line protocol."""
    return b"$" + payload + f"*{crc16_ccitt(payload):04X}\n".encode("ascii")


def bounded_tenths(value: float, low: int, high: int) -> int:
    if not math.isfinite(value):
        return low
    return max(low, min(high, int(round(value * 10.0))))


def _optional_tenths(value: float | None, high: int, missing: int) -> int:
    return missing if value is None else bounded_tenths(float(value), 0, high)


def encode_status_frame(snapshot: StatusSnapshot) -> bytes:
    """``$MSD4,seq,cpu10,mem10,cpu_temp10,gpu_temp10,codex10,codex_stale,down,up,location,location_stale,brightness,offline_brightness``"""
    if not snapshot.valid:
        raise ValueError("cannot encode an invalid desktop status snapshot")
    network_location = (snapshot.network_location or MISSING_NETWORK_LOCATION).upper()
    if re.fullmatch(r"[A-Z0-9?\-]{2,8}", network_location) is None:
        network_location = MISSING_NETWORK_LOCATION
    fields = (
        STATUS_FRAME_PREFIX,
        snapshot.sequence & 0xFFFF,
        bounded_tenths(snapshot.cpu_percent, 0, 1000),
        bounded_tenths(snapshot.memory_percent, 0, 1000),
        _optional_tenths(snapshot.cpu_temperature_celsius, 1500, MISSING_TEMPERATURE),
        _optional_tenths(snapshot.gpu_temperature_celsius, 1500, MISSING_TEMPERATURE),
        _optional_tenths(snapshot.codex_remaining_percent, 1000, MISSING_CODEX_USAGE),
        int(bool(snapshot.codex_usage_stale)),
        max(0, min(0xFFFFFFFF, int(snapshot.download_bps))),
        max(0, min(0xFFFFFFFF, int(snapshot.upload_bps))),
        network_location,
        int(bool(snapshot.network_location_stale)),
        max(0, min(100, int(snapshot.display_brightness_percent))),
        max(0, min(100, int(snapshot.offline_brightness_percent))),
    )
    return wrap_frame(",".join(map(str, fields)).encode("ascii"))


def encode_clock_frame(now: float | None = None) -> bytes:
    """Independent clock packet: old MSD3/MSD4 firmware safely ignores it."""
    local = time.localtime(time.time() if now is None else now)
    seconds = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec
    return wrap_frame(f"MSC1,{seconds}".encode("ascii"))


def encode_calendar_frame(now: float | None = None) -> bytes:
    """Local date and time, encoded as if the local wall clock were UTC."""
    local = time.localtime(time.time() if now is None else now)
    return wrap_frame(f"MSC2,{calendar.timegm(local)}".encode("ascii"))


def encode_claude_frame(usage: Mapping[str, Any]) -> bytes:
    """Remaining tenths of the 5-hour and weekly windows, -1 when unknown."""
    def remaining(key: str) -> int:
        window = usage.get(key)
        return round(window["remaining_percent"] * 10) if window else -1

    payload = f'MSA1,{remaining("five_hour")},{remaining("seven_day")},{int(bool(usage["stale"]))}'
    return wrap_frame(payload.encode("ascii"))


def encode_codex_frame(usage: Any) -> bytes:
    """Codex 5-hour and weekly remaining tenths from a ``codex.UsageSnapshot``."""
    def tenths(value: int | None) -> int:
        return MISSING_CODEX_USAGE if value is None or not usage.valid else bounded_tenths(value, 0, 1000)

    payload = (f"MSA2,{tenths(usage.session_remaining_percent)},{tenths(usage.remaining_percent)},"
               f"{int(bool(usage.stale or not usage.valid))}")
    return wrap_frame(payload.encode("ascii"))


def encode_weather_frame(payload: bytes) -> bytes:
    """Frame a ``MSW1,{json}`` payload from :class:`weather_cache.WeatherCache`."""
    if not payload.startswith(b"MSW1,") or len(payload) > len(b"MSW1,") + MAX_WEATHER_PAYLOAD_BYTES:
        raise ValueError("invalid weather payload")
    return wrap_frame(payload)

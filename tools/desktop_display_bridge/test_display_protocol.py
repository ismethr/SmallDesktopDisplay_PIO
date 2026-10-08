#!/usr/bin/env python3
"""Golden vectors for the USB frames parsed by mac_status_display."""

from __future__ import annotations

import calendar
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import display_protocol as protocol  # noqa: E402


def split(frame: bytes) -> tuple[bytes, int]:
    assert frame.startswith(b"$") and frame.endswith(b"\n")
    payload, checksum = frame[1:-1].split(b"*")
    return payload, int(checksum, 16)


class DisplayProtocolTests(unittest.TestCase):
    def test_every_frame_uses_the_crc_envelope(self) -> None:
        frame = protocol.wrap_frame(b"MSQ1")
        payload, checksum = split(frame)
        self.assertEqual(b"MSQ1", payload)
        self.assertEqual(protocol.crc16_ccitt(b"MSQ1"), checksum)
        self.assertRegex(frame.decode("ascii"), r"^\$MSQ1\*[0-9A-F]{4}\n$")

    def test_calendar_frame_encodes_local_wall_clock(self) -> None:
        now = time.mktime((2026, 9, 30, 20, 39, 7, 0, 0, -1))
        payload, checksum = split(protocol.encode_calendar_frame(now))
        self.assertEqual(f"MSC2,{calendar.timegm((2026, 9, 30, 20, 39, 7, 0, 0, 0))}".encode(), payload)
        self.assertEqual(b"MSC2,1790800747", payload)
        self.assertEqual(protocol.crc16_ccitt(payload), checksum)

    def test_claude_frame_uses_tenths_and_missing_sentinel(self) -> None:
        usage = {"five_hour": {"remaining_percent": 73.04}, "seven_day": None, "stale": True}
        self.assertEqual(b"MSA1,730,-1,1", split(protocol.encode_claude_frame(usage))[0])

    def test_codex_frame_carries_session_and_weekly_windows(self) -> None:
        from types import SimpleNamespace

        usage = SimpleNamespace(valid=True, stale=False, session_remaining_percent=58, remaining_percent=17)
        self.assertEqual(b"MSA2,580,170,0", split(protocol.encode_codex_frame(usage))[0])
        no_session = SimpleNamespace(valid=True, stale=True, session_remaining_percent=None, remaining_percent=100)
        self.assertEqual(b"MSA2,-1,1000,1", split(protocol.encode_codex_frame(no_session))[0])
        # Values held from an earlier refresh are never sent once invalid.
        invalid = SimpleNamespace(valid=False, stale=False, session_remaining_percent=58, remaining_percent=17)
        self.assertEqual(b"MSA2,-1,-1,1", split(protocol.encode_codex_frame(invalid))[0])

    def test_weather_frame_rejects_foreign_or_oversized_payloads(self) -> None:
        self.assertEqual(b'MSW1,{"city":"x"}', split(protocol.encode_weather_frame(b'MSW1,{"city":"x"}'))[0])
        for invalid in (b"MSD4,1", b"MSW1," + b"x" * (protocol.MAX_WEATHER_PAYLOAD_BYTES + 1)):
            with self.subTest(invalid=invalid[:8]), self.assertRaises(ValueError):
                protocol.encode_weather_frame(invalid)


if __name__ == "__main__":
    unittest.main()

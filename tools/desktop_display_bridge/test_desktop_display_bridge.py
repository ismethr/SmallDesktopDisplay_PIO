from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

import desktop_display_bridge as bridge


class Counter:
    def __init__(self, received: int, sent: int) -> None:
        self.bytes_recv = received
        self.bytes_sent = sent


class DesktopDisplayBridgeTests(unittest.TestCase):
    def test_crc_matches_standard_check_value(self) -> None:
        self.assertEqual(0x29B1, bridge.crc16_ccitt(b"123456789"))

    def test_status_frame_is_bounded_versioned_and_checksummed(self) -> None:
        snapshot = bridge.MacStatusSnapshot(
            valid=True,
            sequence=65537,
            cpu_percent=12.34,
            memory_percent=99.96,
            cpu_temperature_c=52.25,
            download_bps=123456,
            upload_bps=-1,
        )
        frame = bridge.encode_status_frame(snapshot).decode("ascii")
        payload, checksum = frame[1:].strip().split("*")
        self.assertEqual("MSD1,1,123,1000,522,123456,0", payload)
        self.assertEqual(bridge.crc16_ccitt(payload.encode("ascii")), int(checksum, 16))

    def test_status_frame_uses_temperature_sentinel(self) -> None:
        snapshot = bridge.MacStatusSnapshot(valid=True, cpu_temperature_c=None)
        self.assertIn(f",{bridge.MISSING_TEMPERATURE},", bridge.encode_status_frame(snapshot).decode())

    def test_invalid_snapshot_cannot_be_sent(self) -> None:
        with self.assertRaises(ValueError):
            bridge.encode_status_frame(bridge.MacStatusSnapshot())

    def test_parse_route_interface(self) -> None:
        output = "route to: default\ninterface: en0\nflags: <UP,GATEWAY>\n"
        self.assertEqual("en0", bridge.parse_default_route_interface(output))
        self.assertIsNone(bridge.parse_default_route_interface("interface missing"))

    def test_interface_override_and_fallback(self) -> None:
        counters = {"lo0": Counter(9999, 9999), "en0": Counter(100, 50)}
        with mock.patch.object(bridge, "default_route_interface", return_value=None):
            self.assertEqual("en0", bridge.choose_network_interface(counters, None))
        self.assertEqual("en0", bridge.choose_network_interface(counters, "en0"))
        self.assertIsNone(bridge.choose_network_interface(counters, "missing"))

    def test_macmon_temperature_parser_rejects_bad_values(self) -> None:
        self.assertEqual(54.25, bridge._parse_macmon_temperature('{"temp":{"cpu_temp_avg":54.25}}'))
        for line in (
            "not-json",
            "{}",
            '{"temp":{"cpu_temp_avg":true}}',
            '{"temp":{"cpu_temp_avg":999}}',
        ):
            with self.subTest(line=line):
                self.assertIsNone(bridge._parse_macmon_temperature(line))

    def test_serial_discovery_prefers_explicit_existing_port(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            port = Path(directory) / "usbserial-test"
            port.touch()
            self.assertEqual((str(port), None), bridge.discover_serial_port(str(port)))

    def test_serial_discovery_refuses_ambiguous_devices(self) -> None:
        with mock.patch.object(bridge.glob, "glob", side_effect=[["/dev/cu.usbserial-a"], ["/dev/cu.usbserial-b"], []]):
            port, error = bridge.discover_serial_port(None)
        self.assertIsNone(port)
        self.assertIn("multiple", error or "")

    def test_public_usb_state_hides_full_local_path(self) -> None:
        payload = bridge.UsbSnapshot(connected=True, port="/dev/cu.usbserial-2140").as_public_dict()
        self.assertEqual("cu.usbserial-2140", payload["port"])
        self.assertNotIn("/dev/", str(payload))


if __name__ == "__main__":
    unittest.main()

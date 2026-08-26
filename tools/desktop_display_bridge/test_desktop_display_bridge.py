from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import desktop_display_bridge as bridge
import macos_bridge_launcher


class Counter:
    def __init__(self, received: int, sent: int) -> None:
        self.bytes_recv = received
        self.bytes_sent = sent


class Port:
    def __init__(
        self,
        device: str,
        *,
        vid: int | None = None,
        description: str = "",
        hwid: str = "",
    ) -> None:
        self.device = device
        self.vid = vid
        self.description = description
        self.hwid = hwid
        self.manufacturer = ""
        self.product = ""


class DesktopDisplayBridgeTests(unittest.TestCase):
    def test_crc_matches_standard_check_value(self) -> None:
        self.assertEqual(0x29B1, bridge.crc16_ccitt(b"123456789"))

    def test_status_frame_is_bounded_versioned_and_checksummed(self) -> None:
        snapshot = bridge.MacStatusSnapshot(
            valid=True,
            sequence=65537,
            cpu_percent=12.34,
            memory_percent=99.96,
            codex_remaining_percent=52,
            codex_usage_stale=True,
            download_bps=123456,
            upload_bps=-1,
            display_brightness_percent=10,
            offline_brightness_percent=5,
        )
        frame = bridge.encode_status_frame(snapshot).decode("ascii")
        payload, checksum = frame[1:].strip().split("*")
        self.assertEqual("MSD3,1,123,1000,520,1,123456,0,10,5", payload)
        self.assertEqual(bridge.crc16_ccitt(payload.encode("ascii")), int(checksum, 16))

    def test_status_frame_uses_codex_usage_sentinel(self) -> None:
        snapshot = bridge.MacStatusSnapshot(valid=True, codex_remaining_percent=None)
        self.assertIn(f",{bridge.MISSING_CODEX_USAGE},0,", bridge.encode_status_frame(snapshot).decode())

    def test_invalid_snapshot_cannot_be_sent(self) -> None:
        with self.assertRaises(ValueError):
            bridge.encode_status_frame(bridge.MacStatusSnapshot())

    def test_parse_route_interface(self) -> None:
        output = "route to: default\ninterface: en0\nflags: <UP,GATEWAY>\n"
        self.assertEqual("en0", bridge.parse_default_route_interface(output))
        self.assertIsNone(bridge.parse_default_route_interface("interface missing"))

    def test_parse_windows_route_interface(self) -> None:
        self.assertEqual("Wi-Fi", bridge.parse_windows_default_route_interface("\r\nWi-Fi\r\n"))
        self.assertIsNone(bridge.parse_windows_default_route_interface("\r\n"))

    def test_windows_default_route_uses_powershell_result(self) -> None:
        completed = SimpleNamespace(returncode=0, stdout="Ethernet\r\n")
        with mock.patch.object(
            bridge,
            "default_route_interface_from_socket",
            return_value=None,
        ), mock.patch.object(bridge.subprocess, "run", return_value=completed) as run:
            self.assertEqual("Ethernet", bridge.default_route_interface(platform_name="win32"))
        self.assertEqual("powershell.exe", run.call_args.args[0][0])

    def test_default_route_maps_operating_system_source_address(self) -> None:
        probe = mock.Mock()
        probe.getsockname.return_value = ("192.168.1.30", 54321)
        addresses = {
            "Ethernet": [SimpleNamespace(family=bridge.socket.AF_INET, address="192.168.1.30")]
        }
        fake_psutil = SimpleNamespace(net_if_addrs=mock.Mock(return_value=addresses))
        with mock.patch.object(bridge.socket, "socket", return_value=probe), mock.patch.object(
            bridge,
            "psutil",
            fake_psutil,
        ):
            self.assertEqual("Ethernet", bridge.default_route_interface_from_socket())
        probe.connect.assert_called_once_with(("1.1.1.1", 9))
        probe.close.assert_called_once()

    def test_night_window_crosses_midnight(self) -> None:
        self.assertTrue(bridge.is_night_hour(0, 23, 7))
        self.assertTrue(bridge.is_night_hour(23, 23, 7))
        self.assertFalse(bridge.is_night_hour(12, 23, 7))
        self.assertFalse(bridge.is_night_hour(0, 0, 0))

    def test_default_midnight_window(self) -> None:
        self.assertTrue(bridge.is_night_hour(0, 0, 7))
        self.assertTrue(bridge.is_night_hour(6, 0, 7))
        self.assertFalse(bridge.is_night_hour(7, 0, 7))

    def test_interface_override_and_fallback(self) -> None:
        counters = {"lo0": Counter(9999, 9999), "en0": Counter(100, 50)}
        with mock.patch.object(bridge, "default_route_interface", return_value=None):
            self.assertEqual("en0", bridge.choose_network_interface(counters, None))
        self.assertEqual("en0", bridge.choose_network_interface(counters, "en0"))
        self.assertIsNone(bridge.choose_network_interface(counters, "missing"))

    def test_windows_loopback_is_not_selected_as_fallback(self) -> None:
        counters = {
            "Loopback Pseudo-Interface 1": Counter(9999, 9999),
            "Wi-Fi": Counter(100, 50),
        }
        with mock.patch.object(bridge, "default_route_interface", return_value=None):
            self.assertEqual("Wi-Fi", bridge.choose_network_interface(counters, None))

    def test_serial_discovery_prefers_explicit_existing_port(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            port = Path(directory) / "usbserial-test"
            port.touch()
            self.assertEqual((str(port), None), bridge.discover_serial_port(str(port)))

    def test_windows_serial_discovery_accepts_explicit_com_port(self) -> None:
        with mock.patch.object(bridge, "_enumerated_serial_ports", return_value=[]):
            self.assertEqual(("COM7", None), bridge.discover_serial_port("COM7", "win32"))

    def test_windows_serial_discovery_finds_one_usb_adapter(self) -> None:
        ports = [
            ("COM1", False),
            ("COM7", True),
        ]
        with mock.patch.object(bridge, "_enumerated_serial_ports", return_value=ports):
            self.assertEqual(("COM7", None), bridge.discover_serial_port(None, "win32"))

    def test_serial_discovery_refuses_ambiguous_devices(self) -> None:
        with mock.patch.object(bridge, "_enumerated_serial_ports", return_value=[]), mock.patch.object(
            bridge.glob,
            "glob",
            side_effect=[["/dev/cu.usbserial-a"], ["/dev/cu.usbserial-b"], []],
        ):
            port, error = bridge.discover_serial_port(None, "darwin")
        self.assertIsNone(port)
        self.assertIn("multiple", error or "")

    def test_serial_enumeration_recognizes_common_windows_usb_adapter(self) -> None:
        records = [
            Port("COM1", description="Communications Port"),
            Port("COM7", vid=0x1A86, description="USB-SERIAL CH340"),
        ]
        fake_list_ports = SimpleNamespace(comports=mock.Mock(return_value=records))
        with mock.patch.object(bridge, "serial_list_ports", fake_list_ports):
            self.assertEqual([("COM1", False), ("COM7", True)], bridge._enumerated_serial_ports())

    def test_public_usb_state_hides_full_local_path(self) -> None:
        payload = bridge.UsbSnapshot(connected=True, port="/dev/cu.usbserial-2140").as_public_dict()
        self.assertEqual("cu.usbserial-2140", payload["port"])
        self.assertNotIn("/dev/", str(payload))

    def test_macos_launcher_uses_standard_user_directories(self) -> None:
        with mock.patch.object(Path, "home", return_value=Path("/Users/tester")):
            self.assertEqual(
                Path("/Users/tester/Library/Application Support/SmallDesktopDisplay"),
                macos_bridge_launcher.application_support_directory(),
            )
            self.assertEqual(
                Path("/Users/tester/Library/Logs/SmallDesktopDisplay"),
                macos_bridge_launcher.log_directory(),
            )

    def test_macos_launcher_lock_rejects_second_instance(self) -> None:
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(
            macos_bridge_launcher,
            "application_support_directory",
            return_value=Path(directory),
        ):
            first, first_acquired = macos_bridge_launcher.acquire_single_instance_lock()
            second, second_acquired = macos_bridge_launcher.acquire_single_instance_lock()
            self.assertTrue(first_acquired)
            self.assertFalse(second_acquired)
            self.assertTrue(second.closed)
            first.close()


if __name__ == "__main__":
    unittest.main()

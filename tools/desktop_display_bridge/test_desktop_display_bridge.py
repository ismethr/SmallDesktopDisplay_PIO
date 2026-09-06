from __future__ import annotations

import os
import json
import http.client
import tempfile
import threading
import unittest
import urllib.request
import urllib.error
from pathlib import Path
from contextlib import contextmanager
from types import SimpleNamespace
from unittest import mock

import desktop_display_bridge as bridge

if os.name != "nt":
    import macos_bridge_launcher
else:  # pragma: no cover - the macOS launcher imports fcntl
    macos_bridge_launcher = None  # type: ignore[assignment]


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


class SimulatedStop:
    """Advance the worker clock without sleeping or touching a physical port."""

    def __init__(self, end: float) -> None:
        self.now = 0.0
        self.end = end

    def is_set(self) -> bool:
        return self.now >= self.end

    def wait(self, seconds: float) -> bool:
        self.now += seconds
        return self.is_set()


class DesktopDisplayBridgeTests(unittest.TestCase):
    def test_expired_system_sample_is_invalid_and_new_sample_recovers(self) -> None:
        with mock.patch.object(bridge.time, "monotonic", return_value=10) as clock:
            state = bridge.DesktopStatusState()
            state.set(bridge.DesktopStatusSnapshot(valid=True, cpu_percent=42))
            clock.return_value = 26
            self.assertFalse(state.get().valid)
            self.assertEqual(42, state.get().cpu_percent)
            state.set(bridge.DesktopStatusSnapshot(valid=True, cpu_percent=23))
            self.assertTrue(state.get().valid)
            state.set_error("sampling failed")
            self.assertFalse(state.get().valid)

    def test_sampler_retries_after_driver_error(self) -> None:
        state = bridge.DesktopStatusState()
        state.set(bridge.DesktopStatusSnapshot(valid=True))
        stop = SimulatedStop(5)
        with mock.patch.object(bridge, "_sample_desktop_status_loop", side_effect=[OSError(), None]) as sample:
            bridge.sample_desktop_status_loop(
                state, bridge.codex.UsageState(), bridge.TemperatureState(),
                bridge.NetworkLocationState(), stop,
            )
        self.assertEqual(2, sample.call_count)
        self.assertFalse(state.get().valid)

    def run_serial_scenario(self, port: mock.Mock, *, end: float = 5) -> tuple[list, SimulatedStop]:
        stop = SimulatedStop(end)
        updates = []
        usb = bridge.UsbState()
        with mock.patch.object(bridge.time, "monotonic", side_effect=lambda: stop.now):
            state = bridge.DesktopStatusState()
            state.set(bridge.DesktopStatusSnapshot(valid=True, sequence=1))
            backend = SimpleNamespace(Serial=mock.Mock(return_value=port), SerialException=OSError)
            with mock.patch.object(bridge, "serial", backend), mock.patch.object(
                bridge, "discover_serial_port", return_value=("COM7", None)
            ), mock.patch.object(usb, "set", side_effect=updates.append):
                bridge.serial_writer_loop(state, usb, stop, "COM7", 115200)
            backend.Serial.assert_called_with(port=None, baudrate=115200, timeout=0.2, write_timeout=1.0)
        return updates, stop

    def test_serial_keepalive_stops_when_sample_expires(self) -> None:
        port = mock.Mock()
        port.write.side_effect = lambda frame: len(frame)
        updates, stop = self.run_serial_scenario(port, end=18)
        self.assertGreater(port.write.call_count, 5)
        self.assertLess(port.write.call_count, 16)
        self.assertIn("waiting-data", [u.phase for u in updates])
        self.assertTrue(any(u.connected and u.phase == "streaming" for u in updates))
        port.flush.assert_not_called()
        port.close.assert_called_once()
        self.assertFalse(port.dtr)
        self.assertFalse(port.rts)

    def test_serial_partial_write_reconnects_and_never_reports_streaming(self) -> None:
        port = mock.Mock()
        port.write.return_value = 1
        updates, _ = self.run_serial_scenario(port, end=7)
        self.assertTrue(any(u.phase == "retrying" for u in updates))
        self.assertFalse(any(u.connected for u in updates))
        self.assertGreaterEqual(port.open.call_count, 2)
        self.assertEqual(port.open.call_count, port.close.call_count)

    def test_serial_setup_failure_closes_descriptor(self) -> None:
        port = mock.Mock()
        port.reset_input_buffer.side_effect = OSError("device removed")
        updates, _ = self.run_serial_scenario(port)
        self.assertEqual(port.open.call_count, port.close.call_count)
        self.assertFalse(any(u.connected for u in updates))
        port.write.assert_not_called()

    def test_stop_during_device_boot_closes_without_writing(self) -> None:
        port = mock.Mock()
        self.run_serial_scenario(port, end=1)
        port.close.assert_called_once()
        port.write.assert_not_called()

    def test_dashboard_serves_local_assets_and_sanitized_overview(self) -> None:
        handler = bridge.make_handler(bridge.codex.UsageState(), bridge.DesktopStatusState(), bridge.UsbState())
        server = bridge.codex.BridgeHTTPServer(("127.0.0.1", 0), handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            with urllib.request.urlopen(base + "/") as response:
                self.assertIn("MiniDisplay Bridge", response.read().decode())
                self.assertIn("frame-ancestors 'none'", response.headers["Content-Security-Policy"])
            with urllib.request.urlopen(base + "/icon.png") as response:
                self.assertTrue(response.read().startswith(b"\x89PNG"))
            with urllib.request.urlopen(base + "/v1/overview") as response:
                payload = json.load(response)
                self.assertFalse(payload["desktop"]["ok"])
                self.assertEqual("waiting", payload["usb"]["phase"])
                self.assertNotIn("access_token", str(payload))
            with self.assertRaises(urllib.error.HTTPError) as failure:
                urllib.request.urlopen(base + "/../desktop_display_bridge.py")
            self.assertEqual(404, failure.exception.code)
            failure.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)

    def test_crc_matches_standard_check_value(self) -> None:
        self.assertEqual(0x29B1, bridge.crc16_ccitt(b"123456789"))

    def test_status_frame_is_bounded_versioned_and_checksummed(self) -> None:
        snapshot = bridge.MacStatusSnapshot(
            valid=True,
            sequence=65537,
            cpu_percent=12.34,
            memory_percent=99.96,
            cpu_temperature_celsius=49.04,
            gpu_temperature_celsius=61.06,
            codex_remaining_percent=52,
            codex_usage_stale=True,
            download_bps=123456,
            upload_bps=-1,
            network_location="CN-SH",
            network_location_stale=True,
            display_brightness_percent=10,
            offline_brightness_percent=5,
        )
        frame = bridge.encode_status_frame(snapshot).decode("ascii")
        payload, checksum = frame[1:].strip().split("*")
        self.assertEqual(
            "MSD4,1,123,1000,490,611,520,1,123456,0,CN-SH,1,10,5",
            payload,
        )
        self.assertEqual(bridge.crc16_ccitt(payload.encode("ascii")), int(checksum, 16))

    def test_status_frame_uses_codex_usage_sentinel(self) -> None:
        snapshot = bridge.MacStatusSnapshot(valid=True, codex_remaining_percent=None)
        self.assertIn(f",{bridge.MISSING_CODEX_USAGE},0,", bridge.encode_status_frame(snapshot).decode())

    def test_status_frame_uses_temperature_sentinels(self) -> None:
        snapshot = bridge.MacStatusSnapshot(valid=True)
        self.assertIn(
            f",{bridge.MISSING_TEMPERATURE},{bridge.MISSING_TEMPERATURE},",
            bridge.encode_status_frame(snapshot).decode(),
        )

    def test_smc_output_selects_hottest_cpu_and_gpu_sensor(self) -> None:
        output = """
[TC0D]     43.0
[TC0C]     38.0
[TC3C]     49.0
[TG0D]     61.0
[TA0P]     24.0
[broken]   99.0
"""
        values = bridge.parse_smc_temperature_output(output)
        self.assertEqual((49.0, 61.0), bridge.select_component_temperatures(values))

    def test_smc_output_filters_invalid_temperatures(self) -> None:
        values = bridge.parse_smc_temperature_output(
            "[TC0D] 0.0\n[TC1C] 126.0\n[TG0D] nan\n[TG0P] 55.5\n"
        )
        self.assertEqual({"TG0P": 55.5}, values)

    def test_temperature_reader_reports_partial_sensor_availability(self) -> None:
        completed = SimpleNamespace(returncode=0, stdout="[TC0D] 44.0\n", stderr="")
        with mock.patch.object(
            bridge,
            "macos_temperature_commands",
            return_value=[(("/tmp/smc-reader",), "test-smc")],
        ), mock.patch.object(bridge.subprocess, "run", return_value=completed):
            snapshot = bridge.read_hardware_temperatures(platform_name="darwin")
        self.assertEqual(44.0, snapshot.cpu_celsius)
        self.assertIsNone(snapshot.gpu_celsius)
        self.assertEqual("GPU temperature unavailable", snapshot.error)
        self.assertEqual("test-smc", snapshot.source)

    def test_ipwho_location_prefers_region_code(self) -> None:
        snapshot = bridge.parse_ipwho_network_location(
            {
                "success": True,
                "country_code": "cn",
                "region_code": "sh",
                "city": "Shanghai",
            }
        )
        self.assertEqual("CN-SH", snapshot.label)
        self.assertEqual("ipwho.is", snapshot.source)
        self.assertFalse(snapshot.stale)

    def test_ipwho_location_falls_back_to_city_initials(self) -> None:
        snapshot = bridge.parse_ipwho_network_location(
            {
                "country_code": "US",
                "region_code": "",
                "city": "Los Angeles",
            }
        )
        self.assertEqual("US-LA", snapshot.label)
        self.assertEqual("ZU", bridge.network_location_initials("Zürich"))

    def test_network_location_keeps_last_success_as_stale(self) -> None:
        previous = bridge.NetworkLocationSnapshot(label="JP-TK", source="ipwho.is")
        failed = bridge.NetworkLocationSnapshot(error="offline", sampled_at=123)
        merged = bridge.merge_network_location(previous, failed)
        self.assertEqual("JP-TK", merged.label)
        self.assertTrue(merged.stale)
        self.assertEqual("offline", merged.error)
        self.assertEqual(123, merged.sampled_at)

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
        if macos_bridge_launcher is None:
            self.skipTest("macOS launcher is not importable on Windows")
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
        if macos_bridge_launcher is None:
            self.skipTest("macOS launcher is not importable on Windows")
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


class DisplaySettingsTests(unittest.TestCase):
    @contextmanager
    def settings_server(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = bridge.SettingsState(Path(directory) / "settings.json")
            handler = bridge.make_handler(bridge.codex.UsageState(), bridge.DesktopStatusState(), bridge.UsbState(), settings)
            server = bridge.codex.BridgeHTTPServer(("127.0.0.1", 0), handler)
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                yield settings, server.server_port
            finally:
                server.shutdown()
                server.server_close()
                worker.join(timeout=2)

    def request(self, port, method="GET", path="/v1/settings", body=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
        request_headers = {"Content-Type": "application/json", **(headers or {})}
        try:
            connection.request(method, path, json.dumps(body) if body is not None else None, request_headers)
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def test_settings_round_trip_and_cli_override(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            settings = bridge.SettingsState(path)
            self.assertFalse(path.exists())
            settings.update({"day_brightness": 35, "serial_port": "COM7"}, 0)
            restored = bridge.SettingsState(path)
            self.assertEqual(35, restored.get().day_brightness)
            self.assertEqual("COM7", restored.get().serial_port)
            self.assertEqual(70, bridge.SettingsState(path, overrides={"day_brightness": 70}).get().day_brightness)
            self.assertEqual(35, bridge.SettingsState(path).get().day_brightness)
            self.assertNotEqual(settings.token, restored.token)

    def test_invalid_settings_do_not_partially_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = bridge.SettingsState(Path(directory) / "settings.json")
            for invalid in ({"day_brightness": 101}, {"night_brightness": True}, {"day_brightness": 3.2},
                            {"night_start_hour": 24}, {"serial_port": "/etc/passwd"}, {"serial_port": "file.txt"},
                            {"unknown": 1}, {"day_brightness": 15, "offline_brightness": -1}):
                with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                    settings.update(invalid, 0)
                self.assertEqual(bridge.DisplaySettings(), settings.get())
                self.assertFalse(settings.path.exists())

    def test_atomic_save_failure_preserves_memory_and_disk(self):
        import bridge_settings
        with tempfile.TemporaryDirectory() as directory:
            settings = bridge.SettingsState(Path(directory) / "settings.json")
            settings.update({"day_brightness": 42}, 0)
            before = settings.path.read_bytes()
            with mock.patch.object(bridge_settings.os, "replace", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    settings.update({"day_brightness": 12}, 1)
            self.assertEqual(42, settings.get().day_brightness)
            self.assertEqual(before, settings.path.read_bytes())
            self.assertEqual([settings.path], list(settings.path.parent.iterdir()))

    def test_corrupt_profile_is_preserved_and_defaults_used(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            path.write_text('{"schema":1,"settings":{"day_brightness":999}}', encoding="utf-8")
            before = path.read_bytes()
            settings = bridge.SettingsState(path)
            self.assertIsNotNone(settings.load_error)
            self.assertEqual(50, settings.get().day_brightness)
            self.assertEqual(before, path.read_bytes())

    def test_brightness_caps_and_midnight_boundaries(self):
        settings = bridge.DisplaySettings(day_brightness=8, night_brightness=10, offline_brightness=20,
                                          night_start_hour=22, night_end_hour=7)
        for hour in (0, 6, 22, 23):
            self.assertEqual((8, 8, True), settings.brightness(hour))
        for hour in (7, 12, 21):
            self.assertEqual((8, 8, False), settings.brightness(hour))
        self.assertEqual((0, 0, True), bridge.DisplaySettings(day_brightness=0).brightness(1))
        self.assertEqual((50, 5, False), bridge.DisplaySettings(night_start_hour=7, night_end_hour=7).brightness(7))

    def test_http_rejects_missing_token_foreign_origin_and_rebinding(self):
        with self.settings_server() as (settings, port):
            body = {"revision": 0, "settings": {"day_brightness": 33}}
            for headers in ({}, {"X-MiniDisplay-Token": "é"},
                            {"X-MiniDisplay-Token": settings.token, "Origin": "https://example.com"},
                            {"X-MiniDisplay-Token": settings.token, "Origin": "null"},
                            {"X-MiniDisplay-Token": settings.token, "Host": f"example.com:{port}"}):
                self.assertEqual(403, self.request(port, "POST", body=body, headers=headers)[0])
            self.assertEqual(403, self.request(port, headers={"Host": f"example.com:{port}"})[0])
            self.assertFalse(settings.path.exists())

    def test_http_save_revision_conflict_and_reconnect(self):
        with self.settings_server() as (settings, port):
            code, payload = self.request(port)
            self.assertEqual(200, code)
            headers = {"X-MiniDisplay-Token": payload["token"], "Origin": f"http://127.0.0.1:{port}"}
            body = {"revision": payload["revision"], "settings": {"day_brightness": 27}}
            self.assertEqual(200, self.request(port, "POST", body=body, headers=headers)[0])
            self.assertEqual(27, bridge.SettingsState(settings.path).get().day_brightness)
            self.assertEqual(409, self.request(port, "POST", body=body, headers=headers)[0])
            generation = settings.connection()[1]
            self.assertEqual(200, self.request(port, "POST", "/v1/reconnect", {}, headers)[0])
            self.assertEqual(generation + 1, settings.connection()[1])
            self.assertEqual(1, settings.public()["revision"])

    def test_http_rejects_unknown_ports_and_invalid_bodies(self):
        with self.settings_server() as (settings, port):
            headers = {"X-MiniDisplay-Token": settings.token}
            with mock.patch.object(bridge, "_enumerated_serial_ports", return_value=[("COM7", True)]), mock.patch.object(
                bridge, "discover_serial_port", return_value=("COM7", None)
            ):
                self.assertEqual(["COM7"], self.request(port)[1]["ports"])
                for body in ([1], {"revision": 0, "settings": {"serial_port": "COM8"}},
                             {"revision": 0, "settings": {"serial_port": "/etc/passwd"}}, {"settings": {}}):
                    self.assertEqual(400, self.request(port, "POST", body=body, headers=headers)[0])
                self.assertEqual(400, self.request(port, "POST", body={}, headers={**headers, "Content-Type": "text/plain"})[0])
                self.assertEqual(400, self.request(port, "POST", body={}, headers={**headers, "Content-Length": "9000"})[0])
            self.assertFalse(settings.path.exists())

    def test_serial_live_port_change_and_manual_reconnect(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = bridge.SettingsState(Path(directory) / "settings.json", bridge.DisplaySettings(serial_port="COM7"))
            stop = SimulatedStop(12)
            port = mock.Mock()
            writes = []
            def write(frame):
                writes.append(port.port)
                if len(writes) == 1:
                    settings.update({"serial_port": "COM8"}, 0)
                elif len(writes) == 2:
                    settings.reconnect()
                return len(frame)
            port.write.side_effect = write
            backend = SimpleNamespace(Serial=mock.Mock(return_value=port), SerialException=OSError)
            with mock.patch.object(bridge.time, "monotonic", side_effect=lambda: stop.now), mock.patch.object(
                bridge, "serial", backend
            ), mock.patch.object(bridge, "discover_serial_port", side_effect=lambda chosen: (chosen, None)):
                state = bridge.DesktopStatusState()
                state.set(bridge.DesktopStatusSnapshot(valid=True))
                bridge.serial_writer_loop(state, bridge.UsbState(), stop, None, 115200, runtime_settings=settings)
            self.assertEqual("COM7", writes[0])
            self.assertTrue(all(value == "COM8" for value in writes[1:]))
            self.assertEqual(3, port.open.call_count)
            self.assertEqual(3, port.close.call_count)

    def test_sampler_applies_new_brightness_without_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = bridge.SettingsState(Path(directory) / "settings.json")
            stop = SimulatedStop(3)
            snapshots = []
            def capture(snapshot):
                snapshots.append(snapshot)
                if len(snapshots) == 1:
                    settings.update({"day_brightness": 6, "night_brightness": 20}, 0)
            state = bridge.DesktopStatusState()
            backend = SimpleNamespace(cpu_percent=lambda **kw: 21, net_io_counters=lambda **kw: {},
                                      virtual_memory=lambda: SimpleNamespace(percent=42))
            with mock.patch.object(bridge, "psutil", backend), mock.patch.object(state, "set", side_effect=capture), mock.patch.object(
                bridge.time, "localtime", return_value=SimpleNamespace(tm_hour=1)
            ), mock.patch.object(bridge, "choose_network_interface", return_value=None):
                bridge._sample_desktop_status_loop(state, bridge.codex.UsageState(), bridge.TemperatureState(),
                    bridge.NetworkLocationState(), stop, 1, None, 0, 7, 50, 10, 5, settings)
            self.assertEqual([10, 6], [s.display_brightness_percent for s in snapshots])


if __name__ == "__main__":
    unittest.main()

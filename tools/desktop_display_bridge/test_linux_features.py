from __future__ import annotations

import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import desktop_display_bridge as bridge
import linux_temperature


def sensor(current: float, label: str = "") -> SimpleNamespace:
    return SimpleNamespace(label=label, current=current)


PROC_NET_ROUTE = """Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT
wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0
enp8s0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0
enp8s0\t0001A8C0\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0
"""


class LinuxRouteTests(unittest.TestCase):
    def test_lowest_metric_default_route_wins(self) -> None:
        self.assertEqual("enp8s0", bridge.parse_linux_default_route_interface(PROC_NET_ROUTE))

    def test_no_default_route(self) -> None:
        header = PROC_NET_ROUTE.splitlines()[0]
        self.assertIsNone(bridge.parse_linux_default_route_interface(header + "\n"))

    def test_default_route_falls_back_to_socket_probe(self) -> None:
        with mock.patch.object(bridge, "LINUX_ROUTE_TABLE", Path("/nonexistent/route")), mock.patch.object(
            bridge, "default_route_interface_from_socket", return_value="eth0"
        ):
            self.assertEqual("eth0", bridge.default_route_interface(platform_name="linux"))


class LinuxTemperatureTests(unittest.TestCase):
    def test_prefers_package_sensor_chip_and_hottest_value(self) -> None:
        sensors = {
            "acpitz": [sensor(27.8)],
            "coretemp": [sensor(31.0, "Package id 0"), sensor(45.0, "Core 4")],
            "amdgpu": [sensor(40.0, "edge"), sensor(48.0, "junction")],
            "nvme": [sensor(60.0)],
        }
        self.assertEqual((45.0, "coretemp", 48.0, "amdgpu"), linux_temperature.select_linux_temperatures(sensors))

    def test_invalid_readings_are_ignored(self) -> None:
        sensors = {"k10temp": [sensor(0.0), sensor(float("nan"))], "acpitz": [sensor(33.0)]}
        cpu, chip, gpu, _ = linux_temperature.select_linux_temperatures(sensors)
        self.assertEqual((33.0, "acpitz", None), (cpu, chip, gpu))

    def test_nvidia_smi_fallback_and_partial_error(self) -> None:
        fake_psutil = SimpleNamespace(sensors_temperatures=lambda: {"k10temp": [sensor(52.0, "Tctl")]})
        with mock.patch.object(linux_temperature, "psutil", fake_psutil), mock.patch.object(
            linux_temperature, "read_nvidia_smi", return_value=None
        ):
            self.assertEqual(
                (52.0, None, "linux-k10temp", "GPU temperature unavailable"),
                linux_temperature.read_temperatures(),
            )
        self.assertEqual(66.0, linux_temperature.parse_nvidia_smi_output("61\n66\n[N/A]\n"))

    def test_bridge_dispatches_linux_reader(self) -> None:
        with mock.patch.object(linux_temperature, "read_temperatures", return_value=(40.0, 50.0, "linux-x", None)):
            snapshot = bridge.read_hardware_temperatures(platform_name="linux")
        self.assertEqual((40.0, 50.0, "linux-x", None), (
            snapshot.cpu_celsius, snapshot.gpu_celsius, snapshot.source, snapshot.error))

    def test_linux_serial_discovery_finds_ch340(self) -> None:
        with mock.patch.object(bridge, "_enumerated_serial_ports", return_value=[("/dev/ttyUSB0", True)]), \
                mock.patch.object(bridge.glob, "glob", return_value=[]):
            self.assertEqual(("/dev/ttyUSB0", None), bridge.discover_serial_port(None, "linux"))


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import json
import os
import socket
import tempfile
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import desktop_display_bridge as bridge
import windows_bridge_launcher as launcher
import windows_temperature as temperature


class WindowsTemperatureTests(unittest.TestCase):
    def test_only_real_cpu_gpu_sensors_are_selected(self):
        rows = [
            dict(Parent='/intelcpu/0', Value=48, Provider='LibreHardwareMonitor'),
            dict(Parent='/intelcpu/0', Value=62, Provider='LibreHardwareMonitor'),
            dict(Parent='/gpu-amd/0', Value=67, Provider='LibreHardwareMonitor'),
            dict(Parent='/lpc/nct6798/0', Name='CPU', Value=95),
            dict(Parent='/intelcpu/0', Value=None),
            dict(Parent='/intelcpu/0', Value='nan'),
            dict(Parent='/intelcpu/0', Value=True),
            dict(Parent='/gpu-amd/0', Value=200),
        ]
        self.assertEqual((62, 67, {'LibreHardwareMonitor'}), temperature.parse_sensors(json.dumps(rows)))

    def test_single_wmi_row_and_malformed_result(self):
        self.assertEqual((50, None, {'hardware-monitor'}), temperature.parse_sensors(
            '{"Parent":"/amdcpu/0","Value":50}'))
        with self.assertRaises(ValueError):
            temperature.parse_sensors('null')

    def test_intel_thermal_headroom_and_gpu_memory_are_not_core_temperatures(self):
        tree = {'Children': [
            {'SensorId': '/intelcpu/0/temperature/14', 'Text': 'CPU Package', 'RawValue': '39 °C'},
            {'SensorId': '/intelcpu/0/temperature/22', 'Text': 'P-Core #8 Distance to TjMax', 'RawValue': '69 °C'},
            {'SensorId': '/gpu-amd/0/temperature/0', 'Text': 'GPU Core', 'RawValue': '52 °C'},
            {'SensorId': '/gpu-amd/0/temperature/1', 'Text': 'GPU Memory', 'RawValue': '62 °C'},
            {'SensorId': '/gpu-amd/0/temperature/7', 'Text': 'GPU Hot Spot', 'RawValue': '54 °C'},
        ]}
        self.assertEqual((39, 54, {'LibreHardwareMonitor HTTP'}), temperature.parse_http_sensors(tree))

    def test_http_tree_handles_fahrenheit_locale_and_wrong_sensor_type(self):
        tree = {'Children': [{'Children': [
            {'SensorId': '/intelcpu/0/temperature/0', 'RawValue': '50,5 °C'},
            {'SensorId': '/gpu-amd/0/temperature/0', 'Value': '149 °F'},
            {'SensorId': '/intelcpu/0/load/0', 'RawValue': '99 %'},
        ]}]}
        self.assertEqual((50.5, 65, {'LibreHardwareMonitor HTTP'}), temperature.parse_http_sensors(tree))

    def test_windows_dispatch_preserves_partial_data_and_diagnostic(self):
        with mock.patch.object(temperature, 'read_temperatures', return_value=(50, None, 'test', 'GPU unavailable')):
            snapshot = bridge.read_hardware_temperatures(platform_name='win32')
        self.assertEqual(50, snapshot.cpu_celsius)
        self.assertIsNone(snapshot.gpu_celsius)
        self.assertEqual('GPU unavailable', snapshot.error)

    def test_nvidia_fallback_survives_wmi_failure(self):
        gpu_result = SimpleNamespace(returncode=0, stdout='N/A\n57\n61\n')
        with mock.patch.object(temperature.urllib.request, 'build_opener') as opener, \
             mock.patch.object(temperature.shutil, 'which', return_value='nvidia-smi'), \
             mock.patch.object(temperature.subprocess, 'run', side_effect=[OSError(), gpu_result]) as run:
            opener.return_value.open.side_effect = OSError()
            cpu, gpu, source, error = temperature.read_temperatures()
        self.assertIsNone(cpu)
        self.assertEqual(61, gpu)
        self.assertEqual('nvidia-smi', source)
        self.assertIn('CPU', error)
        self.assertIn('--query-gpu=temperature.gpu', run.call_args.args[0])

    def test_provider_disappearance_does_not_reuse_old_temperature(self):
        with mock.patch.object(temperature.urllib.request, 'build_opener') as opener, \
             mock.patch.object(temperature.shutil, 'which', return_value=None), \
             mock.patch.object(temperature.subprocess, 'run', return_value=SimpleNamespace(stdout='[]')):
            opener.return_value.open.side_effect = OSError()
            cpu, gpu, source, error = temperature.read_temperatures()
        self.assertIsNone(cpu)
        self.assertIsNone(gpu)
        self.assertIsNone(source)
        self.assertIn('PawnIO', error)


class LifecycleAndClockTests(unittest.TestCase):
    def test_clock_frame_has_local_time_and_valid_crc(self):
        with mock.patch.object(bridge.time, 'localtime', return_value=SimpleNamespace(tm_hour=23, tm_min=59, tm_sec=58)):
            frame = bridge.encode_clock_frame(0)
        payload, crc = frame[1:].strip().split(b'*')
        self.assertEqual(b'MSC1,86398', payload)
        self.assertEqual(bridge.crc16_ccitt(payload), int(crc, 16))

    def test_controlled_shutdown_closes_http_port(self):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        stopped = threading.Event()
        with tempfile.TemporaryDirectory() as directory, \
             mock.patch.object(bridge.codex, 'refresh_loop'), \
             mock.patch.object(bridge, 'temperature_refresh_loop'), \
             mock.patch.object(bridge, 'sample_desktop_status_loop'):
            result = bridge.main(['--listen-port', str(port), '--no-usb', '--no-network-location',
                                  '--settings-file', str(Path(directory) / 'settings.json')],
                                 stop_event=stopped, on_ready=lambda actual: stopped.set())
        self.assertEqual(0, result)
        with socket.socket() as probe:
            self.assertNotEqual(0, probe.connect_ex(('127.0.0.1', port)))

    @unittest.skipUnless(os.name == 'nt', 'Windows handle API')
    def test_real_mutex_can_be_closed_and_reacquired(self):
        with mock.patch.object(launcher, 'MUTEX_NAME', f'Local\\MiniDisplay-test-{os.getpid()}'):
            first, acquired = launcher.acquire_single_instance_mutex()
            self.assertTrue(acquired)
            try:
                second, acquired = launcher.acquire_single_instance_mutex()
                self.assertFalse(acquired)
                self.assertIsNone(second)
            finally:
                launcher.release_mutex(first)
            third, acquired = launcher.acquire_single_instance_mutex()
            self.assertTrue(acquired)
            launcher.release_mutex(third)

    def test_log_failure_is_reported_and_stdio_restored(self):
        original = launcher.sys.stderr
        with mock.patch.object(launcher, 'acquire_single_instance_mutex', return_value=(None, True)), \
             mock.patch.object(launcher, 'open_log', side_effect=PermissionError()), \
             mock.patch.object(launcher, 'show_message') as message:
            self.assertEqual(1, launcher.run(['--background']))
            message.assert_called_once()
        self.assertIs(original, launcher.sys.stderr)

    def test_duplicate_background_start_does_not_touch_logs(self):
        with mock.patch.object(launcher, 'acquire_single_instance_mutex', return_value=(None, False)), \
             mock.patch.object(launcher, 'open_log') as log, \
             mock.patch.object(launcher.webbrowser, 'open') as browser:
            self.assertEqual(0, launcher.run(['--background']))
        log.assert_not_called()
        browser.assert_not_called()

    def test_log_rollover_preserves_previous_run(self):
        with tempfile.TemporaryDirectory() as directory, \
             mock.patch.object(launcher, 'application_data_directory', return_value=Path(directory)):
            path = Path(directory) / 'logs/bridge.log'
            path.parent.mkdir()
            path.write_text('x' * (2 * 1024 * 1024))
            stream, _ = launcher.open_log()
            stream.close()
            self.assertEqual(0, path.stat().st_size)
            self.assertEqual(2 * 1024 * 1024, path.with_name('bridge.previous.log').stat().st_size)


if __name__ == '__main__':
    unittest.main()

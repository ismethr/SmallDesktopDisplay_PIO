"""Read published Windows hardware sensors without installing a driver."""

from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import urllib.request
from pathlib import Path
from typing import Any


# LibreHardwareMonitor and OpenHardwareMonitor expose the same WMI schema.
# Only CPU/GPU hardware identifiers qualify: an ACPI zone or motherboard
# sensor named "CPU" is not necessarily a CPU core/package temperature.
SENSOR_QUERY = r"""
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$readings = @(
  foreach ($provider in @('LibreHardwareMonitor', 'OpenHardwareMonitor')) {
    Get-CimInstance -Namespace ('root/' + $provider) -ClassName Sensor `
      -Filter "SensorType='Temperature'" -ErrorAction SilentlyContinue |
      Select-Object Parent, Name, Value, @{Name='Provider';Expression={$provider}}
  }
)
ConvertTo-Json -InputObject $readings -Compress
"""


def valid_temperature(value: Any) -> float | None:
    if isinstance(value, bool) or value is None:
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) and 1 <= number <= 125 else None


def parse_sensors(output: str) -> tuple[float | None, float | None, set[str]]:
    rows = json.loads(output.lstrip('\ufeff'))
    if isinstance(rows, dict):
        rows = [rows]
    if not isinstance(rows, list):
        raise ValueError('invalid sensor response')
    cpu, gpu, sources = [], [], set()
    for row in rows:
        if not isinstance(row, dict):
            continue
        value = valid_temperature(row.get('Value'))
        parent = str(row.get('Parent', '')).lower()
        name = str(row.get('Name', '')).lower()
        if value is None:
            continue
        # Intel also publishes "Distance to TjMax" as SensorType=Temperature.
        # That is thermal headroom, not a measured temperature: max() would
        # otherwise report colder cores as hotter than the package.
        if any(word in name for word in ('distance', 'tjmax', 'tjunction max', 'critical', 'limit', 'margin')):
            continue
        if parent.startswith(('/intelcpu/', '/amdcpu/')):
            cpu.append(value)
        elif parent.startswith(('/gpu-nvidia/', '/gpu-amd/', '/gpu-intel/', '/atigpu/', '/nvidiagpu/')):
            if any(word in name for word in ('memory', 'vrm', 'vr ', 'vdd')):
                continue
            gpu.append(value)
        else:
            continue
        sources.add(str(row.get('Provider') or 'hardware-monitor'))
    return max(cpu, default=None), max(gpu, default=None), sources


def parse_http_sensors(document: Any) -> tuple[float | None, float | None, set[str]]:
    """LHM 0.9.6 replaced WMI with a sensor tree on its local web server."""
    pending = [document]
    rows = []
    while pending:
        node = pending.pop()
        if not isinstance(node, dict):
            continue
        pending.extend(node.get('Children') or [])
        identifier = str(node.get('SensorId', ''))
        if '/temperature/' not in identifier:
            continue
        raw = str(node.get('RawValue', node.get('Value', ''))).strip()
        # Formatted temperatures can use a locale-specific decimal separator.
        parts = raw.replace(',', '.').split()
        try:
            value = float(parts[0])
        except (ValueError, IndexError):
            continue
        if '°F' in raw:
            value = (value - 32) * 5 / 9
        value = valid_temperature(value)
        if value is None:
            continue
        rows.append(dict(Parent=identifier, Name=node.get('Text', ''), Value=value,
                         Provider='LibreHardwareMonitor HTTP'))
    return parse_sensors(json.dumps(rows))


def read_temperatures(timeout: float = 6) -> tuple[float | None, float | None, str | None, str | None]:
    cpu = gpu = None
    sources: set[str] = set()
    try:
        # Explicitly bypass proxy environment variables for this loopback-only
        # endpoint. Bound both response size and latency; no remote sensors.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open('http://127.0.0.1:18765/sensors', timeout=2) as response:
            data = response.read(2 * 1024 * 1024 + 1)
            if len(data) > 2 * 1024 * 1024:
                raise ValueError('sensor response too large')
            cpu, gpu, sources = parse_sensors(data.decode('utf-8'))
    except (OSError, ValueError):
        pass
    if cpu is not None and gpu is not None:
        return cpu, gpu, ' + '.join(sorted(sources)), None
    options = dict(capture_output=True, text=True, encoding='utf-8', errors='replace',
                   timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    try:
        result = subprocess.run([str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive',
                                 '-Command', SENSOR_QUERY], **options)
        # A missing second namespace can set a nonzero exit code even when the
        # first provider returned valid sensors. Parse the actual JSON response.
        wmi_cpu, wmi_gpu, wmi_sources = parse_sensors(result.stdout)
        cpu = cpu if cpu is not None else wmi_cpu
        gpu = gpu if gpu is not None else wmi_gpu
        sources.update(wmi_sources)
    except (OSError, subprocess.SubprocessError, ValueError):
        pass
    if gpu is None:
        nvidia = shutil.which('nvidia-smi')
        if nvidia:
            try:
                result = subprocess.run([nvidia, '--query-gpu=temperature.gpu',
                                         '--format=csv,noheader,nounits'], **options)
                if result.returncode == 0:
                    values = [valid_temperature(line.strip()) for line in result.stdout.splitlines()]
                    gpu = max((value for value in values if value is not None), default=None)
                    if gpu is not None:
                        sources.add('nvidia-smi')
            except (OSError, subprocess.SubprocessError):
                pass
    missing = [name for name, value in [('CPU', cpu), ('GPU', gpu)] if value is None]
    error = (' / '.join(missing) + ' 温度不可用：请从托盘启动温度采集；首次使用需安装官方 PawnIO 驱动。'
             if missing else None)
    return cpu, gpu, ' + '.join(sorted(sources)) or None, error

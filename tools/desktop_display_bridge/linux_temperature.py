"""Read-only Linux CPU/GPU temperatures from hwmon (via psutil) and nvidia-smi."""

from __future__ import annotations

import math
import shutil
import subprocess
from typing import Any, Iterable, Mapping

try:
    import psutil  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - the bridge startup guard reports this
    psutil = None  # type: ignore[assignment]

# hwmon driver names in preference order. The first chip that reports a valid
# reading wins, so a real package sensor beats the generic ACPI thermal zone.
CPU_CHIPS = ("coretemp", "k10temp", "zenpower", "cpu_thermal", "soc_thermal", "x86_pkg_temp", "acpitz")
GPU_CHIPS = ("amdgpu", "radeon", "nouveau", "xe", "i915")
NVIDIA_SMI_COMMAND = (
    "nvidia-smi",
    "--query-gpu=temperature.gpu",
    "--format=csv,noheader,nounits",
)


def _valid(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) and 1.0 <= number <= 125.0 else None


def _hottest(entries: Iterable[Any]) -> float | None:
    values = [value for value in (_valid(getattr(entry, "current", None)) for entry in entries) if value is not None]
    return max(values) if values else None


def select_linux_temperatures(
    sensors: Mapping[str, Iterable[Any]],
) -> tuple[float | None, str | None, float | None, str | None]:
    """Return (cpu, cpu_chip, gpu, gpu_chip) from psutil.sensors_temperatures()."""
    cpu = cpu_chip = gpu = gpu_chip = None
    for chip in CPU_CHIPS:
        value = _hottest(sensors.get(chip, ()))
        if value is not None:
            cpu, cpu_chip = value, chip
            break
    for chip in GPU_CHIPS:
        value = _hottest(sensors.get(chip, ()))
        if value is not None:
            gpu, gpu_chip = value, chip
            break
    return cpu, cpu_chip, gpu, gpu_chip


def parse_nvidia_smi_output(output: str) -> float | None:
    values = [value for value in (_valid(line.strip()) for line in output.splitlines()) if value is not None]
    return max(values) if values else None


def read_nvidia_smi(timeout: float) -> float | None:
    if shutil.which(NVIDIA_SMI_COMMAND[0]) is None:
        return None
    try:
        result = subprocess.run(NVIDIA_SMI_COMMAND, check=False, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None
    return parse_nvidia_smi_output(result.stdout) if result.returncode == 0 else None


def read_temperatures(timeout: float = 4.0) -> tuple[float | None, float | None, str | None, str | None]:
    """Return (cpu, gpu, source, error) in the same shape as windows_temperature."""
    sensors: Mapping[str, Iterable[Any]] = {}
    if psutil is not None and hasattr(psutil, "sensors_temperatures"):
        try:
            sensors = psutil.sensors_temperatures()
        except (OSError, RuntimeError):
            sensors = {}
    cpu, cpu_chip, gpu, gpu_chip = select_linux_temperatures(sensors)
    if gpu is None:
        gpu = read_nvidia_smi(timeout)
        gpu_chip = "nvidia-smi" if gpu is not None else None
    if cpu is None and gpu is None:
        return None, None, None, "no Linux hwmon CPU or GPU temperature sensors"
    source = "linux-" + "+".join(chip for chip in (cpu_chip, gpu_chip) if chip)
    missing = [name for name, value in (("CPU", cpu), ("GPU", gpu)) if value is None]
    return cpu, gpu, source, f"{' and '.join(missing)} temperature unavailable" if missing else None

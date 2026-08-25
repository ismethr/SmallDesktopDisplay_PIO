#!/usr/bin/env python3
"""Run the Codex usage endpoint and cross-platform USB system monitor."""

from __future__ import annotations

import argparse
import glob
import json
import math
import os
import platform
import re
import socket
import subprocess
import sys
import threading
import time
from dataclasses import asdict, dataclass, replace
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from typing import Any, Mapping, Protocol

try:
    import psutil  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - exercised by the startup guard
    psutil = None  # type: ignore[assignment]

try:
    import serial  # type: ignore[import-not-found]
    from serial.tools import list_ports as serial_list_ports  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - exercised by the startup guard
    serial = None  # type: ignore[assignment]
    serial_list_ports = None  # type: ignore[assignment]


TOOLS_DIR = Path(__file__).resolve().parents[1]
CODEX_BRIDGE_DIR = TOOLS_DIR / "codex_usage_bridge"
if str(CODEX_BRIDGE_DIR) not in sys.path:
    sys.path.insert(0, str(CODEX_BRIDGE_DIR))

import codex_usage_bridge as codex  # noqa: E402


DEFAULT_SERIAL_BAUD = 115200
DEFAULT_SAMPLE_SECONDS = 1.0
DEFAULT_RECONNECT_SECONDS = 2.0
DEFAULT_NIGHT_START_HOUR = 0
DEFAULT_NIGHT_END_HOUR = 7
DEFAULT_DAY_BRIGHTNESS = 50
DEFAULT_NIGHT_BRIGHTNESS = 10
DEFAULT_OFFLINE_BRIGHTNESS = 5
FRAME_PREFIX = "MSD3"
MISSING_CODEX_USAGE = -1
MACOS_ROUTE_COMMAND = ("/sbin/route", "-n", "get", "default")
WINDOWS_ROUTE_COMMAND = (
    "powershell.exe",
    "-NoLogo",
    "-NoProfile",
    "-NonInteractive",
    "-Command",
    "Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' "
    "-ErrorAction SilentlyContinue | Where-Object State -EQ Alive | "
    "Sort-Object RouteMetric | Select-Object -First 1 -ExpandProperty InterfaceAlias",
)
HOST_PLATFORM = platform.system() or sys.platform


class NetCounters(Protocol):
    bytes_recv: int
    bytes_sent: int


@dataclass(frozen=True)
class DesktopStatusSnapshot:
    schema: int = 2
    valid: bool = False
    sequence: int = 0
    cpu_percent: float = 0.0
    memory_percent: float = 0.0
    codex_remaining_percent: int | None = None
    codex_usage_stale: bool = False
    download_bps: int = 0
    upload_bps: int = 0
    display_brightness_percent: int = DEFAULT_DAY_BRIGHTNESS
    offline_brightness_percent: int = DEFAULT_OFFLINE_BRIGHTNESS
    night_mode: bool = False
    interface: str | None = None
    sampled_at: int | None = None
    error: str | None = "waiting for first sample"
    host_platform: str = HOST_PLATFORM

    def as_public_dict(self) -> dict[str, Any]:
        payload = asdict(self)
        payload["ok"] = payload.pop("valid")
        return payload


class DesktopStatusState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = DesktopStatusSnapshot()

    def get(self) -> DesktopStatusSnapshot:
        with self._lock:
            return self._snapshot

    def set(self, snapshot: DesktopStatusSnapshot) -> None:
        with self._lock:
            self._snapshot = snapshot

    def set_error(self, message: str) -> None:
        with self._lock:
            if self._snapshot.valid:
                self._snapshot = replace(self._snapshot, error=message)
            else:
                self._snapshot = DesktopStatusSnapshot(error=message)


# Compatibility aliases for code importing the names used by the original
# macOS-only bridge. The HTTP and serial compatibility surfaces are preserved too.
MacStatusSnapshot = DesktopStatusSnapshot
MacStatusState = DesktopStatusState


@dataclass(frozen=True)
class UsbSnapshot:
    connected: bool = False
    port: str | None = None
    last_sent_at: int | None = None
    error: str | None = "waiting for USB display"

    def as_public_dict(self) -> dict[str, Any]:
        return {
            "connected": self.connected,
            "port": Path(self.port).name if self.port else None,
            "last_sent_at": self.last_sent_at,
            "error": self.error,
        }


class UsbState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = UsbSnapshot()

    def get(self) -> UsbSnapshot:
        with self._lock:
            return self._snapshot

    def set(self, snapshot: UsbSnapshot) -> None:
        with self._lock:
            self._snapshot = snapshot


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE used by the desktop bridge and ESP8266."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _bounded_tenths(value: float, low: int, high: int) -> int:
    if not math.isfinite(value):
        return low
    return max(low, min(high, int(round(value * 10.0))))


def encode_status_frame(snapshot: DesktopStatusSnapshot) -> bytes:
    if not snapshot.valid:
        raise ValueError("cannot encode an invalid desktop status snapshot")
    codex_remaining = (
        MISSING_CODEX_USAGE
        if snapshot.codex_remaining_percent is None
        else _bounded_tenths(float(snapshot.codex_remaining_percent), 0, 1000)
    )
    payload = ",".join(
        (
            FRAME_PREFIX,
            str(snapshot.sequence & 0xFFFF),
            str(_bounded_tenths(snapshot.cpu_percent, 0, 1000)),
            str(_bounded_tenths(snapshot.memory_percent, 0, 1000)),
            str(codex_remaining),
            "1" if snapshot.codex_usage_stale else "0",
            str(max(0, min(0xFFFFFFFF, int(snapshot.download_bps)))),
            str(max(0, min(0xFFFFFFFF, int(snapshot.upload_bps)))),
            str(max(0, min(100, int(snapshot.display_brightness_percent)))),
            str(max(0, min(100, int(snapshot.offline_brightness_percent)))),
        )
    )
    checksum = crc16_ccitt(payload.encode("ascii"))
    return f"${payload}*{checksum:04X}\n".encode("ascii")


def parse_default_route_interface(output: str) -> str | None:
    match = re.search(r"^\s*interface:\s*(\S+)\s*$", output, flags=re.MULTILINE)
    return match.group(1) if match else None


def parse_windows_default_route_interface(output: str) -> str | None:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    return lines[0] if lines else None


def default_route_interface_from_socket() -> str | None:
    """Map the source address selected by the OS routing table back to an interface."""
    if psutil is None:
        return None
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.settimeout(1.0)
        probe.connect(("1.1.1.1", 9))
        local_address = probe.getsockname()[0]
        interfaces = psutil.net_if_addrs()
    except (OSError, AttributeError):
        return None
    finally:
        probe.close()
    for name, addresses in interfaces.items():
        if any(address.family == socket.AF_INET and address.address == local_address for address in addresses):
            return name
    return None


def default_route_interface(
    timeout: float = 2.0,
    platform_name: str | None = None,
) -> str | None:
    platform_name = sys.platform if platform_name is None else platform_name
    if platform_name == "darwin":
        command = MACOS_ROUTE_COMMAND
        parser = parse_default_route_interface
    elif platform_name == "win32":
        routed = default_route_interface_from_socket()
        if routed is not None:
            return routed
        command = WINDOWS_ROUTE_COMMAND
        parser = parse_windows_default_route_interface
    else:
        return None

    run_options: dict[str, Any] = {
        "check": False,
        "capture_output": True,
        "text": True,
        "timeout": timeout,
    }
    if platform_name == "win32" and hasattr(subprocess, "CREATE_NO_WINDOW"):
        run_options["creationflags"] = subprocess.CREATE_NO_WINDOW
    try:
        result = subprocess.run(command, **run_options)
    except (OSError, subprocess.SubprocessError):
        return None
    return parser(result.stdout) if result.returncode == 0 else None


def _is_loopback_interface(name: str) -> bool:
    normalized = name.casefold()
    return normalized in {"lo", "lo0"} or "loopback" in normalized


def choose_network_interface(counters: Mapping[str, NetCounters], override: str | None) -> str | None:
    if override:
        return override if override in counters else None
    routed = default_route_interface()
    if routed in counters:
        return routed
    if "en0" in counters:
        return "en0"
    candidates = ((name, item) for name, item in counters.items() if not _is_loopback_interface(name))
    return max(candidates, key=lambda pair: pair[1].bytes_recv + pair[1].bytes_sent, default=(None, None))[0]


def is_night_hour(hour: int, start_hour: int, end_hour: int) -> bool:
    """Return whether an hour is inside a possibly midnight-crossing window."""
    if start_hour == end_hour:
        return False
    if start_hour < end_hour:
        return start_hour <= hour < end_hour
    return hour >= start_hour or hour < end_hour


def sample_desktop_status_loop(
    state: DesktopStatusState,
    usage_state: codex.UsageState,
    stop_event: threading.Event,
    interval: float,
    network_override: str | None,
    night_start_hour: int,
    night_end_hour: int,
    day_brightness: int,
    night_brightness: int,
    offline_brightness: int,
) -> None:
    assert psutil is not None
    psutil.cpu_percent(interval=None)
    interface: str | None = None
    previous: NetCounters | None = None
    previous_at = time.monotonic()
    sequence = 0
    next_interface_refresh = 0.0

    while not stop_event.wait(interval):
        now_mono = time.monotonic()
        per_interface = psutil.net_io_counters(pernic=True)
        if interface not in per_interface or now_mono >= next_interface_refresh:
            selected = choose_network_interface(per_interface, network_override)
            if selected != interface:
                previous = None
            interface = selected
            next_interface_refresh = now_mono + 30.0

        current = per_interface.get(interface) if interface else None
        elapsed = max(0.001, now_mono - previous_at)
        download_bps = 0
        upload_bps = 0
        if current is not None and previous is not None:
            download_bps = max(0, int((current.bytes_recv - previous.bytes_recv) / elapsed))
            upload_bps = max(0, int((current.bytes_sent - previous.bytes_sent) / elapsed))
        previous = current
        previous_at = now_mono

        night_mode = is_night_hour(
            time.localtime().tm_hour,
            night_start_hour,
            night_end_hour,
        )
        usage = usage_state.get()
        snapshot = DesktopStatusSnapshot(
            valid=True,
            sequence=sequence,
            cpu_percent=max(0.0, min(100.0, float(psutil.cpu_percent(interval=None)))),
            memory_percent=max(0.0, min(100.0, float(psutil.virtual_memory().percent))),
            codex_remaining_percent=usage.remaining_percent if usage.valid else None,
            codex_usage_stale=usage.stale,
            download_bps=download_bps,
            upload_bps=upload_bps,
            display_brightness_percent=night_brightness if night_mode else day_brightness,
            offline_brightness_percent=offline_brightness,
            night_mode=night_mode,
            interface=interface,
            sampled_at=int(time.time()),
            error=None if interface else "network interface unavailable",
        )
        state.set(snapshot)
        sequence = (sequence + 1) & 0xFFFF


sample_mac_status_loop = sample_desktop_status_loop


def _enumerated_serial_ports() -> list[tuple[str, bool]]:
    if serial_list_ports is None:
        return []
    try:
        records = serial_list_ports.comports()
    except Exception:  # pragma: no cover - backend failures vary by OS and driver
        return []

    usb_markers = (
        "usb",
        "ch340",
        "ch341",
        "cp210",
        "silicon labs",
        "ftdi",
        "wch",
        "uart bridge",
    )
    result: list[tuple[str, bool]] = []
    for record in records:
        device = str(getattr(record, "device", "")).strip()
        if not device:
            continue
        metadata = " ".join(
            str(getattr(record, field, "") or "")
            for field in ("description", "manufacturer", "product", "hwid")
        ).casefold()
        is_usb = getattr(record, "vid", None) is not None or any(marker in metadata for marker in usb_markers)
        result.append((device, is_usb))
    return result


def discover_serial_port(
    explicit_port: str | None,
    platform_name: str | None = None,
) -> tuple[str | None, str | None]:
    platform_name = sys.platform if platform_name is None else platform_name
    enumerated = _enumerated_serial_ports()
    if explicit_port:
        for device, _ in enumerated:
            if device.casefold() == explicit_port.casefold():
                return device, None
        if Path(explicit_port).exists() or (
            platform_name == "win32" and re.fullmatch(r"(?i)COM[1-9]\d*", explicit_port)
        ):
            return explicit_port, None
        return None, "configured USB port not found"

    ports = {device for device, is_usb in enumerated if is_usb}
    patterns: tuple[str, ...] = ()
    if platform_name == "darwin":
        patterns = (
            "/dev/cu.usbserial-*",
            "/dev/cu.wchusbserial*",
            "/dev/cu.SLAB_USBtoUART*",
        )
    elif platform_name.startswith("linux"):
        patterns = ("/dev/ttyUSB*", "/dev/ttyACM*")
    ports.update(path for pattern in patterns for path in glob.glob(pattern))
    sorted_ports = sorted(ports, key=str.casefold)
    if len(sorted_ports) == 1:
        return sorted_ports[0], None
    if not sorted_ports:
        return None, "USB display not connected"
    return None, "multiple USB serial devices; configure DESKTOP_BRIDGE_SERIAL_PORT"


def serial_writer_loop(
    status_state: DesktopStatusState,
    usb_state: UsbState,
    stop_event: threading.Event,
    explicit_port: str | None,
    baud: int,
    reconnect_seconds: float = DEFAULT_RECONNECT_SECONDS,
) -> None:
    assert serial is not None
    active = None
    last_sequence: int | None = None
    last_error: str | None = None

    while not stop_event.is_set():
        if active is None:
            port, error = discover_serial_port(explicit_port)
            if port is None:
                usb_state.set(UsbSnapshot(error=error))
                if error != last_error:
                    print(f"[desktop-bridge] {error}", file=sys.stderr, flush=True)
                    last_error = error
                stop_event.wait(reconnect_seconds)
                continue
            try:
                active = serial.Serial(port, baudrate=baud, timeout=0.2, write_timeout=1.0)
                active.dtr = False
                active.rts = False
                active.reset_input_buffer()
                stop_event.wait(2.0)  # ESP8266 boards commonly reset when the port opens.
                usb_state.set(UsbSnapshot(connected=True, port=port, error=None))
                print(f"[desktop-bridge] USB display connected: {port}", file=sys.stderr, flush=True)
                last_error = None
                last_sequence = None
            except (OSError, serial.SerialException) as exc:
                active = None
                message = f"cannot open USB display: {exc}"
                usb_state.set(UsbSnapshot(port=port, error="cannot open USB display"))
                if message != last_error:
                    print(f"[desktop-bridge] {message}", file=sys.stderr, flush=True)
                    last_error = message
                stop_event.wait(reconnect_seconds)
                continue

        snapshot = status_state.get()
        if not snapshot.valid or snapshot.sequence == last_sequence:
            stop_event.wait(0.1)
            continue
        try:
            active.write(encode_status_frame(snapshot))
            active.flush()
            last_sequence = snapshot.sequence
            usb_state.set(
                UsbSnapshot(
                    connected=True,
                    port=active.port,
                    last_sent_at=int(time.time()),
                    error=None,
                )
            )
        except (OSError, serial.SerialException) as exc:
            port = getattr(active, "port", None)
            try:
                active.close()
            except (OSError, serial.SerialException):
                pass
            active = None
            usb_state.set(UsbSnapshot(port=port, error="USB display disconnected"))
            print(f"[desktop-bridge] USB display disconnected: {exc}", file=sys.stderr, flush=True)
            stop_event.wait(reconnect_seconds)

    if active is not None:
        active.close()


def make_handler(
    usage_state: codex.UsageState,
    status_state: DesktopStatusState,
    usb_state: UsbState,
) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "DesktopDisplayBridge/1.0"

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            path = self.path.split("?", 1)[0]
            if path == "/v1/codex-usage":
                self._send_json(usage_state.get().as_public_dict(), HTTPStatus.OK)
            elif path in {"/v1/desktop-status", "/v1/mac-status"}:
                self._send_json(status_state.get().as_public_dict(), HTTPStatus.OK)
            elif path == "/health":
                usage = usage_state.get()
                status = status_state.get()
                usb = usb_state.get()
                self._send_json(
                    {
                        "ok": True,
                        "usage_ready": usage.valid,
                        "usage_stale": usage.stale,
                        "desktop_status_ready": status.valid,
                        "mac_status_ready": status.valid,
                        "codex_usage_on_usb_ready": status.codex_remaining_percent is not None,
                        "host_platform": status.host_platform,
                        "usb": usb.as_public_dict(),
                    },
                    HTTPStatus.OK,
                )
            else:
                self._send_json({"ok": False, "error": "not found"}, HTTPStatus.NOT_FOUND)

        def _send_json(self, payload: Mapping[str, Any], status: HTTPStatus) -> None:
            body = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
            self.send_response(status.value)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt: str, *args: Any) -> None:
            print(f"[desktop-bridge] {self.client_address[0]} {fmt % args}", file=sys.stderr)

    return Handler


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--auth-file", type=Path, default=codex.DEFAULT_AUTH_FILE)
    parser.add_argument("--upstream-url", default=codex.DEFAULT_UPSTREAM_URL)
    parser.add_argument("--listen-host", default=os.getenv("CODEX_BRIDGE_HOST", codex.DEFAULT_LISTEN_HOST))
    parser.add_argument(
        "--listen-port",
        type=int,
        default=int(os.getenv("CODEX_BRIDGE_PORT", str(codex.DEFAULT_LISTEN_PORT))),
    )
    parser.add_argument(
        "--refresh-seconds",
        type=int,
        default=int(os.getenv("CODEX_BRIDGE_REFRESH_SECONDS", str(codex.DEFAULT_REFRESH_SECONDS))),
    )
    parser.add_argument("--timeout", type=float, default=codex.DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--serial-port", default=os.getenv("DESKTOP_BRIDGE_SERIAL_PORT") or None)
    parser.add_argument(
        "--network-interface",
        default=os.getenv("DESKTOP_BRIDGE_NETWORK_INTERFACE") or None,
    )
    parser.add_argument("--serial-baud", type=int, default=DEFAULT_SERIAL_BAUD)
    parser.add_argument("--sample-seconds", type=float, default=DEFAULT_SAMPLE_SECONDS)
    parser.add_argument(
        "--night-start-hour",
        type=int,
        default=int(os.getenv("DESKTOP_BRIDGE_NIGHT_START_HOUR", str(DEFAULT_NIGHT_START_HOUR))),
    )
    parser.add_argument(
        "--night-end-hour",
        type=int,
        default=int(os.getenv("DESKTOP_BRIDGE_NIGHT_END_HOUR", str(DEFAULT_NIGHT_END_HOUR))),
    )
    parser.add_argument(
        "--day-brightness",
        type=int,
        default=int(os.getenv("DESKTOP_BRIDGE_DAY_BRIGHTNESS", str(DEFAULT_DAY_BRIGHTNESS))),
    )
    parser.add_argument(
        "--night-brightness",
        type=int,
        default=int(os.getenv("DESKTOP_BRIDGE_NIGHT_BRIGHTNESS", str(DEFAULT_NIGHT_BRIGHTNESS))),
    )
    parser.add_argument(
        "--offline-brightness",
        type=int,
        default=int(os.getenv("DESKTOP_BRIDGE_OFFLINE_BRIGHTNESS", str(DEFAULT_OFFLINE_BRIGHTNESS))),
    )
    parser.add_argument("--no-usb", action="store_true", help="collect metrics without opening a serial port")
    return parser


def _validate_args(args: argparse.Namespace) -> str | None:
    if not 1 <= args.listen_port <= 65535:
        return "listen port must be between 1 and 65535"
    if args.refresh_seconds < 60:
        return "Codex refresh interval must be at least 60 seconds"
    if not 0.25 <= args.sample_seconds <= 10.0:
        return "sample interval must be between 0.25 and 10 seconds"
    if not 1200 <= args.serial_baud <= 2_000_000:
        return "serial baud must be between 1200 and 2000000"
    if not 0 <= args.night_start_hour <= 23 or not 0 <= args.night_end_hour <= 23:
        return "night start and end hours must be between 0 and 23"
    for name in ("day_brightness", "night_brightness", "offline_brightness"):
        if not 0 <= getattr(args, name) <= 100:
            return f"{name.replace('_', ' ')} must be between 0 and 100"
    return None


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.auth_file = args.auth_file.expanduser()
    validation_error = _validate_args(args)
    if validation_error:
        print(validation_error, file=sys.stderr)
        return 2
    if psutil is None:
        print("psutil is required; install tools/desktop_display_bridge/requirements.txt", file=sys.stderr)
        return 2
    if not args.no_usb and serial is None:
        print("pyserial is required; install tools/desktop_display_bridge/requirements.txt", file=sys.stderr)
        return 2

    usage_state = codex.UsageState()
    status_state = DesktopStatusState()
    usb_state = UsbState()
    stop_event = threading.Event()
    server = codex.BridgeHTTPServer(
        (args.listen_host, args.listen_port),
        make_handler(usage_state, status_state, usb_state),
    )

    threads = [
        threading.Thread(
            target=codex.refresh_loop,
            args=(
                usage_state,
                stop_event,
                args.auth_file,
                args.upstream_url,
                args.refresh_seconds,
                args.timeout,
            ),
            name="codex-usage-refresh",
            daemon=True,
        ),
        threading.Thread(
            target=sample_desktop_status_loop,
            args=(
                status_state,
                usage_state,
                stop_event,
                args.sample_seconds,
                args.network_interface,
                args.night_start_hour,
                args.night_end_hour,
                args.day_brightness,
                args.night_brightness,
                args.offline_brightness,
            ),
            name="desktop-status-sampler",
            daemon=True,
        ),
    ]
    if not args.no_usb:
        threads.append(
            threading.Thread(
                target=serial_writer_loop,
                args=(status_state, usb_state, stop_event, args.serial_port, args.serial_baud),
                name="usb-display-writer",
                daemon=True,
            )
        )
    for thread in threads:
        thread.start()

    print(
        f"[desktop-bridge] listening on http://{args.listen_host}:{args.listen_port}",
        file=sys.stderr,
        flush=True,
    )
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        server.shutdown()
        server.server_close()
        for thread in threads:
            thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

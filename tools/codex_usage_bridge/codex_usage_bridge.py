#!/usr/bin/env python3
"""Expose the local Codex subscription window as a tiny LAN JSON endpoint.

The bridge is intentionally small: it reads the OAuth credential already
maintained by Codex, queries the same ChatGPT usage endpoint as the desktop
client, and publishes only percentages/reset times. The OAuth token is never
returned to the ESP8266 or written to bridge logs.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import os
import ssl
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, replace
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from socketserver import TCPServer
from typing import Any, Mapping


DEFAULT_AUTH_FILE = Path("~/.codex/auth.json").expanduser()
DEFAULT_UPSTREAM_URL = "https://chatgpt.com/backend-api/wham/usage"
DEFAULT_LISTEN_HOST = "0.0.0.0"
DEFAULT_LISTEN_PORT = 8766
DEFAULT_REFRESH_SECONDS = 300
DEFAULT_TIMEOUT_SECONDS = 20
USER_AGENT = "SmallDesktopDisplay-CodexBridge/1.0"
TRUSTED_UPSTREAM_HOSTS = {"chatgpt.com"}
LOOPBACK_UPSTREAM_HOSTS = {"localhost", "127.0.0.1", "::1"}


class BridgeError(RuntimeError):
    """An expected credential, network, or response error."""


def _verified_ssl_context() -> ssl.SSLContext:
    """Build a verified TLS context, including Python.org installs on macOS."""
    try:
        import certifi  # type: ignore[import-not-found]
    except ImportError:
        return ssl.create_default_context()
    return ssl.create_default_context(cafile=certifi.where())


@dataclass(frozen=True)
class CodexCredentials:
    access_token: str
    account_id: str | None


@dataclass(frozen=True)
class UsageSnapshot:
    valid: bool = False
    used_percent: int | None = None
    remaining_percent: int | None = None
    reset_at: int | None = None
    reset_minutes: int | None = None
    window_seconds: int | None = None
    fetched_at: int | None = None
    stale: bool = False
    error: str | None = "waiting for first refresh"

    def as_dict(self) -> dict[str, Any]:
        return {
            "schema": 1,
            "ok": self.valid,
            "used_percent": self.used_percent,
            "remaining_percent": self.remaining_percent,
            "reset_at": self.reset_at,
            "reset_minutes": self.reset_minutes,
            "window_seconds": self.window_seconds,
            "fetched_at": self.fetched_at,
            "stale": self.stale,
            "error": self.error,
        }

    def as_public_dict(self) -> dict[str, Any]:
        """Return LAN-safe data without local paths or upstream diagnostics."""
        payload = self.as_dict()
        if self.error is None:
            payload["error"] = None
        elif self.valid:
            payload["error"] = "stale"
        else:
            payload["error"] = "unavailable"
        return payload


def _decode_jwt_payload(token: str) -> Mapping[str, Any] | None:
    parts = token.split(".")
    if len(parts) < 2:
        return None
    encoded = parts[1].replace("-", "+").replace("_", "/")
    encoded += "=" * (-len(encoded) % 4)
    try:
        raw = base64.b64decode(encoded, validate=True)
        payload = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError, json.JSONDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def _account_id_from_id_token(id_token: str) -> str | None:
    payload = _decode_jwt_payload(id_token)
    if payload is None:
        return None
    auth = payload.get("https://api.openai.com/auth")
    if not isinstance(auth, dict):
        return None
    account_id = auth.get("chatgpt_account_id")
    return account_id if isinstance(account_id, str) and account_id else None


def load_credentials(path: Path) -> CodexCredentials:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise BridgeError(f"Codex credential file not found: {path}") from exc
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BridgeError(f"Cannot read Codex credential file: {exc}") from exc

    tokens = payload.get("tokens") if isinstance(payload, dict) else None
    if not isinstance(tokens, dict):
        raise BridgeError("Codex credential file has no tokens object")
    access_token = tokens.get("access_token")
    if not isinstance(access_token, str) or not access_token:
        raise BridgeError("Codex credential file has no access token")

    account_id = tokens.get("account_id")
    if not isinstance(account_id, str) or not account_id:
        account_id = None
        id_token = tokens.get("id_token")
        if isinstance(id_token, str) and id_token:
            account_id = _account_id_from_id_token(id_token)
    return CodexCredentials(access_token=access_token, account_id=account_id)


def _number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BridgeError(f"Codex usage response has invalid {field}")
    result = float(value)
    if not math.isfinite(result):
        raise BridgeError(f"Codex usage response has non-finite {field}")
    return result


def validate_upstream_url(upstream_url: str) -> None:
    """Prevent an OAuth token from being sent to an arbitrary endpoint."""
    parsed = urllib.parse.urlsplit(upstream_url)
    host = (parsed.hostname or "").lower()
    if parsed.username is not None or parsed.password is not None:
        raise BridgeError("Codex usage URL must not contain credentials")
    if parsed.fragment:
        raise BridgeError("Codex usage URL must not contain a fragment")
    if host in LOOPBACK_UPSTREAM_HOSTS and parsed.scheme in {"http", "https"}:
        return
    trusted_host = host in TRUSTED_UPSTREAM_HOSTS or any(
        host.endswith(f".{suffix}") for suffix in TRUSTED_UPSTREAM_HOSTS
    )
    if parsed.scheme != "https" or not trusted_host:
        raise BridgeError("Codex usage URL must use HTTPS on chatgpt.com")


def parse_usage_response(payload: Mapping[str, Any], now: float | None = None) -> UsageSnapshot:
    now = time.time() if now is None else now
    rate_limit = payload.get("rate_limit")
    if not isinstance(rate_limit, dict):
        raise BridgeError("Codex usage response has no rate_limit object")

    windows: list[tuple[int, Mapping[str, Any]]] = []
    fallbacks = {"primary_window": 5 * 3600, "secondary_window": 7 * 86400}
    for name, fallback in fallbacks.items():
        candidate = rate_limit.get(name)
        if not isinstance(candidate, dict):
            continue
        seconds_value = candidate.get("limit_window_seconds", fallback)
        seconds = int(_number(seconds_value, f"{name}.limit_window_seconds"))
        if seconds > 0:
            windows.append((seconds, candidate))
    if not windows:
        raise BridgeError("Codex usage response contains no usable window")

    # Prefer a weekly/long window. Accounts that only expose a short window
    # still receive a useful value instead of a blank display.
    long_windows = [item for item in windows if item[0] >= 2 * 86400]
    window_seconds, selected = max(long_windows or windows, key=lambda item: item[0])
    used = int(round(_number(selected.get("used_percent"), "used_percent")))
    used = max(0, min(100, used))

    reset_at: int | None = None
    reset_minutes: int | None = None
    if selected.get("reset_at") is not None:
        reset_at = max(0, int(_number(selected["reset_at"], "reset_at")))
        reset_minutes = max(0, int((reset_at - now) / 60))

    return UsageSnapshot(
        valid=True,
        used_percent=used,
        remaining_percent=100 - used,
        reset_at=reset_at,
        reset_minutes=reset_minutes,
        window_seconds=window_seconds,
        fetched_at=int(now),
        stale=False,
        error=None,
    )


def fetch_usage(
    auth_file: Path,
    upstream_url: str = DEFAULT_UPSTREAM_URL,
    timeout: float = DEFAULT_TIMEOUT_SECONDS,
) -> UsageSnapshot:
    validate_upstream_url(upstream_url)
    credentials = load_credentials(auth_file)
    headers = {
        "Authorization": f"Bearer {credentials.access_token}",
        "Accept": "application/json",
        "User-Agent": USER_AGENT,
    }
    if credentials.account_id:
        headers["ChatGPT-Account-Id"] = credentials.account_id
    request = urllib.request.Request(upstream_url, headers=headers)
    open_kwargs: dict[str, Any] = {"timeout": timeout}
    if upstream_url.lower().startswith("https://"):
        open_kwargs["context"] = _verified_ssl_context()
    try:
        with urllib.request.urlopen(request, **open_kwargs) as response:
            status = response.status
            body = response.read(256 * 1024)
    except urllib.error.HTTPError as exc:
        if exc.code in (401, 403):
            raise BridgeError("Codex login expired; sign in to Codex again") from exc
        if exc.code == 429:
            raise BridgeError("Codex usage endpoint is rate limited") from exc
        raise BridgeError(f"Codex usage endpoint returned HTTP {exc.code}") from exc
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise BridgeError(f"Codex usage request failed: {exc}") from exc

    if not 200 <= status < 300:
        raise BridgeError(f"Codex usage endpoint returned HTTP {status}")
    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BridgeError("Codex usage endpoint returned invalid JSON") from exc
    if not isinstance(payload, dict):
        raise BridgeError("Codex usage endpoint returned an invalid object")
    return parse_usage_response(payload)


class UsageState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = UsageSnapshot()

    def get(self) -> UsageSnapshot:
        with self._lock:
            return self._snapshot

    def set_success(self, snapshot: UsageSnapshot) -> None:
        with self._lock:
            self._snapshot = snapshot

    def set_error(self, message: str) -> None:
        with self._lock:
            if self._snapshot.valid:
                self._snapshot = replace(self._snapshot, stale=True, error=message)
            else:
                self._snapshot = UsageSnapshot(error=message)


class BridgeHTTPServer(ThreadingHTTPServer):
    """HTTP server that avoids a blocking reverse-DNS lookup at startup."""

    allow_reuse_address = True

    def server_bind(self) -> None:
        # HTTPServer.server_bind() calls socket.getfqdn() on the listen address.
        # Some macOS/router combinations leave that reverse lookup waiting for
        # many seconds. The server does not use its own hostname, so keep the
        # bound address directly and begin serving immediately.
        TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = str(host)
        self.server_port = int(port)


def refresh_loop(
    state: UsageState,
    stop_event: threading.Event,
    auth_file: Path,
    upstream_url: str,
    refresh_seconds: int,
    timeout: float,
) -> None:
    while not stop_event.is_set():
        try:
            state.set_success(fetch_usage(auth_file, upstream_url, timeout))
        except BridgeError as exc:
            state.set_error(str(exc))
            print(f"[codex-bridge] {exc}", file=sys.stderr, flush=True)
        stop_event.wait(refresh_seconds)


def make_handler(state: UsageState) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "SDDCodexBridge/1.0"

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            path = self.path.split("?", 1)[0]
            if path == "/v1/codex-usage":
                self._send_json(state.get().as_public_dict(), HTTPStatus.OK)
            elif path == "/health":
                snapshot = state.get()
                self._send_json(
                    {"ok": True, "usage_ready": snapshot.valid, "stale": snapshot.stale},
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
            print(f"[codex-bridge] {self.client_address[0]} {fmt % args}", file=sys.stderr)

    return Handler


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--auth-file", type=Path, default=DEFAULT_AUTH_FILE)
    parser.add_argument("--upstream-url", default=DEFAULT_UPSTREAM_URL)
    parser.add_argument("--listen-host", default=os.getenv("CODEX_BRIDGE_HOST", DEFAULT_LISTEN_HOST))
    parser.add_argument(
        "--listen-port",
        type=int,
        default=int(os.getenv("CODEX_BRIDGE_PORT", str(DEFAULT_LISTEN_PORT))),
    )
    parser.add_argument(
        "--refresh-seconds",
        type=int,
        default=int(os.getenv("CODEX_BRIDGE_REFRESH_SECONDS", str(DEFAULT_REFRESH_SECONDS))),
    )
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--once", action="store_true", help="fetch once and print safe JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.auth_file = args.auth_file.expanduser()
    if not 1 <= args.listen_port <= 65535:
        print("listen port must be between 1 and 65535", file=sys.stderr)
        return 2
    if args.refresh_seconds < 60:
        print("refresh interval must be at least 60 seconds", file=sys.stderr)
        return 2

    if args.once:
        try:
            snapshot = fetch_usage(args.auth_file, args.upstream_url, args.timeout)
        except BridgeError as exc:
            print(json.dumps(UsageSnapshot(error=str(exc)).as_dict(), separators=(",", ":")))
            return 1
        print(json.dumps(snapshot.as_dict(), separators=(",", ":")))
        return 0

    state = UsageState()
    stop_event = threading.Event()
    worker = threading.Thread(
        target=refresh_loop,
        args=(
            state,
            stop_event,
            args.auth_file,
            args.upstream_url,
            args.refresh_seconds,
            args.timeout,
        ),
        name="codex-usage-refresh",
        daemon=True,
    )
    worker.start()

    server = BridgeHTTPServer((args.listen_host, args.listen_port), make_handler(state))
    print(
        f"[codex-bridge] listening on http://{args.listen_host}:{args.listen_port}/v1/codex-usage",
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
        worker.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

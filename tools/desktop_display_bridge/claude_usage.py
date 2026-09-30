"""Opt-in Claude account quota reader. Credentials never leave this module.

Adapted from steipete/CodexBar (MIT), revision 25bba9b7fd9ce83c33053958f7366e23b2dc8a82.
Uses Claude's account usage endpoint, not message inference or an API-key budget.
The account endpoint is not a stable public API; failures remain isolated from
USB/system sampling. Login/renewal belongs to Claude, never to this monitor.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime
from email.utils import parsedate_to_datetime
from pathlib import Path
from typing import Any

from claude_cli import renew_credentials

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
REFRESH_SECONDS = 120
STALE_SECONDS = 300
MAX_BYTES = 65536


class UsageError(Exception):
    def __init__(self, code: str, retry_after: int = REFRESH_SECONDS):
        super().__init__(code)
        self.code = code
        self.retry_after = retry_after


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward account authorization to a redirected host/path.
        return None


def _read_access_token_once() -> str:
    config = Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude")
    try:
        path = config / ".credentials.json"
        if path.exists():
            with path.open("rb") as stream:
                raw = stream.read(MAX_BYTES + 1)
        else:
            raise UsageError("login_required")
        if len(raw) > MAX_BYTES:
            raise UsageError("login_required")
        document = json.loads(raw)
        oauth = document.get("claudeAiOauth", {})
        token = oauth.get("accessToken")
        expires = oauth.get("expiresAt")
        if not isinstance(token, str) or not token or len(token) > 16384 or any(c.isspace() for c in token):
            raise UsageError("login_required")
        if type(expires) not in (int, float) or not math.isfinite(expires) or expires / 1000 <= time.time():
            raise UsageError("login_expired")
        scopes = oauth.get("scopes")
        if not isinstance(scopes, list) or "user:profile" not in scopes:
            raise UsageError("missing_scope")
        return token
    except (OSError, ValueError, AttributeError):
        raise UsageError("login_required") from None


def read_access_token() -> str:
    # The owner may be atomically publishing a renewed profile at this instant.
    for attempt in range(3):
        try:
            return _read_access_token_once()
        except UsageError as error:
            if error.code != "login_required" or attempt == 2:
                raise
            time.sleep(0.05)
    raise UsageError("login_required")


def retry_after_seconds(raw: str | None, now: float | None = None) -> int:
    now = time.time() if now is None else now
    try:
        delay = float(raw) if raw is not None else 300.0
    except (TypeError, ValueError):
        try:
            delay = parsedate_to_datetime(raw).timestamp() - now
        except (TypeError, ValueError, AttributeError, OverflowError):
            delay = 300.0
    if not math.isfinite(delay):
        delay = 300.0
    return max(REFRESH_SECONDS, min(3600, math.ceil(delay)))


def parse_window(value: Any, now: float) -> dict[str, Any] | None:
    if not isinstance(value, dict):
        return None
    used = value.get("utilization")
    if type(used) not in (int, float) or not math.isfinite(used) or not 0 <= used <= 100:
        return None
    reset = value.get("resets_at")
    if reset is not None:
        try:
            parsed = datetime.fromisoformat(reset.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                return None
            reset = parsed.timestamp()
            if reset <= now:
                return None
        except (ValueError, TypeError, AttributeError, OverflowError, OSError):
            return None
    elif used != 0:
        # An inactive, unused window can legitimately have no reset timestamp.
        return None
    return {"remaining_percent": round(100 - used, 1), "reset_at": reset}


def fetch_usage(token: str) -> dict[str, Any]:
    request = urllib.request.Request(USAGE_URL, headers={
        "Authorization": "Bearer " + token, "anthropic-beta": "oauth-2025-04-20",
        "Accept": "application/json", "Content-Type": "application/json",
        "User-Agent": "claude-code/2.1.0",
    })
    try:
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=10) as response:
            raw = response.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise UsageError("invalid_response")
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise UsageError("invalid_response")
        return data
    except urllib.error.HTTPError as exc:
        retry_after = (exc.headers or {}).get("Retry-After")
        exc.close()
        if exc.code in (401, 403):
            raise UsageError("login_expired" if exc.code == 401 else "access_denied") from None
        if exc.code == 429:
            raise UsageError("rate_limited", retry_after_seconds(retry_after)) from None
        raise UsageError("service_unavailable") from None
    except (OSError, ValueError, urllib.error.URLError):
        raise UsageError("network_error") from None


class ClaudeUsageState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._windows: dict[str, Any] = {}
        self._fetched_at: float | None = None
        self._error: str | None = "disabled"
        self._account: str | None = None

    def clear(self, error: str) -> None:
        with self._lock:
            self._windows = {}
            self._fetched_at = None
            self._error = error
            self._account = None

    def set_error(self, error: str) -> None:
        if error in {"login_required", "login_expired", "access_denied", "disabled", "missing_scope"}:
            self.clear(error)
        else:
            with self._lock:
                self._error = error

    def update(self, data: dict[str, Any], now: float | None = None) -> None:
        now = time.time() if now is None else now
        windows = {key: parse_window(data.get(key), now) for key in ("five_hour", "seven_day")}
        if not any(windows.values()):
            raise UsageError("no_quota")
        with self._lock:
            self._windows = windows
            self._fetched_at = now
            self._error = None

    def get(self, now: float | None = None) -> dict[str, Any]:
        now = time.time() if now is None else now
        with self._lock:
            windows = {}
            expired = False
            for key in ("five_hour", "seven_day"):
                window = self._windows.get(key)
                # Never carry last window's remaining quota past its reset.
                if window and (now >= (window["reset_at"] or (self._fetched_at or 0) + STALE_SECONDS)):
                    window = None
                    expired = True
                windows[key] = dict(window) if window else None
            return {"ok": any(windows.values()), "source": "claude-oauth", **windows, "fetched_at": self._fetched_at,
                    "stale": bool(self._error or expired or self._fetched_at is None or now - self._fetched_at > STALE_SECONDS),
                    "error": self._error or ("awaiting_reset" if expired else None)}

    def run(self, settings, stop: threading.Event) -> None:
        next_refresh = 0.0
        next_renewal = 0.0
        while not stop.is_set():
            if not settings.get().claude_enabled:
                self.clear("disabled")
                next_refresh = 0.0
            elif time.monotonic() >= next_refresh:
                delay = REFRESH_SECONDS
                try:
                    try:
                        token = read_access_token()
                    except UsageError as auth_error:
                        if auth_error.code != "login_expired" or time.monotonic() < next_renewal:
                            raise
                        self.clear("renewing")
                        next_renewal = time.monotonic() + 300
                        def ready() -> bool:
                            if not settings.get().claude_enabled:
                                return True  # Stop the private probe immediately after opting out.
                            try:
                                read_access_token()
                                return True
                            except UsageError:
                                return False
                        renew_credentials(settings.path.parent / 'claude-probe', stop, ready)
                        if stop.is_set() or not settings.get().claude_enabled:
                            self.clear('disabled')
                            continue
                        token = read_access_token()
                    identity = hashlib.sha256(token.encode()).hexdigest()
                    if self._account != identity:
                        self.clear("connecting")
                        self._account = identity
                    self.update(fetch_usage(token))
                except UsageError as exc:
                    self.set_error(exc.code)
                    delay = exc.retry_after
                # A request in flight cannot restore data after opting out.
                if not settings.get().claude_enabled:
                    self.clear("disabled")
                next_refresh = time.monotonic() + delay
            stop.wait(1)

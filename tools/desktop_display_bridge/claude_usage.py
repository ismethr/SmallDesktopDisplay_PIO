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
import re
import subprocess
import sys
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
# Claude Code's own macOS Keychain item (the same source CodexBar reads).
KEYCHAIN_SERVICE = "Claude Code-credentials"
SECURITY_TOOL = "/usr/bin/security"
# Leaves time to answer a Keychain dialog; no answer counts as a denial.
KEYCHAIN_TIMEOUT_SECONDS = 30
KEYCHAIN_ITEM_NOT_FOUND = 44  # errSecItemNotFound, as truncated by security(1)
KEYCHAIN_EXPIRY_MARGIN_SECONDS = 60


class UsageError(Exception):
    def __init__(self, code: str, retry_after: int = REFRESH_SECONDS):
        super().__init__(code)
        self.code = code
        self.retry_after = retry_after


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward account authorization to a redirected host/path.
        return None


def _parse_credentials(raw: bytes) -> tuple[str, float]:
    """Return the access token and its expiry in seconds."""
    try:
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
        return token, expires / 1000
    except (ValueError, AttributeError):
        raise UsageError("login_required") from None


def _read_credentials_file(config: Path) -> bytes | None:
    try:
        path = config / ".credentials.json"
        if not path.exists():
            return None
        with path.open("rb") as stream:
            return stream.read(MAX_BYTES + 1)
    except OSError:
        raise UsageError("login_required") from None


def _read_keychain_item() -> bytes:
    """Read Claude Code's Keychain item. macOS may ask the user to allow this."""
    try:
        result = subprocess.run(
            [SECURITY_TOOL, "find-generic-password", "-s", KEYCHAIN_SERVICE, "-w"],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            timeout=KEYCHAIN_TIMEOUT_SECONDS, check=False)
    except subprocess.TimeoutExpired:
        raise UsageError("keychain_denied") from None
    except OSError:
        raise UsageError("login_required") from None
    if result.returncode == KEYCHAIN_ITEM_NOT_FOUND:
        raise UsageError("login_required")
    if result.returncode != 0:
        # Denied or cancelled dialog, locked Keychain or ACL failure.
        raise UsageError("keychain_denied")
    raw = result.stdout.strip()
    # security(1) prints non-printable secrets as hex.
    if not raw.startswith(b"{") and len(raw) % 2 == 0 and re.fullmatch(rb"[0-9a-fA-F]+", raw):
        raw = bytes.fromhex(raw.decode("ascii"))
    return raw


class _KeychainTokenCache:
    """Keep an unexpired Keychain token in memory only, so that a dialog
    answered with "Allow" is not repeated on every refresh."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._token: str | None = None
        self._expires_at = 0.0

    def read(self) -> str:
        with self._lock:
            if self._token and self._expires_at - KEYCHAIN_EXPIRY_MARGIN_SECONDS > time.time():
                return self._token
            self._token = None
            token, expires_at = _parse_credentials(_read_keychain_item())
            self._token, self._expires_at = token, expires_at
            return token

    def forget(self) -> None:
        with self._lock:
            self._token = None
            self._expires_at = 0.0


_keychain_tokens = _KeychainTokenCache()


def forget_keychain_token() -> None:
    _keychain_tokens.forget()


def _read_access_token_once(allow_keychain: bool) -> str:
    override = os.environ.get("CLAUDE_CONFIG_DIR")
    config = Path(override or Path.home() / ".claude")
    file_error = UsageError("login_required")
    raw = _read_credentials_file(config)
    if raw is not None:
        try:
            return _parse_credentials(raw)[0]
        except UsageError as error:
            file_error = error
    # Claude Code on macOS keeps the default profile in the Keychain. Another
    # CLAUDE_CONFIG_DIR is a different profile, so never substitute it.
    if not allow_keychain or sys.platform != "darwin" or override:
        raise file_error
    try:
        return _keychain_tokens.read()
    except UsageError as error:
        # A leftover file from an older install must not hide the live login,
        # but its error is more useful than "not found" when both are absent.
        if error.code == "login_required":
            raise file_error from None
        raise


def read_access_token(allow_keychain: bool = True) -> str:
    # The owner may be atomically publishing a renewed profile at this instant.
    for attempt in range(3):
        try:
            return _read_access_token_once(allow_keychain)
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
        if error in {"login_required", "login_expired", "access_denied", "disabled", "missing_scope", "keychain_denied"}:
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
        # CodexBar's default prompt policy: after a denied Keychain dialog, only
        # the user's next opt-in may show one again.
        keychain_allowed = True
        while not stop.is_set():
            if not settings.get().claude_enabled:
                self.clear("disabled")
                forget_keychain_token()
                keychain_allowed = True
                next_refresh = 0.0
            elif time.monotonic() >= next_refresh:
                delay = REFRESH_SECONDS
                try:
                    try:
                        token = read_access_token(allow_keychain=keychain_allowed)
                    except UsageError as auth_error:
                        if auth_error.code == "keychain_denied":
                            keychain_allowed = False
                        elif not keychain_allowed and auth_error.code == "login_required":
                            raise UsageError("keychain_denied") from None
                        if auth_error.code != "login_expired" or time.monotonic() < next_renewal:
                            raise
                        self.clear("renewing")
                        next_renewal = time.monotonic() + 300
                        def ready() -> bool:
                            if not settings.get().claude_enabled:
                                return True  # Stop the private probe immediately after opting out.
                            try:
                                read_access_token(allow_keychain=keychain_allowed)
                                return True
                            except UsageError:
                                return False
                        renew_credentials(settings.path.parent / 'claude-probe', stop, ready)
                        if stop.is_set() or not settings.get().claude_enabled:
                            self.clear('disabled')
                            continue
                        token = read_access_token(allow_keychain=keychain_allowed)
                    identity = hashlib.sha256(token.encode()).hexdigest()
                    if self._account != identity:
                        self.clear("connecting")
                        self._account = identity
                    self.update(fetch_usage(token))
                except UsageError as exc:
                    if exc.code in ("login_expired", "access_denied"):
                        # Claude may have rotated or revoked it; re-read next time.
                        forget_keychain_token()
                    self.set_error(exc.code)
                    delay = exc.retry_after
                # A request in flight cannot restore data after opting out.
                if not settings.get().claude_enabled:
                    self.clear("disabled")
                    forget_keychain_token()
                next_refresh = time.monotonic() + delay
            stop.wait(1)

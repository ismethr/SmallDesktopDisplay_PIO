"""Bounded, tool-free Claude /status probe, following CodexBar's CLI handoff.

Only Claude writes/renews Claude's credentials. Never send chat prompts, accept
onboarding/trust dialogs, scrape conversations, or print terminal output.
"""
from __future__ import annotations

import os
import re
import shutil
import socket
import sys
import time
from pathlib import Path
from typing import Callable


MACOS_SYSTEM_BINARIES = (Path('/opt/homebrew/bin/claude'), Path('/usr/local/bin/claude'))


def _version_key(path: Path) -> tuple[int, ...]:
    return tuple(map(int, path.name.split('.')))


def _find_macos_claude_binary() -> Path | None:
    candidates = [Path.home() / '.local' / 'bin' / 'claude', *MACOS_SYSTEM_BINARIES]
    located = shutil.which('claude')
    if located:
        candidates.insert(0, Path(located))
    # Claude Desktop keeps its own Claude Code copy per version.
    root = Path.home() / 'Library' / 'Application Support' / 'Claude' / 'claude-code'
    versions = [p for p in root.glob('*') if re.fullmatch(r'\d+\.\d+\.\d+', p.name)]
    for version in sorted(versions, key=_version_key, reverse=True):
        candidates.extend(sorted(version.glob('*/claude.app/Contents/MacOS/claude')))
    return next((p for p in candidates if p.is_file() and os.access(p, os.X_OK)), None)


def find_claude_binary() -> Path | None:
    if sys.platform == 'darwin':
        return _find_macos_claude_binary()
    candidates = [Path.home() / '.local' / 'bin' / 'claude.exe']
    located = shutil.which('claude.exe')
    if located:
        candidates.insert(0, Path(located))
    # The Store app's visible process path is virtualized; use its physical cache.
    roots = [Path(os.environ.get('APPDATA', '')) / 'Claude' / 'claude-code',
             Path(os.environ.get('LOCALAPPDATA', '')) / 'Packages' / 'Claude_pzs8sxrjxfjjc' /
             'LocalCache' / 'Roaming' / 'Claude' / 'claude-code']
    for root in roots:
        versions = [p for p in root.glob('*') if re.fullmatch(r'\d+\.\d+\.\d+', p.name)]
        candidates.extend(p / 'claude.exe' for p in sorted(versions, key=_version_key, reverse=True))
    return next((p for p in candidates if p.is_file()), None)


def probe_arguments(binary: Path) -> list[str]:
    return [str(binary), '--allowed-tools', '', '--strict-mcp-config', '--setting-sources', '',
            '--settings', '{"remoteControlAtStartup":false,"disableAllHooks":true}', '/status']


def probe_environment() -> dict[str, str]:
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('ANTHROPIC_') and k not in {
               'CLAUDE_CODE_OAUTH_TOKEN', 'CLAUDE_CODE_OAUTH_SCOPES', 'CLAUDECODE'}}
    env['DISABLE_AUTOUPDATER'] = '1'
    env['CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC'] = '1'
    return env


def _renew_with_posix_pty(directory: Path, stop, ready: Callable[[], bool], timeout: float) -> bool:
    # Claude Code reads and rewrites its own Keychain item; the probe only
    # gives it a chance to renew. Same bounded /status run as on Windows.
    binary = find_claude_binary()
    if binary is None or stop.is_set():
        return False
    import fcntl
    import pty
    import select
    import signal
    import struct
    import subprocess
    import termios
    process = None
    master = None
    try:
        directory.mkdir(parents=True, exist_ok=True)
        master, slave = pty.openpty()
        try:
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 32, 100, 0, 0))
            process = subprocess.Popen(probe_arguments(binary), cwd=str(directory), env=probe_environment(),
                                       stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
        finally:
            os.close(slave)
        terminal_tail = b""
        deadline = time.monotonic() + timeout
        next_check = 0.0
        while not stop.is_set() and time.monotonic() < deadline:
            if time.monotonic() >= next_check:
                # Each check may run security(1); once a second is enough.
                if ready():
                    return True
                next_check = time.monotonic() + 1
            if not select.select([master], [], [], 0.1)[0]:
                continue
            try:
                # Drain and discard; no terminal contents enter logs or snapshots.
                chunk = os.read(master, 4096)
            except OSError:  # EIO once the probe has exited
                chunk = b""
            if not chunk:
                return ready()
            terminal_tail += chunk
            # Ink asks the terminal for its cursor position. Reply only to this
            # terminal query; never press Enter on prompts.
            if b"\x1b[6n" in terminal_tail:
                os.write(master, b"\x1b[1;1R")
                terminal_tail = terminal_tail.replace(b"\x1b[6n", b"")
            terminal_tail = terminal_tail[-8:]
        return ready() if not stop.is_set() else False
    except (OSError, ValueError, subprocess.SubprocessError):
        return False
    finally:
        if process is not None:
            # The probe leads its own session; end any helpers it started too.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except OSError:
                pass
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=2)
                except (OSError, subprocess.TimeoutExpired):
                    pass
        if master is not None:
            os.close(master)


def renew_credentials(directory: Path, stop, ready: Callable[[], bool], timeout: float = 12) -> bool:
    if sys.platform == 'darwin':
        return _renew_with_posix_pty(directory, stop, ready, timeout)
    # Linux keeps file-backed credentials only.
    if sys.platform != 'win32':
        return False
    binary = find_claude_binary()
    if binary is None or stop.is_set():
        return False
    process = None
    try:
        from winpty import Backend, PtyProcess
        directory.mkdir(parents=True, exist_ok=True)
        process = PtyProcess.spawn(probe_arguments(binary), cwd=str(directory),
                                   env=probe_environment(), dimensions=(32, 100), backend=Backend.ConPTY)
        process.fileobj.settimeout(0.1)
        terminal_tail = ""
        deadline = time.monotonic() + timeout
        while not stop.is_set() and time.monotonic() < deadline:
            if ready():
                return True
            try:
                # Drain and discard; no terminal contents enter logs or snapshots.
                chunk = process.read(4096)
                terminal_tail += chunk
                # ConPTY/Ink asks the terminal host for its cursor position.
                # Reply only to this terminal query; never press Enter on prompts.
                if "\x1b[6n" in terminal_tail:
                    process.write("\x1b[1;1R")
                    terminal_tail = terminal_tail.replace("\x1b[6n", "")
                terminal_tail = terminal_tail[-8:]
            except socket.timeout:
                pass
            except EOFError:
                return ready()
            stop.wait(0.1)
        return ready() if not stop.is_set() else False
    except (ImportError, OSError, RuntimeError, ValueError):
        return False
    finally:
        if process is not None:
            try:
                import psutil
                for child in psutil.Process(process.pid).children(recursive=True):
                    try:
                        child.terminate()
                    except psutil.Error:
                        pass
            except Exception:
                pass
            try:
                process.close(force=True)
            except (OSError, RuntimeError):
                pass

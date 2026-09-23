#!/usr/bin/env python3
"""Single-instance Windows tray app with graceful installer lifecycle commands."""

from __future__ import annotations

import ctypes
import json
import os
import sys
import threading
import traceback
import webbrowser
from datetime import datetime
from pathlib import Path
from typing import TextIO

APP_NAME = "MiniDisplay Bridge"
MUTEX_NAME = "Local\\SmallDesktopDisplayBridge-9E5B3921"
STOP_EVENT_NAME = MUTEX_NAME + "-Stop"
ERROR_ALREADY_EXISTS = 183


def application_data_directory() -> Path:
    base = Path(os.getenv("LOCALAPPDATA") or Path.home() / "AppData" / "Local")
    return base / "SmallDesktopDisplay"


def open_log() -> tuple[TextIO, Path]:
    directory = application_data_directory() / "logs"
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "bridge.log"
    if path.exists() and path.stat().st_size >= 2 * 1024 * 1024:
        path.replace(directory / "bridge.previous.log")
    return path.open("a", encoding="utf-8", buffering=1), path


def show_message(message: str, *, error: bool = False) -> None:
    if os.name == "nt":
        ctypes.windll.user32.MessageBoxW(None, message, APP_NAME, (0x10 if error else 0x40) | 0x00040000)


def kernel_api():
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_bool, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.CreateEventW.restype = ctypes.c_void_p
    kernel.OpenEventW.argtypes = [ctypes.c_uint32, ctypes.c_bool, ctypes.c_wchar_p]
    kernel.OpenEventW.restype = ctypes.c_void_p
    kernel.SetEvent.argtypes = [ctypes.c_void_p]
    kernel.SetEvent.restype = ctypes.c_int
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.WaitForSingleObject.restype = ctypes.c_uint32
    # An untyped CloseHandle truncates a 64-bit HANDLE to a 32-bit integer.
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.CloseHandle.restype = ctypes.c_int
    return kernel


def acquire_single_instance_mutex() -> tuple[object | None, bool]:
    if os.name != "nt":
        return None, True
    kernel = kernel_api()
    handle = kernel.CreateMutexW(None, False, MUTEX_NAME)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    if ctypes.get_last_error() == ERROR_ALREADY_EXISTS:
        kernel.CloseHandle(handle)
        return None, False
    return handle, True


def release_mutex(handle: object | None) -> None:
    if os.name == "nt" and handle:
        kernel_api().CloseHandle(handle)


def request_stop() -> int:
    kernel = kernel_api()
    handle = kernel.OpenEventW(0x0002, False, STOP_EVENT_NAME)
    if not handle:
        return 0 if ctypes.get_last_error() == 2 else 1
    try:
        return 0 if kernel.SetEvent(handle) else 1
    finally:
        kernel.CloseHandle(handle)


def dashboard_url() -> str:
    try:
        port = json.loads((application_data_directory() / "instance.json").read_text(encoding="utf-8"))["port"]
        if type(port) is int and 1 <= port <= 65535:
            return f"http://127.0.0.1:{port}/"
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return "http://127.0.0.1:8766/"


def start_temperature_monitor() -> None:
    base = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[2] / "build/windows_sensors"
    executable = base / "sensors/MiniDisplayTemperature.exe"
    try:
        if executable.is_file():
            # The sensor app's manifest requests elevation only when selected.
            # The bridge itself continues to run without administrative rights.
            os.startfile(str(executable), arguments=str(os.getpid()), cwd=str(executable.parent))
        else:
            webbrowser.open("https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases")
    except OSError as exc:
        show_message(f"温度采集未启动：{exc}", error=True)


def run(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments == ["--stop"]:
        return request_stop()
    background = "--background" in arguments
    no_tray = "--no-tray" in arguments
    arguments = [arg for arg in arguments if arg not in {"--background", "--no-tray"}]
    mutex = stop_handle = None
    stream = None
    old_stdout, old_stderr = sys.stdout, sys.stderr
    stop = threading.Event()
    worker = watcher = icon = None
    result = [0]
    instance_path = application_data_directory() / "instance.json"
    owns_instance_file = False
    log_path = application_data_directory() / "logs/bridge.log"
    try:
        mutex, acquired = acquire_single_instance_mutex()
        if not acquired:
            if not background:
                webbrowser.open(dashboard_url())
            return 0
        stream, log_path = open_log()
        sys.stdout = sys.stderr = stream
        print(f"\n[{datetime.now().isoformat(timespec='seconds')}] starting Windows bridge")
        from desktop_display_bridge import ASSET_DIRECTORY, main as bridge_main

        kernel = kernel_api()
        stop_handle = kernel.CreateEventW(None, True, False, STOP_EVENT_NAME)
        if not stop_handle:
            raise ctypes.WinError(ctypes.get_last_error())

        def watch_stop() -> None:
            while not stop.is_set():
                if kernel.WaitForSingleObject(stop_handle, 250) == 0:
                    stop.set()

        def ready(port: int) -> None:
            nonlocal owns_instance_file
            instance_path.write_text(json.dumps({"port": port}), encoding="utf-8")
            owns_instance_file = True
            if not background:
                webbrowser.open(f"http://127.0.0.1:{port}/")

        def bridge_worker() -> None:
            try:
                result[0] = bridge_main(arguments, stop_event=stop, on_ready=ready)
            except SystemExit as exc:
                result[0] = exc.code if isinstance(exc.code, int) else 1
            except Exception:
                traceback.print_exc()
                result[0] = 1
            finally:
                stop.set()
                if icon is not None:
                    icon.stop()

        watcher = threading.Thread(target=watch_stop, name="windows-stop-event", daemon=True)
        worker = threading.Thread(target=bridge_worker, name="windows-bridge", daemon=True)
        watcher.start()
        if no_tray:
            worker.start()
            worker.join()
        else:
            import pystray
            from PIL import Image

            def quit_bridge() -> None:
                stop.set()
                icon.stop()

            icon = pystray.Icon("MiniDisplayBridge", Image.open(ASSET_DIRECTORY / "MiniDisplayBridgeIcon.png"),
                                APP_NAME, pystray.Menu(
                pystray.MenuItem("打开连接面板", lambda: webbrowser.open(dashboard_url()), default=True),
                pystray.MenuItem("显示屏设置", lambda: webbrowser.open(dashboard_url() + "#settings")),
                pystray.MenuItem("启动温度采集（管理员）", start_temperature_monitor),
                pystray.MenuItem("下载温度驱动 PawnIO", lambda: webbrowser.open("https://github.com/namazso/PawnIO.Setup/releases")),
                pystray.MenuItem("查看日志", lambda: os.startfile(str(log_path))),
                pystray.Menu.SEPARATOR,
                pystray.MenuItem("退出", quit_bridge),
            ))

            def setup(tray) -> None:
                tray.visible = True
                worker.start()

            icon.run(setup)
        if worker.ident is not None:
            worker.join(timeout=15)
        if result[0]:
            show_message(f"桥接未能启动或已异常退出。端口可能被占用，或启动参数无效。\n\n详情：\n{log_path}", error=True)
        return result[0]
    except Exception:
        if stream:
            traceback.print_exc(file=stream)
        show_message(f"桥接启动失败。\n\n请检查目录权限及日志：\n{log_path}", error=True)
        return 1
    finally:
        stop.set()
        if worker is not None and worker.ident is not None and worker.is_alive():
            worker.join(timeout=15)
        if watcher is not None and watcher.ident is not None:
            watcher.join(timeout=1)
        if owns_instance_file:
            instance_path.unlink(missing_ok=True)
        release_mutex(stop_handle)
        release_mutex(mutex)
        sys.stdout, sys.stderr = old_stdout, old_stderr
        if stream:
            stream.close()


if __name__ == "__main__":
    raise SystemExit(run())

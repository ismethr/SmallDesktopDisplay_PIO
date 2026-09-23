@echo off
setlocal
title SmallDesktopDisplay Windows Bridge

pushd "%~dp0"
set "BRIDGE_PYTHON=%CD%\.venv\Scripts\python.exe"
set "BRIDGE_SCRIPT=%CD%\tools\desktop_display_bridge\desktop_display_bridge.py"

if not exist "%BRIDGE_PYTHON%" (
  echo [ERROR] Python virtual environment was not found:
  echo         %BRIDGE_PYTHON%
  echo.
  echo Run these commands from the repository root first:
  echo   py -3 -m venv .venv
  echo   .\.venv\Scripts\python.exe -m pip install -r tools\desktop_display_bridge\requirements.txt
  goto :failed
)

echo Starting SmallDesktopDisplay bridge (USB port auto-detection enabled)...
echo Keep this window open. Press Ctrl+C to stop the bridge.
echo.
"%BRIDGE_PYTHON%" -B "%BRIDGE_SCRIPT%" %*
set "BRIDGE_EXIT=%ERRORLEVEL%"

if "%BRIDGE_EXIT%"=="0" goto :done
echo.
echo [ERROR] Bridge exited with code %BRIDGE_EXIT%.

:failed
echo.
pause
popd
exit /b 1

:done
popd
exit /b 0

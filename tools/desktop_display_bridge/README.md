# USB 系统状态屏桥接（macOS / Windows）

这个后台只服务第二块 USB 系统状态屏：

- 状态屏不使用 Wi-Fi，后台每秒通过 USB 串口发送 CPU、内存、Codex 周剩余用量、下载和上传速度。
- Codex 登录或网络请求失败时，CPU、内存和网速仍会更新，用量卡片会保留旧值并显示为陈旧；串口重新插入后会自动连接。
- 第一块天气时钟不运行此桥接，也不访问电脑。

CPU、内存和网卡计数统一由 [psutil](https://github.com/giampaolo/psutil) 读取，串口统一由 [pyserial](https://github.com/pyserial/pyserial) 驱动。平台相关部分会自动选择：

| 功能 | macOS | Windows |
| --- | --- | --- |
| 默认网卡 | 系统默认路由接口 | 系统路由探测，必要时回退到 `Get-NetRoute` |
| 串口 | `/dev/cu.usbserial-*` 等 USB 串口 | `COM` 口及 USB VID/描述识别 |
| Codex 用量 | 读取本机 Codex 登录状态 | 读取本机 Codex 登录状态 |

## macOS 安装与运行

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/desktop_display_bridge/requirements.txt
.venv/bin/python tools/desktop_display_bridge/desktop_display_bridge.py
```

只连接一个常见 USB 串口设备时会自动发现；长期运行建议明确指定端口：

```bash
DESKTOP_BRIDGE_SERIAL_PORT=/dev/cu.usbserial-2140 \
  .venv/bin/python tools/desktop_display_bridge/desktop_display_bridge.py
```

## Windows 安装与运行

### 单文件后台 EXE

发布包中的 `SmallDesktopDisplayBridge.exe` 不需要 Python 环境。双击后没有控制台窗口，会自动发现唯一的 CH340/USB 串口并在后台运行；重复启动会提示已有实例。EXE 不包含 Codex 凭据，运行时只读取当前 Windows 用户的 `%USERPROFILE%\.codex\auth.json`。

运行日志位于：

```text
%LOCALAPPDATA%\SmallDesktopDisplay\logs\bridge.log
```

可在浏览器打开 `http://127.0.0.1:8766/health` 检查状态。要停止后台程序，可在任务管理器结束 `SmallDesktopDisplayBridge.exe`，或执行：

```powershell
Get-Process SmallDesktopDisplayBridge -ErrorAction SilentlyContinue | Stop-Process
```

从源码构建单文件 EXE：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools\desktop_display_bridge\requirements-build.txt
.\tools\build_windows_bridge_exe.ps1 -Clean
```

产物位于 `build\windows_bridge_exe\dist\SmallDesktopDisplayBridge.exe`。GitHub Actions 也会生成 `SmallDesktopDisplayBridge-windows-x64` 构建产物。

社区构建目前没有商业代码签名证书，Windows 首次运行时可能显示 SmartScreen 提示。请只使用本仓库源码或 GitHub Actions 生成的文件，并按构建日志中的 SHA-256 校验值核对；来源不明的同名 EXE 不应运行。

### Python/命令行方式

在 PowerShell 中从仓库根目录执行：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools\desktop_display_bridge\requirements.txt
.\.venv\Scripts\python.exe tools\desktop_display_bridge\desktop_display_bridge.py
```

仓库根目录还提供 [`run_windows_bridge.cmd`](../../run_windows_bridge.cmd)。依赖安装完成后可直接双击运行，默认使用 `COM5`；窗口必须保持打开，按 `Ctrl+C` 可停止。也可以先设置 `DESKTOP_BRIDGE_SERIAL_PORT` 环境变量覆盖默认端口。

只有一个 USB 串口适配器时会自动选择。可用以下命令查看端口，并在多个 USB 串口并存时明确指定：

```powershell
.\.venv\Scripts\python.exe -m serial.tools.list_ports -v
.\.venv\Scripts\python.exe tools\desktop_display_bridge\desktop_display_bridge.py --serial-port COM7
```

USB 状态屏不需要入站网络访问，建议使用 `--listen-host 127.0.0.1` 将 HTTP 诊断接口限制为本机。不要在路由器上映射 `8766`。

## 配置

可用环境变量：

- `CODEX_BRIDGE_HOST`、`CODEX_BRIDGE_PORT`、`CODEX_BRIDGE_REFRESH_SECONDS`：与原 Codex 桥接相同。
- `DESKTOP_BRIDGE_SERIAL_PORT`：状态屏固定串口；例如 macOS 的 `/dev/cu.usbserial-2140` 或 Windows 的 `COM7`。
- `DESKTOP_BRIDGE_NETWORK_INTERFACE`：可选网卡名；留空时跟随系统默认路由。
- `DESKTOP_BRIDGE_NIGHT_START_HOUR`、`DESKTOP_BRIDGE_NIGHT_END_HOUR`：夜间节能开始和结束小时，默认 `0` 和 `7`；两者相同表示关闭定时节能。
- `DESKTOP_BRIDGE_DAY_BRIGHTNESS`、`DESKTOP_BRIDGE_NIGHT_BRIGHTNESS`：白天和夜间亮度百分比，默认 `50` 和 `10`。
- `DESKTOP_BRIDGE_OFFLINE_BRIGHTNESS`：连续 4 秒收不到数据后的亮度，默认 `5`。

同名命令行参数优先于环境变量；执行 `desktop_display_bridge.py --help` 可查看完整列表。

诊断接口：

- `/health`：Codex、桌面状态和 USB 连接是否就绪。
- `/v1/desktop-status`：浏览器可读的最新桌面状态，用于排错。
- `/v1/mac-status`：保留的旧版兼容路径，内容与 `/v1/desktop-status` 相同。
- `/v1/codex-usage`：本机 Codex 用量诊断接口。

这些 HTTP 接口没有账号认证，只适合可信家庭局域网。Codex OAuth 令牌只会由原桥接模块发送至经过 TLS 验证的 `chatgpt.com`，不会进入 USB 数据帧或日志。

## USB 协议

串口为 115200 baud，每行一帧：

```text
$MSD3,<序号>,<CPU×10>,<内存×10>,<Codex剩余×10>,<用量陈旧0/1>,<下载B/s>,<上传B/s>,<当前亮度>,<离线亮度>*<CRC16>
```

`MSD3` 将旧协议的温度字段替换为 Codex 周剩余用量，并增加陈旧标志，防止新旧固件误读。CRC 使用 CRC-16/CCITT-FALSE，校验范围是 `$` 之后、`*` 之前的 ASCII 内容。用量尚未取得时发送 `-1`；陈旧标志为 `1` 时屏幕保留百分比但改用灰色。ESP8266 只接受版本、字段数、数值范围和 CRC 均合法的完整帧，连续 4 秒没有合法帧就显示 `USB LOST` 并使用离线亮度。重新收到合法帧后会按电脑当前时段自动恢复亮度。

第二块屏幕固件位于 [`mac_status_display`](../../mac_status_display/README.md)；目录名为兼容已有构建命令而保留。

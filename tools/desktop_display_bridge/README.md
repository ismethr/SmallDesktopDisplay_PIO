# 统一桌面屏幕桥接

这个 macOS 后台把两块屏幕需要的功能合并到同一个进程：

- 第一块天气时钟继续通过局域网访问 `http://<Mac IP>:8766/v1/codex-usage`，接口格式保持不变。
- 第二块状态屏不使用 Wi-Fi，后台每秒通过 USB 串口发送 CPU、内存、CPU 温度、下载和上传速度。
- Codex 登录或网络请求失败不影响 USB 状态屏；USB 拔出也不影响第一块天气时钟。串口重新插入后会自动连接。

CPU、内存和网卡计数由 [psutil](https://github.com/giampaolo/psutil) 读取。Apple Silicon 温度由 [macmon](https://github.com/vladkens/macmon) 的持续 JSON 数据流提供；桥接不会每秒反复启动新进程。

## 安装

```bash
brew install macmon
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/desktop_display_bridge/requirements.txt
```

在仓库根目录运行：

```bash
.venv/bin/python tools/desktop_display_bridge/desktop_display_bridge.py
```

默认自动选择系统的默认路由网卡，避免把 VPN、Wi-Fi 和有线网卡重复相加。只连接一个常见 USB 串口设备时会自动发现；长期运行建议明确指定第二块屏幕的端口：

```bash
DESKTOP_BRIDGE_SERIAL_PORT=/dev/cu.usbserial-2140 \
  .venv/bin/python tools/desktop_display_bridge/desktop_display_bridge.py
```

可用环境变量：

- `CODEX_BRIDGE_HOST`、`CODEX_BRIDGE_PORT`、`CODEX_BRIDGE_REFRESH_SECONDS`：与原 Codex 桥接相同。
- `DESKTOP_BRIDGE_SERIAL_PORT`：第二块屏幕的固定串口路径。
- `DESKTOP_BRIDGE_NETWORK_INTERFACE`：可选网卡名，例如 `en0`；留空时跟随默认路由。
- `DESKTOP_BRIDGE_MACMON`：`macmon` 可执行文件路径，Apple Silicon Homebrew 默认是 `/opt/homebrew/bin/macmon`。

诊断接口：

- `/health`：Codex、状态采集、温度和 USB 连接是否就绪。
- `/v1/mac-status`：浏览器可读的最新 Mac 状态，用于排错。
- `/v1/codex-usage`：第一块屏幕使用的原兼容接口。

这些 HTTP 接口没有账号认证，只适合可信家庭局域网，不要将 `8766` 映射到公网。Codex OAuth 令牌只会由原桥接模块发送至经过 TLS 验证的 `chatgpt.com`，不会进入 USB 数据帧或日志。

## USB 协议

串口为 115200 baud，每行一帧：

```text
$MSD1,<序号>,<CPU×10>,<内存×10>,<温度×10>,<下载B/s>,<上传B/s>*<CRC16>
```

CRC 使用 CRC-16/CCITT-FALSE，校验范围是 `$` 之后、`*` 之前的 ASCII 内容。缺少温度时发送 `-32768`。ESP8266 只接受版本、字段数、数值范围和 CRC 均合法的完整帧，连续 4 秒没有合法帧就显示 `USB LOST`。

第二块屏幕固件位于 [`mac_status_display`](../../mac_status_display/README.md)。

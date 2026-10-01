# USB 桌面状态屏（macOS / Windows）

这是第二块 ESP8266 + 240 × 240 ST7789 屏幕使用的独立固件。它不连接 Wi-Fi，只通过 USB 串口接收 macOS 或 Windows 后台发送的数据，显示：

- CPU 使用率
- 内存使用率
- macOS / Windows CPU 最热点与 GPU 温度（取不到时显示 `--°C`）
- 当前公网出口国家/地区：以小国旗和地区缩写显示，可用于辨认 VPN/代理节点
- ChatGPT/Codex 周剩余用量，以及 Claude 5 小时、每周剩余额度
- 当前默认上网接口的下载、上传速度
- USB 在线/断线状态；电脑校时后标题栏显示本地时间与日期（`20:39 09-30 WED`），校时前显示 `MINIDISPLAY`
- 纯黑卡片与中性灰圆角边界
- CPU 与内存并排大字显示，温度独立成行；Codex 与 Claude 并排显示，底部三栏显示位置、下载和上传
- 大号数值使用 TFT_eSPI 的 26 px Font 4，替代放大两倍的 Font 2，笔画更平滑且更省行高
- 额度卡片带进度条：余量低于 30% 变黄、低于 10% 变红，缓存数据整体灰显；Claude 每周余量数字同样按余量着色
- 只重绘变化的指标，减少刷新闪烁；等待时显示 `--`，额度用 `--` / `LEFT` / `CACHED`（Claude 为 `OLD`）区分缺失、有效与缓存数据
- 自动节能亮度：默认 00:00–07:00 为 10%，白天 50%，数据断开后 5%
- 连续 4 秒无系统数据自动切到原项目完整天气时钟页（原字库、天气图标、中文日期、温湿度和初音动画），重连后恢复状态页面

接线和第一块天气时钟相同：SCK GPIO14、MOSI GPIO13、CS GPIO15、DC GPIO0、RST GPIO2、背光 GPIO5。串口固定为 115200 baud。

## 构建和刷入

在仓库根目录执行：

```bash
pio run -d mac_status_display -e esp12e
# macOS
pio run -d mac_status_display -e esp12e -t upload --upload-port /dev/cu.usbserial-2140
# Windows（按设备管理器中的实际端口修改）
pio run -d mac_status_display -e esp12e -t upload --upload-port COM7
```

协议的本机测试：

```bash
pio test -d mac_status_display -e native_test
```

macOS / Linux 上可在首次固件构建后检查真实绘图代码的布局：

```bash
sh tools/preview_status_screen.sh
```

Windows 安装 Visual Studio Build Tools（C++ 工作负载）后，一条命令即可用 MSVC 运行协议单元测试和布局预览，并把预览转换为 PNG 与总览图 `build/status_preview/contact-sheet.png`：

```powershell
.\tools\test_status_display.ps1
```

预览程序链接固件的真实 `MiniDisplayApp`、页面和国旗源码，以及 TFT_eSPI 实际字库（Font 1、Font 2 与 RLE 编码的 Font 4），全部状态都通过带 CRC 的 USB 帧驱动。它验证文字重叠、画布越界、数值极限、重复帧与同一分钟内不重绘、跨分钟刷新标题时间、低余量配色、断线/午夜/重连、辅助帧不充当心跳、Claude 15 秒过期以及 `millis()` 回绕。它不模拟 SPI、屏幕面板色序或背光，不能代替实机颜色验收；国旗仍沿用已在实机确认的颜色校正。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `src/main.cpp` | 只负责串口读行并把完整行交给应用对象 |
| `include/minidisplay_app.h`、`src/minidisplay_app.cpp` | 状态机：帧分发、状态页/离线页切换、背光、Claude 过期与计时 |
| `include/status_screen.h`、`src/status_screen.cpp` | 在线状态页布局与按字段增量重绘 |
| `include/country_flags.h`、`src/country_flags.cpp` | 出口国旗（颜色已按实机面板校正） |
| `include/offline_screen.h`、`src/offline_screen.cpp` | 断联时钟页；实机上叠加 `offline_weather.h` 的天气、日期和动画 |
| `include/display_theme.h` | 共享配色、字体编号与负载/温度/余量阈值 |
| `include/status_protocol.h`、`offline_clock.h`、`ai_usage_protocol.h` | 统一的 `$…*CRC16` 帧校验与各帧解析、本地日历换算 |
| `include/line_reader.h`、`status_format.h` | 可单测的按行缓冲与数值格式化 |

电脑端对应的帧编码集中在 `tools/desktop_display_bridge/display_protocol.py`。

屏幕不保存账号、Wi-Fi 密码、Codex 数据或公网 IP，只接收统一桥接提供的系统指标、`国家-地区` 短标签、剩余百分比和陈旧标志。`MSD4` 串口帧经过 CRC16、长度、版本、字段数和范围检查；固件也兼容缺少温度与位置字段的 `MSD3` 帧。

桥接每秒追加独立的 `$MSC1,本地午夜以来的秒数*CRC16` 校时帧，范围为 0–86399。
新版还同步 `MSC2` 本地日期时间（本地时间作为 UTC 编码的秒数）与 `MSW1` UTF-8 JSON 天气缓存，均有 CRC16 校验。时钟日期支持 2020–2099 年。天气由电脑每 30 分钟获取，经 USB 发送，断联后保留最后数据并轮播更新时间，小屏不配置 Wi-Fi。
4 秒无有效系统数据后显示时钟并降低背光，重新连接后整屏恢复状态页；单独的校时帧不会掩盖系统数据中断。
小屏必须保持供电；计时不写入闪存，完全断电重启后显示 `--:--`，需重新连接电脑校时。
断联期间依赖 ESP8266 晶振，存在正常漂移；跨午夜与 `millis()` 回绕均可持续计时。

刷机前请完整备份设备原来的 4 MB Flash，并记录备份的 SHA-256，以便需要时恢复。


Claude 通过独立 CRC16 辅助包 `MSA1` 同步 5 小时与每周剩余比例，旧版状态包保持兼容。
15 秒无 Claude 包时清空其数值；仅有额度、时间或天气流量不能维持过期系统状态页。

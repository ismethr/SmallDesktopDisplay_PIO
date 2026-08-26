# SmallDesktopDisplay：天气屏与系统状态屏

本仓库包含两个独立的 ESP8266 NodeMCU + 240 × 240 ST7789 屏幕项目：

- **天气时钟屏（仓库根目录）**：在上游 SmallDesktopDisplay 基础上修改，开机联网同步一次时间和天气，随后关闭 Wi-Fi；显示天气、室外温湿度、AQI、日期和动画。
- **USB 系统状态屏（`mac_status_display/` 兼容目录名）**：本仓库自行开发，通过 USB 接收 macOS 或 Windows 桥接数据，显示 CPU、内存、ChatGPT/Codex 周剩余量和网速，不使用 Wi-Fi。

两个固件互不依赖，也不能刷到同一块屏幕上同时运行。天气屏当前版本为 **SDD 1.8.0**，使用 Arduino 框架和 PlatformIO 构建。

> **修改版声明（2026-08-22）：** 本仓库是在
> [KittenCN/SmallDesktopDisplay_PIO](https://github.com/KittenCN/SmallDesktopDisplay_PIO)
> 基础上继续开发的社区分支；上游又源自
> [chuxin520922/SmallDesktopDisplay](https://github.com/chuxin520922/SmallDesktopDisplay)。
> 仓库保留完整提交历史、原作者与历次贡献者署名。根目录天气屏是社区修改版，不是上游官方版本；`mac_status_display/` 和跨平台主机桥接是本仓库新增的第二套显示系统。

本项目与 OpenAI 不存在隶属、赞助或背书关系。ChatGPT/Codex 名称与图形只用于说明兼容的数据来源和界面功能。

## 功能

- 24 小时时钟、分钟与秒钟显示；NTP 成功后固定显示“月日 + 星期”，不再轮播日期页。
- 时间屏将保存的亮度作为日间亮度；NTP 有效后每天 00:00–07:00 自动降至 10%，早上自动恢复，日间设置低于 10% 时夜间不会反向调亮。
- 自动或手动城市代码，显示启动时获取的天气、室外温度、室外湿度、风向风力、当日高低温和天气图标。
- 温度与湿度各自使用 24 × 24 图标、数值和颜色进度条；天气横幅依次显示天气、AQI、风向、今日天气、最低温和最高温。
- 按中国 AQI 指数的六级边界显示空气质量；AQI 缺失时明确显示“未知”。
- 右下角 JPEG 帧动画，内置太空人、胡桃和初音未来三套资源，也可完全关闭。
- 可选 DHT11 室内温湿度显示；读数无效时保留上一帧而不绘制 `NaN`。
- WiFiManager Web 配网，连接失败时开启限时配置门户；也可编译为 SmartConfig 模式。
- 可选的局域网管理页；默认关闭，因为启用它会让天气屏保持 Wi-Fi 在线。
- 第二块 USB 系统状态屏使用纯黑无填充卡片和中性灰虚线边界显示 CPU、内存、ChatGPT/Codex 周剩余用量及默认网卡实时速度，并按夜间/离线状态自动降低背光。
- 配置持久化：亮度、旋转、城市、DHT 开关和 Wi-Fi 凭据。
- 串口配置与诊断：状态、空闲堆、最大连续堆块、碎片率、亮度、城市、方向、手动联网刷新与重启。
- 默认仅在启动和手动执行 `0x08` 时联网；NTP 与天气请求完成后关闭 Wi-Fi 射频，不需要电脑常驻运行桥接。
- 天气横幅按实际字体宽度分段横移；固定日期行不会切换，上游返回字库未覆盖字符时显示 `-`。

## 硬件与接线

默认目标板是 `nodemcuv2`（ESP-12E），屏幕驱动及引脚已经写在 `platformio.ini`，不需要修改下载到 `.pio` 中的 TFT_eSPI 文件。

| 功能 | NodeMCU | GPIO |
| --- | --- | ---: |
| TFT SCK | D5 | 14 |
| TFT MOSI | D7 | 13 |
| TFT CS | D8 | 15 |
| TFT DC | D3 | 0 |
| TFT RST | D4 | 2 |
| TFT 背光 | D1 | 5 |
| 按钮 | D2 | 4 |
| DHT11（可选） | D6 | 12 |

屏幕配置为 ST7789_2、240 × 240、27 MHz SPI。按钮单击在 25% / 50% / 75% / 100% 日间亮度之间循环，长按清除 Wi-Fi 配置并重启。夜间实际亮度由节能规则限制为不高于 10%。

## 首次启动与配网

1. 固件先读取经过边界校验的 EEPROM 配置。Wi-Fi 数据使用版本号和 CRC16 检查；旧版 32 + 64 字节布局仍可读取，并在下次成功连接后迁移。
2. 设备尝试连接已保存的网络。默认 Web 配网模式下，失败后开启 `SmallDisplay-<芯片ID>` 配置热点。
3. 配置门户最多开放 180 秒，可设置城市代码、亮度、屏幕方向和可选 DHT11。
4. 联网后依次同步 NTP 与天气，然后关闭 Wi-Fi 射频；需要再次更新时通过串口发送 `0x08`。
5. 城市代码填 `0` 时通过 IP 自动识别；也可填写 `101xxxxxx` 格式的 9 位 weather.com.cn 城市代码。

配置门户超时或上游不可用时，设备继续以离线界面运行，不会无限卡在主循环中。

## 局域网管理页

默认构建不启用管理页。只有将 `LAN_ADMIN_ENABLED` 编译为 `1` 后，设备才会保持 Wi-Fi 在线并可通过以下任一地址访问：

```text
http://<时钟的局域网IP>/
http://smalldisplay-<芯片ID>.local/
```

串口发送 `0x00` 可查看当前 IP。管理页支持修改城市代码、亮度和屏幕方向，还可立即刷新或重启设备。保存操作会先完整校验所有参数，再一次性写入 EEPROM；任何参数无效时都不会保存部分设置。

管理页不显示 Wi-Fi 密码，修改请求带有每次启动随机生成的页面令牌，并拒绝来自设备当前子网以外的客户端。它不提供用户账号认证，只适合可信家庭局域网；不要在路由器上把设备的 `80` 端口映射到公网。

## 数据源与安全边界

- 时间：`ntp.aliyun.com`、`ntp.tencent.com`、`pool.ntp.org`。固件验证响应来源、NTP 模式、stratum、时间范围和请求 cookie，不再接受明文 HTTP 时间降级。
- 天气与城市：weather.com.cn 的 HTTP 接口。该接口不提供 TLS，因此天气数据没有传输完整性保证；失败或格式变化时保留已有画面。
- 系统状态屏：[`tools/desktop_display_bridge`](tools/desktop_display_bridge/README.md) 使用 psutil 读取 CPU、内存与默认网卡计数，并通过本机 Codex 登录凭据获取周剩余用量。第二块屏幕只接收经过 CRC16 校验的 USB 串口帧，不使用 Wi-Fi，也不会收到 OAuth 令牌。
- 局域网管理页只接受设备当前子网内的连接，并使用页面令牌防止跨站表单提交；它没有账号认证，不应暴露到公网或不受信任的局域网。
- Wi-Fi 密码存储于 ESP8266 EEPROM 模拟区，未做静态加密；具备芯片物理访问能力的人员仍可能读取。
- 配网热点当前不设密码，但使用设备唯一 SSID 且仅在连接失败时限时开放。请在可信环境中完成首次配置。

## 构建、上传与串口

安装 [PlatformIO](https://platformio.org/) 后在仓库根目录执行：

```powershell
pio run -e esp12e
pio run -e esp12e -t upload
pio device monitor -b 115200
```

所有直接依赖均在 `platformio.ini` 精确锁定。正常构建会生成：

- `.pio/build/esp12e/firmware.bin`：常规 ESP8266 上传镜像；
- `.pio/build/esp12e/firmware.elf`：符号和调试信息；
- `.pio/build/esp12e/firmware.hex`：由 `extra_script.py` 额外生成，供需要 Intel HEX 的烧录流程使用。

当前默认构建使用 39,276 B RAM（47.9%）和 763,552 B 应用分区 Flash（73.1%）。CI 对默认 `firmware.bin` 设置 925,000 B 上限；新增图片、字体或依赖前仍须重新检查容量。

固件内的 JPEG 全部来自 `PROGMEM` 数组，因此项目固定使用 `lib/TJpg_Decoder_ArrayOnly`：Tiny JPEG 解码核心与上游 1.1.0 字节一致，只移除了从未使用的 LittleFS/SPIFFS/SD 文件接口。离线文件系统并不是本项目的固件功能。

## Windows 系统状态桥接 EXE

第二块 USB 系统状态屏可使用无控制台窗口的单文件 `SmallDesktopDisplayBridge.exe`。双击后自动在后台运行，日志写入 `%LOCALAPPDATA%\SmallDesktopDisplay\logs\bridge.log`；程序不打包或复制 Codex 凭据。构建命令：

```powershell
.\.venv\Scripts\python.exe -m pip install -r tools\desktop_display_bridge\requirements-build.txt
.\tools\build_windows_bridge_exe.ps1 -Clean
```

本地产物位于 `build\windows_bridge_exe\dist`，GitHub Actions 产物名为 `SmallDesktopDisplayBridge-windows-x64`。配置、停止方式和 Python 运行方式见 [`tools/desktop_display_bridge/README.md`](tools/desktop_display_bridge/README.md)。

## macOS 系统状态桥接 App

第二块 USB 系统状态屏也提供无终端窗口、单实例和本地日志的 `SmallDesktopDisplayBridge.app`。它保持固件现有 `MSD3 + CRC16` 协议不变，自动发现 CH340 串口，采集 CPU、内存和默认网卡速度，并优先通过 Codex 官方 App Server 获取周剩余量。Intel 黑苹果使用 `macos-x86_64` 构建：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/desktop_display_bridge/requirements-build.txt
MACOS_BRIDGE_ARCH=x86_64 ./tools/build_macos_bridge_app.sh
```

本地构建产物与 GitHub Actions 的 `SmallDesktopDisplayBridge-macos-x86_64` 均包含可直接运行的 App。日志、登录启动与首次打开方式见 [`tools/desktop_display_bridge/README.md`](tools/desktop_display_bridge/README.md)。

## Windows 原生界面模拟器

仓库提供可交互的 Windows x64 原生模拟器，无需连接 ESP8266 即可预览天气屏。模拟器直接复用天气/温湿度 JPEG、三套动画、LineAtom 数字字模、VLW 中文字体以及 AQI/温湿度边界逻辑；右侧控制面板可调整温度、湿度和 AQI。

已安装 Visual Studio 的“使用 C++ 的桌面开发”工作负载后，在仓库根目录执行：

```powershell
.\tools\build_simulator.ps1 -Clean -Run
.\tools\test_simulator.ps1 -SkipBuild
.\tools\package_simulator.ps1 -SkipBuild
```

可执行文件位于 `build\simulator\bin\Release`，便携 ZIP 位于 `build\packages`。解压便携包后双击 `SmallDesktopDisplaySimulator.exe` 即可；`SDL3.dll` 需与 EXE 保持在同一目录。完整按键、命令行和模拟边界见 [`simulator/README.md`](simulator/README.md)。

模拟器用于界面与资源回归，不模拟真实 Wi-Fi、HTTP/TLS、NTP、EEPROM/SPI/DHT 时序、背光 PWM 或 ESP8266 内存压力，因此不能替代真机验收。

## 编译选项

编辑 `src/config.h`：

| 选项 | 默认值 | 含义 |
| --- | ---: | --- |
| `ANIMATION_CHOICE` | `3` | `0` 关闭，`1` 太空人，`2` 胡桃，`3` 初音未来 |
| `WEB_CONFIG_ENABLED` | `1` | `1` 使用 WiFiManager，`0` 使用 SmartConfig |
| `DHT_ENABLED` | `0` | 是否编译 DHT11 室内温湿度功能 |
| `LAN_ADMIN_ENABLED` | `0` | `1` 会保持 Wi-Fi 在线并提供可选局域网管理页 |
| `NIGHT_DIM_START_HOUR` | `0` | 时间屏夜间节能开始小时 |
| `NIGHT_DIM_END_HOUR` | `7` | 时间屏夜间节能结束小时；与开始值相同可关闭定时调暗 |
| `NIGHT_DIM_BRIGHTNESS` | `10` | 夜间亮度上限；不会把更低的日间设置调亮 |

## 串口命令

串口速率为 115200。设置类命令既支持交互式两步输入，也支持单行 `命令 参数`、`命令=参数` 或 `命令:参数`。

| 命令 | 参数/行为 |
| --- | --- |
| `0x00` | 输出版本、运行时间、空闲堆、Wi-Fi、城市和配置摘要 |
| `0x01 50` | 设置日间亮度 `0-100`；夜间自动应用节能上限 |
| `0x02 101020200` | 城市代码；`0` 为自动识别 |
| `0x03 0` | 屏幕方向 `0-3` |
| `0x05` | 清除 Wi-Fi 配置并重启 |
| `0x07` | 临时联网并执行 NTP 同步 |
| `0x08` | 临时联网并刷新 NTP 与天气，完成后再次关闭 Wi-Fi |
| `0x99` | 重启设备 |

无效数字、越界值和附带垃圾字符的参数都会被拒绝；例如 `abc` 不会再被当成合法的 `0`。

## 测试与验证

执行 Python 工具测试：

```powershell
python -B -m unittest discover -s tools/font_translate/tests -v
python -B tools/codex_usage_bridge/test_codex_usage_bridge.py -v
python -B tools/desktop_display_bridge/test_desktop_display_bridge.py -v
python -B -m unittest discover -s test/host -v
```

安装了本机 C++ 编译器时，可实际执行不依赖硬件的显示逻辑断言：

```powershell
pio test -e native_test
pio test -d mac_status_display -e native_test
```

Windows 模拟器还提供 MSVC `/W4 /WX` 构建、真实 JPEG 解码、固定场景 RGB565 哈希和便携包自检：

```powershell
.\tools\test_simulator.ps1
```

在没有连接开发板时，编译 Unity 测试固件但不上传、不等待串口：

```powershell
pio test -e esp12e_test --without-uploading --without-testing
```

编译可选功能矩阵：

```powershell
pio run -e esp12e_smartconfig
pio run -e esp12e_dht
pio run -e esp12e_no_animation
pio run -e esp12e_astronaut
pio run -e esp12e_hutao
pio run -e esp12e_admin
```

连接硬件后可去掉 `--without-testing` 并指定上传/测试串口，以实际执行 Unity 断言。发布前还应在真机检查屏幕颜色和旋转、背光、配网/重置、天气和 NTP 失败恢复、DHT、按钮，以及至少 24 小时的空闲堆和重连稳定性。本仓库的自动验证只能证明编译、纯逻辑边界和工具行为，不能替代真机验收。

## 字体与动画工具

- [`tools/font_translate`](tools/font_translate/README.md)：安全解析 C 字节数组、提取字模、按字符清单无损精简 VLW/HZK 字体及字符去重。
- [`src/Animate`](src/Animate/README.md)：从 GIF 确定性生成 JPEG 帧头文件；帧数和尺寸表由代码自动校验。

## 目录

```text
src/SmallDesktopDisplay.cpp  上游衍生天气屏的主程序、显示、网络与配置流程
src/core/                    可独立测试的边界与显示逻辑
src/Animate/                 动画播放器、资源和生成工具
src/weatherNum/              天气代码到图标的映射
src/font/                    自定义字体资源
lib/TJpg_Decoder_ArrayOnly/  固定版本的 PROGMEM 数组 JPEG 解码器
simulator/                    Windows 原生界面模拟器、场景与确定性测试
test/test_display_logic/     Unity 边界测试
test/host/                    固件证书与静态资源完整性测试
tools/font_translate/        字体转换工具及 Python 单元测试
tools/codex_usage_bridge/    系统状态屏使用的本机 Codex 周用量读取模块
tools/desktop_display_bridge/macOS/Windows 系统采集与 USB 状态传输
mac_status_display/          自研 USB 系统状态屏固件（保留早期兼容目录名）
tools/*_simulator.ps1        Windows 模拟器构建、测试与便携打包脚本
```

## 许可证

本项目使用 [GNU Affero General Public License v3](LICENSE)。

修改、再发布或通过网络提供本项目服务时，请保留版权、修改声明、许可证与无担保说明，并按 AGPL-3.0 提供对应源代码。第三方组件、图形、字体与动画的来源及额外权利说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；AGPL-3.0 不授予 OpenAI 或其他权利人的商标使用权。

24 × 24 ChatGPT 结形图标由 Peter Steinberger 的
[CodexBar](https://github.com/steipete/CodexBar) 同比例栅格化；CodexBar 以
[MIT License](https://github.com/steipete/CodexBar/blob/main/LICENSE) 发布。

安全问题请按 [SECURITY.md](SECURITY.md) 私下报告；贡献代码或素材前请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

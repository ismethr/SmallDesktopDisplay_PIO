# Changelog

## Unreleased

- 天气屏改为每 30 分钟自动唤醒 Wi-Fi，同步 NTP 与天气后再次关闭射频；保留开机刷新和串口 `0x08` 立即刷新，并覆盖 `millis()` 回绕边界。
- 修复 Codex App Server 返回多个限额桶时可能误选未使用的模型专属周窗口、导致状态屏恒显 100% 的问题；主 Codex 周额度现在严格使用兼容主桶，并以只读、禁止审批模式启动本机 App Server。

## 1.8.0 - 2026-08-26

- 桌面状态桥接新增 Windows 支持：自动识别 `COM` USB 串口，并按默认 IPv4 路由选择网卡；现有 macOS 行为保持兼容。
- 固定 Tiny JPEG 解码器核心文件的 LF 字节格式，并在 Windows CI 执行桥接与资源完整性测试，避免 `core.autocrlf` 造成哈希误报。
- 新增第二块 ESP8266 的 USB 桌面状态屏固件，显示 CPU、内存、ChatGPT/Codex 周剩余用量和默认网卡实时速度，不保存 Wi-Fi 配置。
- 将 Codex 用量读取模块集成到跨平台系统状态桥接，每秒通过带 CRC16 的 `MSD3` 串口协议驱动状态屏；用量缺失和陈旧状态均有明确表示。
- 新增 USB 自动重连、断线提示、默认路由网卡选择、诊断接口及 Python/C++ 自动测试。
- 状态屏改为纯黑无填充卡片和中性灰虚线边界，并新增默认 00:00–07:00 夜间调暗、数据断开调暗和恢复连接自动恢复亮度；三个亮度值及夜间时段均可由统一后台配置。
- 天气时间屏同步采用 00:00–07:00 夜间 10% 亮度上限；NTP 未就绪时保持日间亮度，管理页区分保存的日间亮度和当前实际亮度。
- 天气屏移除 Codex 用量、桥接地址和第七个横幅，恢复室外湿度图标、数值、进度条及原六项天气横幅。
- 天气屏默认改为只在启动或串口手动刷新时联网，完成 NTP/天气请求后关闭 Wi-Fi；局域网管理页改为显式可选构建。
- Windows 天气屏模拟器同步恢复湿度控制和湿度资源，项目说明明确区分上游衍生天气屏与自研 USB 系统状态屏。
- 新增无控制台窗口、单实例和本地日志的 Windows 单文件桥接 EXE 入口、可重复 PyInstaller 构建脚本及 GitHub Actions 构建产物。
- 新增可双击无窗口、无 Dock 图标后台运行、单实例和本地日志的 macOS App 打包及 GitHub Actions 产物，支持 Intel 黑苹果 `x86_64` 构建且保持现有 `MSD3` 固件协议不变。
- 新增 Apple Silicon `arm64` 原生 macOS App 构建，并自动将 Intel、Apple Silicon 和 Windows 桥接程序发布到带版本号的 GitHub Release。
- Codex 周用量默认优先通过官方本机 App Server 的 `account/rateLimits/read` 获取，不再要求桥接直接解析 OAuth 凭据；旧客户端仍可自动兼容。

## 1.6.3 - 2026-08-22

- 新增 Windows x64 原生界面模拟器，直接复用固件的 RGB565 显示逻辑、字体、天气图标、温湿度图标和三套动画资源。
- 新增交互式天气/DHT/AQI/亮度/旋转/动画控制、无窗口截图与内置自检。
- 新增固定场景渲染哈希、资源解码和边界测试，以及独立 Windows CI 构建与便携 ZIP 打包。
- 新增 PowerShell 一键构建、测试、打包脚本和中文安装使用说明；模拟器明确不替代网络、外设时序与真机验收。
- 日期区固定显示“月日 + 星期”，移除不可用的农历轮播与相关网络请求；补齐当前城市和 Codex 中文横幅字形。
- 原室外湿度区域改为 ChatGPT 图标、Codex 周剩余额度进度条和百分比，并增加距离重置时间横幅。
- 新增最小化的本机 Codex 用量桥接；令牌仅发送到 TLS 验证的 `chatgpt.com`，局域网响应不会包含令牌、本机路径或详细诊断。
- 新增 Codex 配置的 Web/串口设置与 EEPROM 持久化、边界测试和 CI 覆盖。
- 补充修改版声明、AGPL-3.0 再发布要求、第三方资源说明、贡献指南、安全报告流程和 OpenAI 非关联声明。
- 移除构建配置中的个人绝对包路径，恢复为可公开复现的 PlatformIO 官方依赖解析。

## 1.5.3 - 2026-08-04

### Changed

- Reduced the calendar VLW font from 245 to the exact 100-glyph runtime manifest while preserving every retained metric, bitmap, footer byte, spacing metric, and replacement glyph. The font payload fell from 69,384 B to 32,900 B.
- Replaced dynamically constructed clock line atoms with byte-identical POD tables in `PROGMEM`; all 1,333 drawing triples are protected by a canonical SHA-256 regression test.
- Pinned an array-only TJpg_Decoder 1.1.0 variant. The Tiny JPEG core is byte-identical to upstream, while unused LittleFS/SPIFFS/SD members, overloads, and dependencies are no longer linked.
- Stopped forcing unused newlib float `printf`/`scanf` implementations into the image; actual references would still be resolved normally.
- Removed dead lunar snapshot strings, duplicate JSON helper implementations, duplicate JPEG setup calls, unused banner state, and repeated serial log-prefix template bodies.

### Added

- Added strict VLW parser limits matching TFT_eSPI, an explicit calendar glyph manifest, exact font/clock asset checks, and validation for all 71 bundled JPEG frames.
- Added compile targets for the astronaut and Hu Tao animation variants and a 925,000 B default `firmware.bin` CI budget.

### Validation

- Default ESP8266 build changed from 983,012 B Flash / 47,624 B RAM to 907,464 B Flash / 41,760 B RAM: 75,548 B less Flash and 5,864 B less static RAM, with EEPROM, display, network, TLS, serial, button, and feature semantics unchanged.

## 1.5.2 - 2026-08-03

### Fixed

- Accepted decimal live temperatures returned by weather.com.cn (for example `33.9`) instead of rejecting the complete weather snapshot and leaving the top, temperature, and humidity areas blank.
- Made live weather text, weather code, city name, wind, and AQI resilient to optional-field variation while retaining strict validation for required numeric data.
- Replaced the disabled-by-default TianAPI TLS path with validation against the bundled DigiCert Global Root G2 and explicitly supplied the synchronized TimeLib UTC time to BearSSL; an explicitly configured leaf fingerprint still overrides the trust anchor.
- Removed the misleading `TLS未开` display state. A configured API key now attempts secure root-validated TLS and preserves the previous snapshot on failure.
- Accepted both padded Gregorian dates and TianAPI's documented non-padded lunar `YYYY-M-D` dates without weakening calendar-day validation.

### Added

- Added native decimal-temperature regression coverage and a SHA-256 integrity test for the bundled official TLS root certificate.
- Added field-specific serial diagnostics for rejected weather responses.
- Added a font-verified `WEATHER WAIT` first-start banner and detailed HTTPS failure diagnostics.

## 1.5.1 - 2026-08-03

### Fixed

- Fixed blank calendar frames after the lunar summary by selecting only non-empty carousel pages; the weather carousel now uses the same bounded state machine.
- Added width-aware horizontal paging for calendar and weather banners so valid long text is no longer clipped by the 150-pixel viewport.
- Rejected partial or incorrectly typed TianAPI results before updating any lunar display field, while keeping `jieqi` optional as documented by the provider.
- Preserved the last complete lunar snapshot across network, JSON, business-code, and date-format failures.
- Reported distinct unconfigured-key, unconfigured-TLS, and temporarily unavailable lunar states using only glyphs present in the compact calendar font.
- Rendered lunar months numerically, with `L` for leap months, avoiding missing 正/腊/闰 glyphs in the compact font.
- Rejected malformed weather values and missing/incorrectly typed required weather fields before drawing; wind remains an optional all-or-nothing page.
- Replaced unsupported dynamic weather glyphs with a visible marker instead of silently dropping them.
- Fetched lunar data during the first connected startup cycle instead of waiting for the next periodic refresh.
- Handled every TFT sprite allocation failure without drawing through a null buffer; banner failures also preserve carousel state.

### Changed

- Replaced rapid TianAPI retry behavior with bounded exponential backoff from 1 to 16 minutes.
- Split periodic NTP, weather, and lunar work into cooperative stages so UI tasks get control between blocking network operations.
- Rebuild calendar `String` pages only when the date, time status, or lunar snapshot changes; replaced the per-refresh weekday `String` array with static text.
- Released the DHT smooth font after each completed sensor frame to reduce retained heap pressure.
- Added maximum-heap-block and heap-fragmentation diagnostics to serial status output.
- Added executable native display-logic tests plus carousel, marquee, weather-range, and VLW glyph-coverage cases.
- Updated the official checkout and Python setup CI actions to their Node 24 releases.

## 1.5.0 - 2026-08-02

### Added

- Device status (`0x00`) and immediate network refresh (`0x08`) serial commands.
- Persistent weather interval and safe one-line forms for every parameterized serial command.
- CRC16/version validation for the legacy-compatible Wi-Fi EEPROM layout.
- Compile-only Unity tests for display/configuration boundary logic and a feature build matrix.
- GitHub Actions coverage for clean Linux builds, feature variants, tests, and repository hygiene.
- Deterministic, current-Pillow animation generator and strict font conversion test suite.

### Fixed

- Declared and pinned every direct PlatformIO dependency and moved the complete TFT setup into project build flags.
- Bounded Wi-Fi, HTTP, configuration portal, and NTP waits; unified periodic network ownership and radio sleep cleanup.
- Removed plaintext Wi-Fi password/API-key logs and insecure HTTP time fallback.
- Replaced hand-written HTTPS response parsing with `HTTPClient`; TianAPI now fails closed unless a TLS fingerprint is configured.
- Validated NTP source, mode, stratum, timestamp range, and request cookie.
- Rejected incomplete weather responses before drawing; fixed 100% humidity, negative temperature bars, three-digit weather codes, and AQI thresholds.
- Made brightness endpoints persistent, validated every Web/serial setting, and repaired immediate city switching.
- Made Web-disabled, DHT-enabled, and animation-disabled builds compile again.
- Fixed animation PROGMEM access, frame-count drift, and two out-of-bounds astronaut frame lengths.
- Fixed Linux case-sensitive `weatherNum.h` include.

### Changed

- Button click now cycles brightness instead of rebooting; long click still resets Wi-Fi.
- Default weather interval is 10 minutes and is restored from EEPROM when valid.
- Removed obsolete test sketches, empty Wi-Fi module, broken Wokwi placeholders, downloaded dependencies, CMake scratch trees, and machine-specific editor files.
- Repository-generated dependencies and build products are ignored rather than committed.

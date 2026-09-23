# MiniDisplay Bridge 1.12 · Windows 使用说明

双击安装包，按向导完成安装。无需 Python；支持 Windows 10/11 x64。
安装时可选桌面快捷方式、登录 Windows 后自动运行。默认按当前用户安装，无需管理员。

启动后自动打开连接面板。关闭浏览器后，程序仍在右下角托盘运行。
右键托盘可打开设置、查看日志、启动温度采集或退出。再次双击程序会打开已有实例的面板。

## Windows 温度

1. 首次使用可从托盘“下载温度驱动 PawnIO”进入官方发布页，安装 PawnIO 后继续。安装包不会静默安装系统驱动。
2. 右键托盘，选择“启动温度采集（管理员）”，接受 Windows 权限请求。安装包已包含 LibreHardwareMonitor 0.9.6 库和本项目的只读采集程序。
3. 温度通常会在 10 秒内更新。退出 Bridge 时采集程序一并退出；下次启动 Bridge 后，可从托盘重新开启采集。

只读采集接口固定为 `http://127.0.0.1:18765/sensors`，不启用 LibreHardwareMonitor GUI 的 Remote Web Server，也没有风扇控制功能。
也兼容旧版 LibreHardwareMonitor / OpenHardwareMonitor 的 WMI，以及 NVIDIA 驱动自带的 nvidia-smi。
硬件/驱动未提供的传感器仍显示 `--`，不会用主板 ACPI 温度冒充 CPU 温度。

## 断联时钟

需要同时更新第二块 USB 系统状态屏的固件。桥接每秒通过 USB 同步电脑本地时间。
连续 4 秒无系统数据后，小屏自动切到原项目的完整页面：白色小时、黄色分钟、右侧小秒数、天气图标、城市/AQI、室外温湿度、中文日期和初音动画；重连后恢复 CPU/内存/温度/网速页面。
电脑每 30 分钟更新天气，并保存在本机；USB 每 30 秒同步缓存。默认按电脑网络位置识别城市，可在面板修改天气城市代码。断联后保留最后一次天气，轮播显示更新时间，日期和动画继续运行。小屏无需 Wi-Fi。
断联期间按“断线亮度”显示。天气缓存在小屏 RAM 中，完全断电后也需要电脑重新同步。

小屏必须持续供电。拔掉唯一供电的 USB 线后无法显示；完全断电重启后显示 `--:--`，需重新连接电脑校时。
离线时间由 ESP8266 计时，会有晶振误差；重连会校正。新时钟数据包不会影响旧版状态屏。

## 连接和维护

- 只有一个 USB 串口时自动识别。多个串口时，在面板的“显示屏设置”选择状态屏端口。
- 不要选择天气屏。烧录前先从托盘退出桥接，释放串口。
- 设置：`%LOCALAPPDATA%\SmallDesktopDisplay\settings.json`
- 日志：`%LOCALAPPDATA%\SmallDesktopDisplay\logs\bridge.log`
- 升级安装保留设置；可从 Windows“已安装的应用”卸载。
- 需要手动停止时：`SmallDesktopDisplayBridge.exe --stop`

项目源码与版本记录：https://github.com/ismethr/SmallDesktopDisplay_PIO
传感器组件源码：https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/v0.9.6
本程序遵循仓库的 AGPL-3.0 许可。随安装包一并分发对应源码；第三方许可见安装目录的 `licenses`。
本社区构建未做商业代码签名。

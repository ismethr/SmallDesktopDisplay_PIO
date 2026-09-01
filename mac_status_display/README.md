# USB 桌面状态屏（macOS / Windows）

这是第二块 ESP8266 + 240 × 240 ST7789 屏幕使用的独立固件。它不连接 Wi-Fi，只通过 USB 串口接收 macOS 或 Windows 后台发送的数据，显示：

- CPU 使用率
- 内存使用率
- macOS CPU 最热点与 GPU 温度（取不到时显示 `--°C`）
- 当前公网出口国家/地区：以小国旗和地区缩写显示，可用于辨认 VPN/代理节点
- ChatGPT/Codex 周剩余用量、进度条和陈旧状态
- 当前默认上网接口的下载、上传速度
- USB 在线/断线状态
- 纯黑无填充卡片与中性灰虚线边界
- 紧凑双行系统卡片：负载、温度、进度条在同一区域对齐显示；底部三栏显示位置、下载和上传
- 自动节能亮度：默认 00:00–07:00 为 10%，白天 50%，数据断开后 5%

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

屏幕不保存账号、Wi-Fi 密码、Codex 数据或公网 IP，只接收统一桥接提供的系统指标、`国家-地区` 短标签、剩余百分比和陈旧标志。`MSD4` 串口帧经过 CRC16、长度、版本、字段数和范围检查；固件也兼容缺少温度与位置字段的 `MSD3` 帧。数据中断后保留最后一组数值、显示 `USB LOST` 并降低背光，重新连接后由统一后台按当前时段自动恢复。

刷机前请完整备份设备原来的 4 MB Flash，并记录备份的 SHA-256，以便需要时恢复。

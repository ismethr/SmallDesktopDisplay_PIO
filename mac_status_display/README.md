# USB Mac 状态屏

这是第二块 ESP8266 + 240 × 240 ST7789 屏幕使用的独立固件。它不连接 Wi-Fi，只通过 USB 串口接收 Mac 后台发送的数据，显示：

- CPU 使用率
- 内存使用率
- CPU 温度
- 当前默认上网接口的下载、上传速度
- USB 在线/断线状态

接线和第一块天气时钟相同：SCK GPIO14、MOSI GPIO13、CS GPIO15、DC GPIO0、RST GPIO2、背光 GPIO5。串口固定为 115200 baud。

## 构建和刷入

在仓库根目录执行：

```bash
pio run -d mac_status_display -e esp12e
pio run -d mac_status_display -e esp12e -t upload --upload-port /dev/cu.usbserial-2140
```

协议的本机测试：

```bash
pio test -d mac_status_display -e native_test
```

屏幕不保存账号、Wi-Fi 密码或 Codex 数据。串口帧经过 CRC16、长度、版本、字段数和范围检查；拔掉 USB 后保留最后一组数值并显示 `USB LOST`，重新插入后由统一后台自动恢复。

刷机前请完整备份设备原来的 4 MB Flash，并记录备份的 SHA-256，以便需要时恢复。

# MiniDisplay Bridge 图标

`MiniDisplayBridgeIcon.png` 是 1024 × 1024 的透明底主图，`MiniDisplayBridgeIcon.icns` 是 macOS App 使用的多尺寸资源。重新生成 `.icns`：

```bash
swift tools/build_macos_icon.swift \
  tools/desktop_display_bridge/assets/MiniDisplayBridgeIcon.png \
  tools/desktop_display_bridge/assets/MiniDisplayBridgeIcon.icns
```

图标由 OpenAI 内置图像生成工具原创生成，再做透明背景和小尺寸可读性处理。设计表达黑色硬件小屏、青色状态曲线、绿色指标条与 USB-C 桥接，不包含文字、商标或第三方图标。

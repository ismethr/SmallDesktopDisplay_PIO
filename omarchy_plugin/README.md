# MiniDisplay · Omarchy 状态栏插件

[Omarchy](https://omarchy.org/) shell 的状态栏小部件（id `minidisplay.bridge`），用于查看和控制 Linux 上的 MiniDisplay Bridge。

- 状态栏图标：USB 已连接为常规色；桥接在运行但小屏未连上时高亮；桥接未运行时显示灰色的断开图标。悬停显示 CPU、内存、温度和 Codex 摘要。
- 左键：打开面板，显示连接状态、CPU/内存、CPU/GPU 温度、网速、出口位置、Codex/Claude 余量。
- 面板中可以调节小屏日间/夜间亮度、开关 Claude 额度，用开关启停 systemd 服务，也可以重连、重启服务、打开状态页或日志。
- 中键：在浏览器打开 `http://127.0.0.1:8766/`。
- 快捷键（面板打开时）：`R` 重连，`O` 打开状态页，`Esc` 关闭。

## 安装

```bash
tools/linux_bridge.sh install        # 安装桥接服务，检测到 Omarchy 时一并安装插件
tools/linux_bridge.sh plugin         # 只安装/更新插件
tools/linux_bridge.sh plugin-remove  # 卸载插件
omarchy bar move minidisplay.bridge --section right   # 调整位置
```

插件会复制到 `~/.config/omarchy/plugins/minidisplay.bridge/`。更新后如果界面没有变化，执行 `omarchy restart shell`。

## 组成

| 文件 | 作用 |
| --- | --- |
| `manifest.json` | 插件清单（`bar-widget`），可以在设置里改刷新间隔 `pollSeconds` |
| `Panel.qml` | 状态栏按钮和弹出面板 |
| `scripts/minidisplayctl` | 只用 Python 标准库：读取 `/v1/overview`、`/v1/settings`，带令牌写入设置，调用 `systemctl --user` |

IPC：`omarchy-shell minidisplay.bridge open|close|toggle|reconnect|current`。

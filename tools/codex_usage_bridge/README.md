# Codex 用量桥接

这个小程序只做一件事：读取本机 Codex 已有的登录凭据，获取订阅额度窗口，向同一局域网内的桌面时钟提供不含令牌的 JSON。

```bash
python3 codex_usage_bridge.py --once
python3 codex_usage_bridge.py
```

桥接会正常验证 HTTPS 证书。Python.org 的 macOS Python 若没有可用的系统 CA 路径，会自动使用已安装的 `certifi`；若两者都不可用，请先修复 Python 证书环境，程序不会通过关闭证书校验来绕过问题。

默认监听：

```text
http://<Mac局域网IP>:8766/v1/codex-usage
```

在时钟的 Web 配网页面把“Codex桥接地址”填写为 Mac 的局域网 `IP:8766`；也可以通过串口发送 `0x09 192.168.1.10:8766`。发送 `0x09 off` 可关闭该功能。

设备得到的内容只有已用百分比、剩余百分比和重置时间。`~/.codex/auth.json` 中的 OAuth 令牌不会发给设备，也不会写入日志。

桥接服务没有用户认证，只应在可信局域网内运行；不要把 `8766` 端口映射到公网。局域网响应会把详细错误统一为 `unavailable` 或 `stale`，不会泄露凭据文件路径或上游诊断信息。OAuth 令牌只允许发送至经过 HTTPS 验证的 `chatgpt.com` 域名；明文 HTTP 仅允许用于本机回环测试。

环境变量：

- `CODEX_BRIDGE_HOST`：监听地址，默认 `0.0.0.0`。
- `CODEX_BRIDGE_PORT`：监听端口，默认 `8766`。
- `CODEX_BRIDGE_REFRESH_SECONDS`：刷新周期，默认 300 秒，最小 60 秒。

此功能使用 ChatGPT 客户端内部的 Codex 用量接口，而不是公开的 OpenAI API 用量接口；上游格式变化时只需更新这个桥接程序，不需要重新设计设备端界面。

OpenAI、ChatGPT、Codex 及相关标识是其各自权利人的商标或标识。本项目是独立的社区项目，与 OpenAI 不存在隶属、赞助或背书关系。

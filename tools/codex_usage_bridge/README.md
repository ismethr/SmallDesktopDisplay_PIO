# Codex 用量读取模块

> 天气屏不再使用 Codex 数据。本模块由支持 macOS 与 Windows 的 [`../desktop_display_bridge`](../desktop_display_bridge/README.md) 复用，为 USB 系统状态屏提供周剩余用量。

这个模块只做一件事：通过本机 Codex App Server 获取订阅额度窗口。默认 `auto` 模式优先使用 OpenAI 文档公开的 `account/rateLimits/read` 方法；只有本机 Codex 版本不支持 App Server 时才兼容原来的凭据请求。也可以独立运行，以 JSON 查看和诊断解析结果。

```bash
python3 codex_usage_bridge.py --once
python3 codex_usage_bridge.py
```

桥接会正常验证 HTTPS 证书。Python.org 的 macOS Python 若没有可用的系统 CA 路径，会自动使用已安装的 `certifi`；若两者都不可用，请先修复 Python 证书环境，程序不会通过关闭证书校验来绕过问题。

独立运行时默认监听：

```text
http://127.0.0.1:8766/v1/codex-usage
```

USB 状态屏只会收到已用/剩余百分比和陈旧标志。App Server 负责既有登录状态和令牌刷新，桥接本身不需要解析 OAuth 令牌；兼容回退也不会把令牌发送到设备或写入日志。

桥接服务没有用户认证，只应在可信局域网内运行；不要把 `8766` 端口映射到公网。局域网响应会把详细错误统一为 `unavailable` 或 `stale`，不会泄露凭据文件路径或上游诊断信息。OAuth 令牌只允许发送至经过 HTTPS 验证的 `chatgpt.com` 域名；明文 HTTP 仅允许用于本机回环测试。

环境变量：

- `CODEX_BRIDGE_HOST`：监听地址，默认 `127.0.0.1`；仅在确有局域网调用方时才改为 `0.0.0.0`。
- `CODEX_BRIDGE_PORT`：监听端口，默认 `8766`。
- `CODEX_BRIDGE_REFRESH_SECONDS`：刷新周期，默认 60 秒，最小 60 秒。USB 屏每秒收到最近一次成功结果，不会每秒请求 ChatGPT。
- `CODEX_BRIDGE_SOURCE`：`auto`（默认）、`app-server` 或 `legacy`。
- `CODEX_BRIDGE_CODEX_BINARY`：可选的 Codex 可执行文件路径；macOS 会自动发现 ChatGPT App 内置版本。

此功能使用 Codex App Server 的账户限额接口，而不是 OpenAI API 的组织用量接口；上游格式变化时只需更新这个桥接程序，不需要重新设计设备端界面。

OpenAI、ChatGPT、Codex 及相关标识是其各自权利人的商标或标识。本项目是独立的社区项目，与 OpenAI 不存在隶属、赞助或背书关系。

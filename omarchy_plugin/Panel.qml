import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// MiniDisplay bar widget: USB status-display bridge state, live metrics,
// brightness and service controls. All data comes from scripts/minidisplayctl,
// which talks to the local bridge on 127.0.0.1:8766 and systemd --user.
Panel {
  id: root
  moduleName: "minidisplay.bridge"
  ipcTarget: "minidisplay.bridge"
  manageIpc: false

  property var status: ({ service: "unknown", bridge: false })
  property bool refreshPending: false
  property string errorText: ""
  property bool busy: false

  readonly property string ctl: root.localPath(Qt.resolvedUrl("scripts/minidisplayctl"))
  readonly property int pollMs: Math.max(1, Number(root.setting("pollSeconds", 3))) * 1000
  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  readonly property var desktop: status.desktop || ({})
  readonly property var usb: status.usb || ({})
  readonly property var codex: status.codex || ({})
  readonly property var claude: status.claude || ({})
  readonly property var prefs: status.settings || ({})
  readonly property bool connected: status.bridge === true && usb.connected === true
  // "ok" (streaming), "warn" (bridge up, display not reachable), "off" (bridge down)
  readonly property string health: connected ? "ok" : (status.bridge === true ? "warn" : "off")

  function localPath(url) {
    var value = String(url || "")
    if (value.indexOf("file://") === 0) value = value.substring(7)
    try { return decodeURIComponent(value) } catch (error) { return value }
  }

  function pct(value) { return value === null || value === undefined ? "--" : Math.round(value) + "%" }
  function temp(value) { return value === null || value === undefined ? "--°C" : Math.round(value) + "°C" }
  function rate(bps) {
    if (bps === null || bps === undefined) return "--"
    if (bps >= 1048576) return (bps / 1048576).toFixed(1) + " MB/s"
    if (bps >= 1024) return Math.round(bps / 1024) + " KB/s"
    return Math.round(bps) + " B/s"
  }

  function usbText() {
    if (status.bridge !== true) {
      if (status.service === "not-installed") return "桥接服务未安装或未运行"
      if (status.service === "active" || status.service === "activating") return "桥接正在启动…"
      return "桥接服务未运行"
    }
    if (usb.connected === true) return "已连接 " + (usb.port || "")
    var error = String(usb.error || "")
    if (error === "cannot open USB display") return "无法打开 " + (usb.port || "串口") + "（检查 uucp 组权限）"
    if (error === "USB display not connected") return "未检测到 USB 小屏"
    return error !== "" ? error : "等待连接…"
  }

  function tooltip() {
    var lines = ["MiniDisplay · " + usbText()]
    if (status.bridge === true) {
      lines.push("CPU " + pct(desktop.cpu_percent) + " · 内存 " + pct(desktop.memory_percent)
                 + " · " + temp(desktop.cpu_temperature_celsius))
      lines.push("Codex 剩余 " + pct(codex.remaining_percent))
    }
    lines.push("左键面板 · 中键状态页")
    return lines.join("\n")
  }

  function refresh() {
    if (statusProc.running) { root.refreshPending = true; return }
    root.refreshPending = false
    statusProc.running = true
  }

  function run(args) {
    if (actionProc.running) return
    root.busy = true
    root.errorText = ""
    actionProc.command = [root.ctl].concat(args)
    actionProc.running = true
  }

  function setPref(key, value) { root.run(["set", key, String(value)]) }
  function openDashboard() { Quickshell.execDetached(["xdg-open", "http://127.0.0.1:8766/"]) }
  function openLogs() {
    Quickshell.execDetached(["xdg-terminal-exec", "journalctl", "--user", "-u", "minidisplay-bridge.service", "-f"])
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  Component.onCompleted: refresh()
  onOpenedChanged: if (opened) {
    root.errorText = ""
    root.refresh()
    Qt.callLater(function() { keyCatcher.forceActiveFocus() })
  }

  Timer {
    interval: root.opened ? 1000 : root.pollMs
    repeat: true
    running: true
    onTriggered: root.refresh()
  }

  Process {
    id: statusProc
    command: [root.ctl, "status"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        try { root.status = JSON.parse(text || "{}") }
        catch (error) { root.status = { service: "unknown", bridge: false } }
      }
    }
    onExited: if (root.refreshPending) Qt.callLater(root.refresh)
  }

  Process {
    id: actionProc
    stderr: StdioCollector { id: actionError; waitForEnd: true }
    onExited: function(exitCode) {
      root.busy = false
      if (exitCode !== 0) root.errorText = String(actionError.text || "操作失败").trim()
      refreshSoon.restart()
    }
  }

  Timer { id: refreshSoon; interval: 400; onTriggered: root.refresh() }

  IpcHandler {
    target: root.ipcTarget
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.toggle() }
    function reconnect(): void { root.run(["reconnect"]) }
    function current(): string { return JSON.stringify(root.status) }
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.health === "off" ? "󰶐" : "󰍹"
    active: root.health === "warn"
    dimmed: root.health === "off"
    tooltipText: root.tooltip()
    onPressed: function(buttonCode) {
      if (buttonCode === Qt.MiddleButton) root.openDashboard()
      else if (buttonCode === Qt.LeftButton) root.toggle()
    }
  }

  component InfoPair: Row {
    property string label: ""
    property string value: ""
    property color valueColor: root.foreground
    width: parent.width
    spacing: Style.space(8)
    Text {
      id: labelText
      text: parent.label
      color: root.foreground
      opacity: 0.6
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
    }
    Item { width: Math.max(0, parent.width - labelText.implicitWidth - valueText.implicitWidth - parent.spacing * 2); height: 1 }
    Text {
      id: valueText
      text: parent.value
      color: parent.valueColor
      font.family: root.fontFamily
      font.pixelSize: Style.font.bodySmall
    }
  }

  component BrightnessRow: Column {
    id: brightnessRow
    property string label: ""
    property string key: ""
    property int maximum: 100
    width: parent.width
    spacing: Style.space(4)
    InfoPair { label: brightnessRow.label; value: Math.round(slider.liveValue) + "%" }
    PanelSlider {
      id: slider
      width: parent.width
      height: Style.space(22)
      bar: root.bar
      minimum: 0
      maximum: brightnessRow.maximum
      step: 1
      integer: true
      enabled: root.status.bridge === true && !root.busy
      value: Number(root.prefs[brightnessRow.key] || 0)
      onReleased: function(v) { root.setPref(brightnessRow.key, Math.round(v)) }
    }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(360))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      blocked: root.busy
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      onTextKey: function(t) {
        if (t === "r" || t === "R") root.run(["reconnect"])
        else if (t === "o" || t === "O") root.openDashboard()
      }

      Column {
        id: column
        width: parent.width
        spacing: Style.space(12)

        PanelHero {
          id: hero
          width: parent.width
          title: "MiniDisplay"
          meta: root.connected ? "USB connected" : (root.status.bridge === true ? "USB offline" : "Bridge stopped")
          foreground: root.foreground
          fontFamily: root.fontFamily
          iconOpacity: root.connected ? 1.0 : 0.5
          iconComponent: Component {
            Text {
              text: root.health === "off" ? "󰶐" : "󰍹"
              color: root.health === "warn" ? root.urgent : root.foreground
              font.family: root.fontFamily
              font.pixelSize: Style.font.display
            }
          }
          trailingControl: Component {
            ToggleSwitch {
              id: serviceSwitch
              visible: root.status.service !== "not-installed"
              checked: root.status.service === "active"
              busy: root.busy
              foreground: hero.foreground
              onToggled: root.run([checked ? "stop" : "start"])
              PanelToolTip {
                visible: serviceSwitch.containsMouse
                text: serviceSwitch.checked ? "停止桥接服务" : "启动桥接服务"
                fontFamily: root.fontFamily
              }
            }
          }
        }

        Text {
          width: parent.width
          text: root.usbText() + (root.status.version
            ? "\nBridge " + root.status.version + " · " + (root.desktop.interface || "--") : "")
          color: root.connected ? root.dim : root.urgent
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          wrapMode: Text.WordWrap
        }

        Text {
          visible: root.errorText !== "" || (root.status.bridge !== true && root.status.service === "not-installed")
          width: parent.width
          text: root.errorText !== "" ? root.errorText : "安装：在仓库根目录运行 tools/linux_bridge.sh install"
          color: root.urgent
          font.family: root.fontFamily
          font.pixelSize: Style.font.bodySmall
          wrapMode: Text.WordWrap
        }

        Column {
          visible: root.status.bridge === true
          width: parent.width
          spacing: Style.spacing.labelGap

          InfoPair { label: "CPU / 内存"; value: root.pct(root.desktop.cpu_percent) + " / " + root.pct(root.desktop.memory_percent) }
          InfoPair { label: "CPU / GPU 温度"; value: root.temp(root.desktop.cpu_temperature_celsius) + " / " + root.temp(root.desktop.gpu_temperature_celsius) }
          InfoPair { label: "下载 / 上传"; value: root.rate(root.desktop.download_bps) + " / " + root.rate(root.desktop.upload_bps) }
          InfoPair { label: "出口位置"; value: root.desktop.network_location || "--" }
          InfoPair {
            label: "Codex 周剩余"
            value: root.pct(root.codex.remaining_percent) + (root.codex.stale ? "（缓存）" : "")
            valueColor: root.codex.remaining_percent !== undefined && root.codex.remaining_percent < 10 ? root.urgent : root.foreground
          }
          InfoPair {
            label: "Claude 5h / 周"
            value: root.prefs.claude_enabled !== true ? "未启用"
              : root.claude.ok === true ? root.pct((root.claude.five_hour || {}).remaining_percent) + " / " + root.pct((root.claude.seven_day || {}).remaining_percent)
              : (root.claude.error || "--")
          }
        }

        PanelSeparator { visible: root.status.bridge === true; foreground: root.foreground }

        Column {
          visible: root.status.bridge === true
          width: parent.width
          spacing: Style.space(10)

          PanelSectionHeader {
            text: "小屏亮度" + (root.desktop.night_mode ? "（夜间时段）" : "")
            foreground: root.foreground
            fontFamily: root.fontFamily
          }
          BrightnessRow { label: "日间"; key: "day_brightness" }
          BrightnessRow { label: "夜间上限"; key: "night_brightness" }

          Row {
            width: parent.width
            spacing: Style.space(8)
            Text {
              anchors.verticalCenter: parent.verticalCenter
              width: parent.width - claudeSwitch.width - parent.spacing
              text: "显示 Claude 额度"
              color: root.foreground
              opacity: 0.6
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
            }
            ToggleSwitch {
              id: claudeSwitch
              checked: root.prefs.claude_enabled === true
              busy: root.busy
              foreground: root.foreground
              onToggled: root.setPref("claude_enabled", !checked)
            }
          }
        }

        PanelSeparator { foreground: root.foreground }

        Row {
          spacing: Style.space(6)
          Button {
            text: "重连"
            iconText: "󰑓"
            fontSize: Style.font.bodySmall
            foreground: root.foreground
            fontFamily: root.fontFamily
            enabled: root.status.bridge === true && !root.busy
            onClicked: root.run(["reconnect"])
          }
          Button {
            text: "重启服务"
            iconText: "󰜉"
            fontSize: Style.font.bodySmall
            foreground: root.foreground
            fontFamily: root.fontFamily
            enabled: root.status.service !== "not-installed" && !root.busy
            onClicked: root.run(["restart"])
          }
          Button {
            text: "状态页"
            iconText: "󰖟"
            fontSize: Style.font.bodySmall
            foreground: root.foreground
            fontFamily: root.fontFamily
            onClicked: { root.close(); root.openDashboard() }
          }
          Button {
            text: "日志"
            iconText: "󰌱"
            fontSize: Style.font.bodySmall
            foreground: root.foreground
            fontFamily: root.fontFamily
            onClicked: { root.close(); root.openLogs() }
          }
        }
      }
    }
  }
}

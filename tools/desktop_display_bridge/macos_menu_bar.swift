// Native menu-bar shell. The bundled Python worker owns metrics and USB;
// WebKit only renders our loopback status/settings page, never remote content.
import AppKit
import WebKit
import Darwin

private let showNotification = Notification.Name("io.github.ismethr.SmallDesktopDisplayBridge.show")

func menuBarImage() -> NSImage {
    let image = NSImage(size: NSSize(width: 20, height: 18), flipped: false) { _ in
        NSColor.black.setStroke()
        let outline = NSBezierPath(roundedRect: NSRect(x: 1, y: 4, width: 18, height: 13), xRadius: 2, yRadius: 2)
        outline.lineWidth = 1.4
        outline.stroke()
        let pulse = NSBezierPath()
        pulse.move(to: NSPoint(x: 3, y: 9))
        for point in [NSPoint(x: 6, y: 9), NSPoint(x: 8, y: 13), NSPoint(x: 11, y: 7),
                      NSPoint(x: 13, y: 11), NSPoint(x: 17, y: 11)] { pulse.line(to: point) }
        pulse.lineWidth = 1.2
        pulse.stroke()
        let stand = NSBezierPath()
        stand.move(to: NSPoint(x: 10, y: 4)); stand.line(to: NSPoint(x: 10, y: 1))
        stand.move(to: NSPoint(x: 6, y: 1)); stand.line(to: NSPoint(x: 14, y: 1))
        stand.lineWidth = 1.4
        stand.stroke()
        return true
    }
    image.isTemplate = true // macOS supplies the correct light/dark menu-bar color.
    image.accessibilityDescription = "MiniDisplay Bridge"
    return image
}

final class MenuBarApp: NSObject, NSApplicationDelegate, WKNavigationDelegate {
    private var statusItem: NSStatusItem!
    private var window: NSWindow?
    private var webView: WKWebView?
    private var worker: Process?
    private var quitting = false
    private var workerFailures = 0
    private var navigationRetries = 0
    private var requestedPage: URL?
    private var signalSources: [DispatchSourceSignal] = []
    private let baseURL: URL

    override init() {
        var port = Int(ProcessInfo.processInfo.environment["CODEX_BRIDGE_PORT"] ?? "8766") ?? 8766
        let arguments = Array(CommandLine.arguments.dropFirst())
        for (index, argument) in arguments.enumerated() {
            if argument == "--listen-port", index + 1 < arguments.count { port = Int(arguments[index + 1]) ?? port }
            if argument.hasPrefix("--listen-port=") { port = Int(argument.dropFirst(14)) ?? port }
        }
        baseURL = URL(string: "http://127.0.0.1:\((1...65535).contains(port) ? port : 8766)/")!
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        statusItem = NSStatusBar.system.statusItem(withLength: 28)
        if let button = statusItem.button {
            button.image = menuBarImage()
            button.toolTip = "MiniDisplay Bridge · 点击查看状态，右键打开菜单"
            button.setAccessibilityLabel("MiniDisplay Bridge 状态与设置")
            button.target = self
            button.action = #selector(statusClicked)
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }
        let appMenu = NSMenu()
        let root = NSMenuItem()
        let submenu = NSMenu()
        submenu.addItem(withTitle: "关闭窗口", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        submenu.addItem(withTitle: "退出 MiniDisplay Bridge", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        root.submenu = submenu
        appMenu.addItem(root)
        NSApp.mainMenu = appMenu
        DistributedNotificationCenter.default().addObserver(
            self, selector: #selector(showStatus), name: showNotification, object: nil
        )
        for value in [SIGTERM, SIGINT] {
            signal(value, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: value, queue: .main)
            source.setEventHandler { NSApp.terminate(nil) }
            source.resume()
            signalSources.append(source)
        }
        startWorker()
        NSLog("MiniDisplay menu bar ready (visible: %@)", statusItem.isVisible ? "yes" : "no")
    }

    private func startWorker() {
        guard !quitting else { return }
        let process = Process()
        process.executableURL = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/MiniDisplay Bridge Worker")
        process.arguments = Array(CommandLine.arguments.dropFirst()).filter { !$0.hasPrefix("-psn_") }
            + ["--listen-host", "127.0.0.1"]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        process.terminationHandler = { [weak self] ended in
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.worker = nil
                if self.quitting { NSApp.reply(toApplicationShouldTerminate: true); return }
                self.workerFailures += 1
                if ended.terminationStatus != 0 && self.workerFailures <= 3 {
                    DispatchQueue.main.asyncAfter(deadline: .now() + Double(self.workerFailures * 2)) { self.startWorker() }
                } else {
                    self.showError("桥接进程已停止。请退出后重新打开 App；若已有命令行桥接正在运行，请先关闭它。")
                }
            }
        }
        do { try process.run(); worker = process }
        catch { showError("无法启动内置桥接程序，请重新安装完整的 MiniDisplay Bridge.app。") }
    }

    private func showError(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "MiniDisplay Bridge"
        alert.informativeText = message
        alert.addButton(withTitle: "好")
        NSApp.activate(ignoringOtherApps: true)
        alert.runModal()
    }

    @objc private func statusClicked() {
        if NSApp.currentEvent?.type == .rightMouseUp {
            let menu = NSMenu()
            for (title, action) in [("打开状态面板", #selector(showStatus)), ("显示屏设置…", #selector(showSettings))] {
                let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
                item.target = self
                menu.addItem(item)
            }
            menu.addItem(.separator())
            menu.addItem(withTitle: "退出 MiniDisplay Bridge", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
            statusItem.menu = menu
            statusItem.button?.performClick(nil)
            statusItem.menu = nil
        } else { showStatus() }
    }

    @objc func showStatus() { openPage(settings: false) }
    @objc private func showSettings() { openPage(settings: true) }

    private func openPage(settings: Bool) {
        if window == nil {
            let available = NSScreen.main?.visibleFrame.size ?? NSSize(width: 1200, height: 900)
            let created = NSWindow(contentRect: NSRect(x: 0, y: 0, width: min(960, available.width - 60),
                                                       height: min(800, available.height - 80)),
                                   styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
            created.title = "MiniDisplay · 状态与设置"
            created.minSize = NSSize(width: 480, height: 460)
            created.isReleasedWhenClosed = false
            created.center()
            let configuration = WKWebViewConfiguration()
            configuration.websiteDataStore = .nonPersistent()
            let view = WKWebView(frame: .zero, configuration: configuration)
            view.navigationDelegate = self
            created.contentView = view
            window = created
            webView = view
        }
        requestedPage = settings ? URL(string: "#settings", relativeTo: baseURL)!.absoluteURL : baseURL
        navigationRetries = 0
        if webView?.url?.path == "/" {
            // Reusing the window must not discard an unsaved settings form.
            webView?.evaluateJavaScript(settings ? "location.hash='settings';document.getElementById('settings').scrollIntoView();"
                                                  : "location.hash='';window.scrollTo(0,0);", completionHandler: nil)
        } else { webView?.load(URLRequest(url: requestedPage!)) }
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        showStatus()
        return true
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        quitting = true
        guard let process = worker, process.isRunning else { return .terminateNow }
        process.terminate()
        DispatchQueue.main.asyncAfter(deadline: .now() + 8) {
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
        }
        return .terminateLater
    }

    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard let url = navigationAction.request.url, url.scheme == baseURL.scheme,
              url.host == baseURL.host, url.port == baseURL.port else { decisionHandler(.cancel); return }
        decisionHandler(.allow)
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        navigationRetries = 0
        NSLog("MiniDisplay status page loaded")
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        NSLog("MiniDisplay status page load failed (code: %ld)", (error as NSError).code)
        guard window?.isVisible == true, navigationRetries < 8, let url = requestedPage else { return }
        navigationRetries += 1
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            guard self?.window?.isVisible == true else { return }
            self?.webView?.load(URLRequest(url: url))
        }
    }
}

let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    .appendingPathComponent("SmallDesktopDisplay", isDirectory: true)
try FileManager.default.createDirectory(at: support, withIntermediateDirectories: true)
let lockDescriptor = open(support.appendingPathComponent("menu-bar.lock").path, O_CREAT | O_RDWR, 0o600)
guard lockDescriptor >= 0 else { exit(1) }
guard flock(lockDescriptor, LOCK_EX | LOCK_NB) == 0 else {
    DistributedNotificationCenter.default().postNotificationName(showNotification, object: nil, userInfo: nil, deliverImmediately: true)
    exit(0)
}
// The worker must not inherit the shell's instance lock across exec.
_ = fcntl(lockDescriptor, F_SETFD, FD_CLOEXEC)
let application = NSApplication.shared
let delegate = MenuBarApp()
application.delegate = delegate
application.run()
close(lockDescriptor)

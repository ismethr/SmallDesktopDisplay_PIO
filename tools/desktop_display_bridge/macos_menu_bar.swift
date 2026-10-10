// Native menu-bar shell. The bundled Python worker owns metrics and USB; this
// process only reads the worker's loopback JSON API and renders it with AppKit
// and SwiftUI: a status popover, a settings window and an optional quota title.
import AppKit
import Combine
import Darwin
import ServiceManagement
import SwiftUI

private let showNotification = Notification.Name("io.github.ismethr.SmallDesktopDisplayBridge.show")
private let menuBarQuotaKey = "ShowQuotaInMenuBar"

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

// MARK: - Worker API

struct QuotaWindow: Decodable {
    let remainingPercent: Double?
    let resetAt: Double?
}

struct Overview: Decodable {
    struct Desktop: Decodable {
        let ok: Bool?
        let cpuPercent: Double?
        let memoryPercent: Double?
        let cpuTemperatureCelsius: Double?
        let gpuTemperatureCelsius: Double?
        let networkLocation: String?
        let networkLocationStale: Bool?
        let downloadBps: Double?
        let uploadBps: Double?
        let displayBrightnessPercent: Int?
        let nightMode: Bool?
    }
    struct Usb: Decodable {
        let connected: Bool?
        let port: String?
        let phase: String?
        let error: String?
    }
    struct Codex: Decodable {
        let ok: Bool?
        let remainingPercent: Double?
        let resetAt: Double?
        let session: QuotaWindow?
        let fetchedAt: Double?
        let stale: Bool?
    }
    struct Claude: Decodable {
        let ok: Bool?
        let fiveHour: QuotaWindow?
        let sevenDay: QuotaWindow?
        let fetchedAt: Double?
        let stale: Bool?
        let error: String?
    }
    let version: String?
    let desktop: Desktop?
    let usb: Usb?
    let usage: Codex?
    let claude: Claude?
}

struct DisplaySettings: Codable, Equatable {
    var dayBrightness: Int
    var nightBrightness: Int
    var offlineBrightness: Int
    var nightStartHour: Int
    var nightEndHour: Int
    var serialPort: String?
    var weatherCityCode: String
    var claudeEnabled: Bool

    // Always encode serial_port, as null for automatic detection.
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(dayBrightness, forKey: .dayBrightness)
        try container.encode(nightBrightness, forKey: .nightBrightness)
        try container.encode(offlineBrightness, forKey: .offlineBrightness)
        try container.encode(nightStartHour, forKey: .nightStartHour)
        try container.encode(nightEndHour, forKey: .nightEndHour)
        try container.encode(serialPort, forKey: .serialPort)
        try container.encode(weatherCityCode, forKey: .weatherCityCode)
        try container.encode(claudeEnabled, forKey: .claudeEnabled)
    }
}

struct SettingsPayload: Decodable {
    let settings: DisplaySettings
    let revision: Int
    let token: String
    let ports: [String]
    let warning: String?
}

private struct SettingsUpdate: Encodable {
    let revision: Int
    let settings: DisplaySettings
}

private struct APIError: Decodable { let error: String? }

final class BridgeAPI {
    let baseURL: URL
    private let session: URLSession

    init(baseURL: URL) {
        self.baseURL = baseURL
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 4
        configuration.connectionProxyDictionary = [:] // Loopback only; never via a proxy.
        session = URLSession(configuration: configuration)
    }

    private static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return decoder
    }()

    private func send<T: Decodable>(_ request: URLRequest, as type: T.Type,
                                    completion: @escaping (Result<T, Error>) -> Void) {
        session.dataTask(with: request) { data, response, error in
            let result: Result<T, Error>
            if let error = error {
                result = .failure(error)
            } else if let data = data, let http = response as? HTTPURLResponse {
                if (200..<300).contains(http.statusCode), let value = try? Self.decoder.decode(T.self, from: data) {
                    result = .success(value)
                } else {
                    let message = (try? Self.decoder.decode(APIError.self, from: data))?.error ?? "HTTP \(http.statusCode)"
                    result = .failure(NSError(domain: "MiniDisplay", code: http.statusCode,
                                              userInfo: [NSLocalizedDescriptionKey: message]))
                }
            } else {
                result = .failure(NSError(domain: "MiniDisplay", code: -1))
            }
            DispatchQueue.main.async { completion(result) }
        }.resume()
    }

    func overview(_ completion: @escaping (Result<Overview, Error>) -> Void) {
        send(URLRequest(url: baseURL.appendingPathComponent("v1/overview")), as: Overview.self, completion: completion)
    }

    func settings(_ completion: @escaping (Result<SettingsPayload, Error>) -> Void) {
        send(URLRequest(url: baseURL.appendingPathComponent("v1/settings")), as: SettingsPayload.self, completion: completion)
    }

    private func post(_ path: String, token: String, body: Data,
                      completion: @escaping (Result<SettingsPayload, Error>) -> Void) {
        var request = URLRequest(url: baseURL.appendingPathComponent(path))
        request.httpMethod = "POST"
        request.httpBody = body
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(token, forHTTPHeaderField: "X-MiniDisplay-Token")
        send(request, as: SettingsPayload.self, completion: completion)
    }

    func save(_ settings: DisplaySettings, revision: Int, token: String,
              completion: @escaping (Result<SettingsPayload, Error>) -> Void) {
        let encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
        guard let body = try? encoder.encode(SettingsUpdate(revision: revision, settings: settings)) else { return }
        post("v1/settings", token: token, body: body, completion: completion)
    }

    func reconnect(token: String, completion: @escaping (Result<SettingsPayload, Error>) -> Void) {
        post("v1/reconnect", token: token, body: Data("{}".utf8), completion: completion)
    }
}

// MARK: - Status model

final class StatusModel: ObservableObject {
    @Published private(set) var overview: Overview?
    @Published private(set) var reachable = true
    @Published private(set) var updatedAt: Date?
    private let api: BridgeAPI
    private var timer: Timer?
    private var inFlight = false

    init(api: BridgeAPI) { self.api = api }

    func refresh() {
        guard !inFlight else { return }
        inFlight = true
        api.overview { [weak self] result in
            guard let self = self else { return }
            self.inFlight = false
            switch result {
            case .success(let value):
                self.overview = value
                self.reachable = true
                self.updatedAt = Date()
            case .failure:
                self.reachable = false
            }
        }
    }

    /// Polls every `interval` seconds; the popover uses 1 s, the menu-bar title 30 s.
    func poll(every interval: TimeInterval) {
        timer?.invalidate()
        refresh()
        let created = Timer(timeInterval: interval, repeats: true) { [weak self] _ in self?.refresh() }
        RunLoop.main.add(created, forMode: .common)
        timer = created
    }

    func stopPolling() {
        timer?.invalidate()
        timer = nil
    }

    /// The tightest 5-hour window across providers, for the optional menu-bar title.
    var lowestFiveHour: Double? {
        [overview?.usage?.ok == true ? overview?.usage?.session?.remainingPercent : nil,
         overview?.claude?.fiveHour?.remainingPercent].compactMap { $0 }.min()
    }
}

// MARK: - Formatting

enum Format {
    static func percent(_ value: Double?) -> String {
        guard let value = value, value.isFinite else { return "--" }
        return "\(Int(value.rounded()))%"
    }

    static func rate(_ bytesPerSecond: Double?) -> String {
        guard let value = bytesPerSecond, value.isFinite else { return "--" }
        let units = ["B/s", "KB/s", "MB/s", "GB/s"]
        var scaled = value
        var unit = 0
        while scaled >= 1024, unit < units.count - 1 { scaled /= 1024; unit += 1 }
        return unit == 0 ? "\(Int(scaled)) B/s" : String(format: "%.1f %@", scaled, units[unit])
    }

    static func temperature(_ celsius: Double?) -> String {
        guard let value = celsius, value.isFinite else { return "--°C" }
        return "\(Int(value.rounded()))°C"
    }

    static func countdown(to timestamp: Double?, now: Date = Date()) -> String? {
        guard let timestamp = timestamp else { return nil }
        let seconds = Int(timestamp - now.timeIntervalSince1970)
        if seconds <= 0 { return "即将重置" }
        let days = seconds / 86400, hours = seconds % 86400 / 3600, minutes = seconds % 3600 / 60
        if days > 0 { return "\(days) 天 \(hours) 小时后重置" }
        if hours > 0 { return "\(hours) 小时 \(minutes) 分后重置" }
        return "\(max(1, minutes)) 分钟后重置"
    }

    private static let resetDate: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "zh_CN")
        formatter.dateFormat = "M/d E HH:mm"
        return formatter
    }()

    static func resetDate(_ timestamp: Double?) -> String? {
        guard let timestamp = timestamp else { return nil }
        return resetDate.string(from: Date(timeIntervalSince1970: timestamp))
    }

    /// "CN-YN" → ("🇨🇳", "YN"); missing labels become a globe.
    static func location(_ label: String?) -> (flag: String, detail: String) {
        guard let label = label, label.count >= 2, !label.hasPrefix("-") else { return ("🌐", "--") }
        let parts = label.split(separator: "-", maxSplits: 1).map(String.init)
        let code = parts[0].uppercased()
        var flag = ""
        if code.count == 2, code.unicodeScalars.allSatisfy({ $0.value >= 65 && $0.value <= 90 }) {
            for scalar in code.unicodeScalars {
                if let regional = UnicodeScalar(127397 + scalar.value) { flag.unicodeScalars.append(regional) }
            }
        }
        return (flag.isEmpty ? "🌐" : flag, parts.count > 1 ? parts[1] : code)
    }
}

// MARK: - Status popover

private extension Color {
    static let codex = Color(red: 0.42, green: 0.86, blue: 0.71)
    static let claude = Color(red: 0.86, green: 0.47, blue: 0.34)
}

/// Same thresholds as the firmware: yellow below 30 %, red below 10 % remaining.
private func quotaTint(_ remaining: Double?, accent: Color) -> Color {
    guard let value = remaining else { return .secondary }
    return value < 10 ? .red : value < 30 ? .yellow : accent
}

private func loadTint(_ value: Double?) -> Color {
    guard let value = value else { return .secondary }
    return value >= 85 ? .red : value >= 65 ? .yellow : .green
}

private func temperatureTint(_ value: Double?) -> Color {
    guard let value = value else { return .secondary }
    return value >= 80 ? .red : value >= 60 ? .orange : .primary
}

struct MeterBar: View {
    let fraction: Double?
    let tint: Color
    var height: CGFloat = 4

    var body: some View {
        GeometryReader { proxy in
            ZStack(alignment: .leading) {
                Capsule().fill(.quaternary)
                if let fraction {
                    Capsule().fill(tint)
                        .frame(width: max(0, min(1, fraction)) * proxy.size.width)
                }
            }
        }
        .frame(height: height)
        .animation(.easeOut(duration: 0.25), value: fraction)
    }
}

struct SectionCard<Content: View>: View {
    @ViewBuilder let content: Content

    var body: some View {
        content
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(.fill.quinary, in: RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

/// One compact line: name, bar, percentage and a temperature.
struct LoadRow: View {
    let title: String
    let value: Double?
    let temperatureTitle: String
    let temperature: Double?

    var body: some View {
        GridRow {
            Text(title).foregroundStyle(.secondary)
            MeterBar(fraction: value.map { $0 / 100 }, tint: loadTint(value))
            Text(Format.percent(value)).monospacedDigit().gridColumnAlignment(.trailing)
            Text(temperatureTitle).foregroundStyle(.secondary)
            Text(Format.temperature(temperature)).monospacedDigit()
                .foregroundStyle(temperatureTint(temperature))
                .gridColumnAlignment(.trailing)
        }
    }
}

struct QuotaRow: View {
    let title: String
    let window: QuotaWindow?
    let accent: Color
    let stale: Bool
    let weekly: Bool

    var body: some View {
        let remaining = window?.remainingPercent
        let reset = weekly ? Format.resetDate(window?.resetAt) : Format.countdown(to: window?.resetAt)
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Text(title).foregroundStyle(.secondary).frame(width: 30, alignment: .leading)
                MeterBar(fraction: remaining.map { $0 / 100 },
                         tint: stale ? Color.secondary : quotaTint(remaining, accent: accent))
                Text(Format.percent(remaining))
                    .fontWeight(.semibold).monospacedDigit()
                    .foregroundStyle(stale || remaining == nil ? AnyShapeStyle(.secondary)
                                                               : AnyShapeStyle(quotaTint(remaining, accent: .primary)))
                    .frame(width: 38, alignment: .trailing)
            }
            .font(.callout)
            Text(reset ?? " ").font(.caption2).foregroundStyle(.tertiary).lineLimit(1).padding(.leading, 36)
        }
        .help(reset.map { weekly ? "\($0) 重置" : $0 } ?? "")
    }
}

struct QuotaCard: View {
    let name: String
    let accent: Color
    let fiveHour: QuotaWindow?
    let week: QuotaWindow?
    let stale: Bool
    let note: String?

    var body: some View {
        SectionCard {
            VStack(alignment: .leading, spacing: 5) {
                HStack(spacing: 5) {
                    Circle().fill(accent).frame(width: 7, height: 7)
                    Text(name).font(.callout.weight(.semibold))
                    Spacer()
                    if stale { Text("上次数据").font(.caption2).foregroundStyle(.orange) }
                }
                if let note {
                    Text(note).font(.caption).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxHeight: .infinity, alignment: .top)
                } else {
                    QuotaRow(title: "5 时", window: fiveHour, accent: accent, stale: stale, weekly: false)
                    QuotaRow(title: "每周", window: week, accent: accent, stale: stale, weekly: true)
                }
            }
        }
    }
}

private let claudeNotes: [String: String] = [
    "disabled": "未开启，可在设置中连接 Claude 账户额度。",
    "connecting": "正在读取账户额度…",
    "renewing": "正在通过 Claude 官方程序恢复登录…",
    "login_required": "未找到 Claude Code 登录，请先运行一次 claude auth login。",
    "login_expired": "登录已过期，使用一次 Claude Code 后会自动恢复。",
    "keychain_denied": "未获准读取钥匙串，请在设置中重新开启并选择“始终允许”。",
    "missing_scope": "当前登录缺少额度读取权限。",
    "access_denied": "当前登录无法访问订阅额度。",
]

struct StatusView: View {
    @ObservedObject var model: StatusModel
    let openSettings: () -> Void
    let openDashboard: () -> Void
    let quit: () -> Void

    private var usbLine: (text: String, color: Color) {
        guard model.reachable else { return ("桥接未响应", .red) }
        guard let usb = model.overview?.usb else { return ("正在连接…", .secondary) }
        if usb.connected == true { return ("USB 已连接", .green) }
        switch usb.phase {
        case "connecting": return ("正在建立 USB 连接", .yellow)
        case "retrying": return ("连接中断，正在重试", .orange)
        case "disabled": return ("仅采集模式", .secondary)
        default: return ("等待 USB 小屏", .yellow)
        }
    }

    var body: some View {
        let desktop = model.overview?.desktop
        let codex = model.overview?.usage
        let claude = model.overview?.claude
        let location = Format.location(desktop?.networkLocation)
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                Text("MiniDisplay").font(.headline)
                Circle().fill(usbLine.color).frame(width: 7, height: 7)
                Text(usbLine.text).font(.caption).foregroundStyle(.secondary)
                    .help(model.overview?.usb?.port ?? "")
                Spacer()
                if let brightness = desktop?.displayBrightnessPercent {
                    Label("\(brightness)%", systemImage: desktop?.nightMode == true ? "moon.fill" : "sun.max.fill")
                        .font(.caption).foregroundStyle(.secondary)
                        .help("小屏当前亮度")
                }
            }

            SectionCard {
                Grid(horizontalSpacing: 8, verticalSpacing: 6) {
                    LoadRow(title: "CPU", value: desktop?.cpuPercent,
                            temperatureTitle: "CPU", temperature: desktop?.cpuTemperatureCelsius)
                    LoadRow(title: "内存", value: desktop?.memoryPercent,
                            temperatureTitle: "GPU", temperature: desktop?.gpuTemperatureCelsius)
                }
                .font(.callout)
            }

            HStack(alignment: .top, spacing: 8) {
                QuotaCard(name: "Codex", accent: .codex,
                          fiveHour: codex?.ok == true ? codex?.session : nil,
                          week: codex?.ok == true ? QuotaWindow(remainingPercent: codex?.remainingPercent,
                                                                 resetAt: codex?.resetAt) : nil,
                          stale: codex?.stale == true,
                          note: codex == nil || codex?.ok == true ? nil : "暂未取得余量，请确认 Codex 已登录。")
                QuotaCard(name: "Claude", accent: .claude,
                          fiveHour: claude?.fiveHour, week: claude?.sevenDay,
                          stale: claude?.ok == true && claude?.stale == true,
                          note: claude?.ok == true ? nil : claude?.error.flatMap { claudeNotes[$0] })
            }
            .fixedSize(horizontal: false, vertical: true)

            HStack(spacing: 10) {
                Text("\(location.flag) \(location.detail)")
                    .foregroundStyle(desktop?.networkLocationStale == true ? .secondary : .primary)
                    .help("公网出口位置")
                Spacer()
                Label(Format.rate(desktop?.downloadBps), systemImage: "arrow.down").foregroundStyle(.blue)
                Label(Format.rate(desktop?.uploadBps), systemImage: "arrow.up").foregroundStyle(.purple)
            }
            .font(.callout.monospacedDigit())
            .labelStyle(CompactLabelStyle())
            .padding(.horizontal, 2)

            Divider()
            HStack(spacing: 14) {
                Button(action: openSettings) { Label("设置", systemImage: "gearshape") }
                Button(action: openDashboard) { Label("详细页面", systemImage: "safari") }
                Spacer()
                Button("退出", action: quit)
            }
            .buttonStyle(.borderless)
            .font(.callout)
        }
        .padding(12)
        .frame(width: 340)
    }
}

/// Icon and text with a tight gap, for the one-line network summary.
struct CompactLabelStyle: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(spacing: 2) { configuration.icon.imageScale(.small); configuration.title }
    }
}

// MARK: - Launch at login

/// Wraps SMAppService.mainApp: the same login item that System Settings →
/// General → Login Items lists, so either place can turn it off.
enum LaunchAtLogin {
    static var isEnabled: Bool { SMAppService.mainApp.status == .enabled }
    static var needsApproval: Bool { SMAppService.mainApp.status == .requiresApproval }

    /// Returns a message for the user when the change did not fully apply.
    static func set(_ enabled: Bool) -> String? {
        do {
            if enabled { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
        } catch {
            if !enabled && SMAppService.mainApp.status == .notRegistered { return nil }
            return "无法更改登录项：\(error.localizedDescription)"
        }
        if enabled && needsApproval {
            SMAppService.openSystemSettingsLoginItems()
            return "请在“系统设置 → 通用 → 登录项”中允许 MiniDisplay Bridge。"
        }
        return nil
    }
}

// MARK: - Settings window

final class SettingsModel: ObservableObject {
    @Published var draft: DisplaySettings?
    @Published private(set) var ports: [String] = []
    @Published private(set) var message: String?
    @Published private(set) var failed = false
    @Published private(set) var busy = false
    @Published var showQuotaInMenuBar: Bool {
        didSet { UserDefaults.standard.set(showQuotaInMenuBar, forKey: menuBarQuotaKey); onMenuBarChange?() }
    }
    var onMenuBarChange: (() -> Void)?
    /// Mirrors the system login item; refreshed whenever the window opens.
    @Published var launchAtLogin = LaunchAtLogin.isEnabled {
        didSet {
            guard launchAtLogin != oldValue, launchAtLogin != LaunchAtLogin.isEnabled else { return }
            if let problem = LaunchAtLogin.set(launchAtLogin) {
                failed = true
                message = problem
            }
            if !LaunchAtLogin.needsApproval { launchAtLogin = LaunchAtLogin.isEnabled }
        }
    }
    private var saved: DisplaySettings?
    private var revision = 0
    private var token = ""
    private let api: BridgeAPI

    init(api: BridgeAPI) {
        self.api = api
        showQuotaInMenuBar = UserDefaults.standard.bool(forKey: menuBarQuotaKey)
    }

    var dirty: Bool { draft != nil && draft != saved }

    func refreshLaunchAtLogin() {
        let current = LaunchAtLogin.isEnabled || LaunchAtLogin.needsApproval
        if launchAtLogin != current { launchAtLogin = current }
    }

    func load() {
        busy = true
        api.settings { [weak self] result in self?.apply(result, success: nil) }
    }

    func save() {
        guard let draft = draft else { return }
        busy = true
        api.save(draft, revision: revision, token: token) { [weak self] result in
            self?.apply(result, success: "已保存并应用到小屏。")
        }
    }

    func reconnect() {
        busy = true
        api.reconnect(token: token) { [weak self] result in
            self?.apply(result, success: "正在重新连接 USB…", keepDraft: true)
        }
    }

    private func apply(_ result: Result<SettingsPayload, Error>, success: String?, keepDraft: Bool = false) {
        busy = false
        switch result {
        case .success(let payload):
            saved = payload.settings
            if !keepDraft || draft == nil { draft = payload.settings }
            revision = payload.revision
            token = payload.token
            ports = payload.ports
            failed = payload.warning != nil
            message = payload.warning ?? success
        case .failure(let error):
            failed = true
            message = (error as NSError).code == 409 ? "设置已在其他地方修改，已重新载入。" : error.localizedDescription
            if (error as NSError).code == 409 || (error as NSError).code == 403 { load() }
        }
    }
}

struct SettingsView: View {
    @ObservedObject var model: SettingsModel

    var body: some View {
        Group {
            if model.draft != nil {
                form
            } else {
                VStack(spacing: 8) {
                    Text(model.message ?? "正在读取设置…").foregroundStyle(.secondary)
                    Button("重试") { model.load() }
                }
                .frame(maxWidth: .infinity, minHeight: 200)
            }
        }
        .frame(width: 420)
    }

    private func binding<T>(_ keyPath: WritableKeyPath<DisplaySettings, T>) -> Binding<T> {
        Binding(get: { model.draft![keyPath: keyPath] }, set: { model.draft![keyPath: keyPath] = $0 })
    }

    private func brightness(_ title: String, _ keyPath: WritableKeyPath<DisplaySettings, Int>, help: String) -> some View {
        LabeledContent {
            HStack {
                // Continuous track (no tick marks), snapped to 5 % steps.
                Slider(value: Binding(get: { Double(model.draft![keyPath: keyPath]) },
                                      set: { model.draft![keyPath: keyPath] = Int(($0 / 5).rounded()) * 5 }),
                       in: 0...100)
                Text("\(model.draft![keyPath: keyPath])%").monospacedDigit().frame(width: 38, alignment: .trailing)
            }
        } label: {
            Text(title)
        }
        .help(help)
    }

    private var hours: some View {
        ForEach(0..<24, id: \.self) { Text(String(format: "%02d:00", $0)).tag($0) }
    }

    private var form: some View {
        VStack(spacing: 0) {
            Form {
                Section("通用") {
                    Toggle("登录时自动启动", isOn: $model.launchAtLogin)
                        .help("与“系统设置 → 通用 → 登录项”同步。")
                }
                Section("亮度") {
                    brightness("日间", \.dayBrightness, help: "0% 关闭背光，数据继续更新。")
                    brightness("夜间上限", \.nightBrightness, help: "夜间时段的上限，不会高于日间亮度。")
                    brightness("断线上限", \.offlineBrightness, help: "4 秒收不到数据后的上限。")
                    LabeledContent("夜间时段") {
                        HStack(spacing: 4) {
                            Picker("开始", selection: binding(\.nightStartHour)) { hours }.labelsHidden().fixedSize()
                            Text("至").foregroundStyle(.secondary)
                            Picker("结束", selection: binding(\.nightEndHour)) { hours }.labelsHidden().fixedSize()
                        }
                    }
                    .help("起止相同即关闭夜间调暗，支持跨午夜。")
                }
                Section("连接") {
                    LabeledContent("USB 端口") {
                        HStack {
                            Picker("USB 端口", selection: binding(\.serialPort)) {
                                Text("自动识别").tag(String?.none)
                                ForEach(model.ports, id: \.self) { Text($0).tag(Optional($0)) }
                            }
                            .labelsHidden()
                            Button("重新连接") { model.reconnect() }
                        }
                    }
                    TextField("天气城市代码", text: binding(\.weatherCityCode), prompt: Text("0 为自动识别"))
                        .help("9 位 weather.com.cn 城市代码，填 0 按 IP 自动识别。")
                }
                Section {
                    Toggle("连接 Claude 账户额度", isOn: binding(\.claudeEnabled))
                    Toggle("菜单栏显示最低 5 小时余量", isOn: $model.showQuotaInMenuBar)
                } header: {
                    Text("账户额度")
                } footer: {
                    Text("读取本机 Claude Code 的登录，令牌只发送给 Anthropic 官方额度服务，不保存、不调用模型。")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            .fixedSize(horizontal: false, vertical: true)

            HStack {
                if let message = model.message {
                    Text(message).font(.caption).foregroundStyle(model.failed ? .red : .secondary)
                }
                Spacer()
                Button("保存并应用") { model.save() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(!model.dirty || model.busy)
            }
            .padding(.horizontal, 20)
            .padding(.bottom, 16)
        }
    }
}

// MARK: - App shell

final class MenuBarApp: NSObject, NSApplicationDelegate, NSPopoverDelegate {
    private var statusItem: NSStatusItem!
    private let popover = NSPopover()
    private var settingsWindow: NSWindow?
    private var worker: Process?
    private var quitting = false
    private var workerFailures = 0
    private var signalSources: [DispatchSourceSignal] = []
    private var titleSubscription: AnyCancellable?
    private let baseURL: URL
    private let api: BridgeAPI
    private let status: StatusModel
    private let settings: SettingsModel

    override init() {
        var port = Int(ProcessInfo.processInfo.environment["CODEX_BRIDGE_PORT"] ?? "8766") ?? 8766
        let arguments = Array(CommandLine.arguments.dropFirst())
        for (index, argument) in arguments.enumerated() {
            if argument == "--listen-port", index + 1 < arguments.count { port = Int(arguments[index + 1]) ?? port }
            if argument.hasPrefix("--listen-port=") { port = Int(argument.dropFirst(14)) ?? port }
        }
        baseURL = URL(string: "http://127.0.0.1:\((1...65535).contains(port) ? port : 8766)/")!
        api = BridgeAPI(baseURL: baseURL)
        status = StatusModel(api: api)
        settings = SettingsModel(api: api)
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.image = menuBarImage()
            button.imagePosition = .imageLeading
            button.toolTip = "MiniDisplay Bridge · 点击查看状态，右键打开菜单"
            button.setAccessibilityLabel("MiniDisplay Bridge 状态与设置")
            button.target = self
            button.action = #selector(statusClicked)
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }
        popover.behavior = .transient
        popover.delegate = self
        popover.contentViewController = NSHostingController(rootView: StatusView(
            model: status,
            openSettings: { [weak self] in self?.showSettings() },
            openDashboard: { [weak self] in self?.openDashboard() },
            quit: { NSApp.terminate(nil) }))

        let appMenu = NSMenu()
        let root = NSMenuItem()
        let submenu = NSMenu()
        submenu.addItem(withTitle: "关闭窗口", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        submenu.addItem(withTitle: "退出 MiniDisplay Bridge", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        root.submenu = submenu
        appMenu.addItem(root)
        // Standard edit commands so copy/paste work in the settings text field.
        let editRoot = NSMenuItem()
        let edit = NSMenu(title: "编辑")
        edit.addItem(withTitle: "剪切", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "拷贝", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "粘贴", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "全选", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editRoot.submenu = edit
        appMenu.addItem(editRoot)
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
        titleSubscription = status.$overview.receive(on: RunLoop.main).sink { [weak self] _ in
            DispatchQueue.main.async { self?.updateTitle() }
        }
        settings.onMenuBarChange = { [weak self] in self?.menuBarPreferenceChanged() }
        startWorker()
        menuBarPreferenceChanged()
        NSLog("MiniDisplay menu bar ready (visible: %@)", statusItem.isVisible ? "yes" : "no")
    }

    private func menuBarPreferenceChanged() {
        if !popover.isShown {
            if settings.showQuotaInMenuBar { status.poll(every: 30) } else { status.stopPolling() }
        }
        updateTitle()
    }

    private func updateTitle() {
        guard let button = statusItem?.button else { return }
        if settings.showQuotaInMenuBar, let lowest = status.lowestFiveHour {
            button.title = " " + Format.percent(lowest)
            button.font = NSFont.monospacedDigitSystemFont(ofSize: NSFont.smallSystemFontSize + 1, weight: .medium)
        } else {
            button.title = ""
        }
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
                if self.quitting { return }
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
            popover.performClose(nil)
            let menu = NSMenu()
            for (title, action) in [("打开状态面板", #selector(showStatus)), ("显示屏设置…", #selector(showSettings)),
                                    ("在浏览器中打开详细页面", #selector(openDashboard))] {
                let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
                item.target = self
                menu.addItem(item)
            }
            menu.addItem(.separator())
            let login = NSMenuItem(title: "登录时自动启动", action: #selector(toggleLaunchAtLogin), keyEquivalent: "")
            login.target = self
            login.state = LaunchAtLogin.isEnabled ? .on : .off
            menu.addItem(login)
            menu.addItem(.separator())
            menu.addItem(withTitle: "退出 MiniDisplay Bridge", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
            statusItem.menu = menu
            statusItem.button?.performClick(nil)
            statusItem.menu = nil
        } else if popover.isShown {
            popover.performClose(nil)
        } else {
            showStatus()
        }
    }

    @objc func showStatus() {
        guard let button = statusItem.button else { return }
        status.poll(every: 1)
        popover.show(relativeTo: button.bounds, of: button, preferredEdge: .minY)
        popover.contentViewController?.view.window?.makeKey()
        NSApp.activate(ignoringOtherApps: true)
    }

    func popoverDidClose(_ notification: Notification) {
        menuBarPreferenceChanged()
    }

    @objc private func showSettings() {
        popover.performClose(nil)
        if settingsWindow == nil {
            let window = NSWindow(contentViewController: NSHostingController(rootView: SettingsView(model: settings)))
            window.title = "MiniDisplay 设置"
            window.styleMask = [.titled, .closable]
            window.isReleasedWhenClosed = false
            window.center()
            settingsWindow = window
        }
        // Reopening reloads saved values unless the form has unsaved edits.
        settings.refreshLaunchAtLogin()
        if !settings.dirty { settings.load() }
        settingsWindow?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func toggleLaunchAtLogin() {
        if let problem = LaunchAtLogin.set(!LaunchAtLogin.isEnabled) { showError(problem) }
        settings.refreshLaunchAtLogin()
    }

    @objc private func openDashboard() {
        popover.performClose(nil)
        NSWorkspace.shared.open(baseURL)
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
        // AppKit can stop servicing the main dispatch queue after terminateLater.
        // Reap this owned child before returning, with a background deadline so
        // shutdown never depends on a completion queued on that same main loop.
        let deadline = DispatchWorkItem {
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
        }
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + 8, execute: deadline)
        process.waitUntilExit()
        deadline.cancel()
        return .terminateNow
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

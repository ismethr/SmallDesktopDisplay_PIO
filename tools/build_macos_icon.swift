#!/usr/bin/env swift

import Foundation

private struct IconChunk {
    let type: String
    let size: Int
}

private let chunks = [
    IconChunk(type: "icp4", size: 16),
    IconChunk(type: "icp5", size: 32),
    IconChunk(type: "ic11", size: 32),
    IconChunk(type: "icp6", size: 64),
    IconChunk(type: "ic12", size: 64),
    IconChunk(type: "ic07", size: 128),
    IconChunk(type: "ic08", size: 256),
    IconChunk(type: "ic13", size: 256),
    IconChunk(type: "ic09", size: 512),
    IconChunk(type: "ic14", size: 512),
    IconChunk(type: "ic10", size: 1024),
]

private func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("\(message)\n".utf8))
    exit(2)
}

private func appendBigEndian(_ value: UInt32, to data: inout Data) {
    var encoded = value.bigEndian
    withUnsafeBytes(of: &encoded) { data.append(contentsOf: $0) }
}

private func resize(source: URL, output: URL, size: Int) throws {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/bin/sips")
    process.arguments = [
        "-z", String(size), String(size),
        source.path,
        "--out", output.path,
    ]
    process.standardOutput = FileHandle.nullDevice
    process.standardError = FileHandle.standardError
    try process.run()
    process.waitUntilExit()
    guard process.terminationStatus == 0 else {
        throw NSError(
            domain: "MiniDisplayIconBuilder",
            code: Int(process.terminationStatus),
            userInfo: [NSLocalizedDescriptionKey: "sips failed for \(size)x\(size)"]
        )
    }
}

guard CommandLine.arguments.count == 3 else {
    fail("usage: swift tools/build_macos_icon.swift SOURCE.png OUTPUT.icns")
}

let source = URL(fileURLWithPath: CommandLine.arguments[1]).standardizedFileURL
let output = URL(fileURLWithPath: CommandLine.arguments[2]).standardizedFileURL
guard FileManager.default.fileExists(atPath: source.path) else {
    fail("source icon was not found: \(source.path)")
}

let temporaryDirectory = FileManager.default.temporaryDirectory
    .appendingPathComponent("MiniDisplayBridgeIcon-\(UUID().uuidString)", isDirectory: true)

do {
    try FileManager.default.createDirectory(
        at: temporaryDirectory,
        withIntermediateDirectories: true
    )
    defer { try? FileManager.default.removeItem(at: temporaryDirectory) }

    var pngBySize: [Int: Data] = [:]
    for size in Set(chunks.map(\.size)).sorted() {
        let resized = temporaryDirectory.appendingPathComponent("icon-\(size).png")
        try resize(source: source, output: resized, size: size)
        pngBySize[size] = try Data(contentsOf: resized)
    }

    var body = Data()
    for chunk in chunks {
        guard let png = pngBySize[chunk.size],
              let type = chunk.type.data(using: .ascii),
              type.count == 4 else {
            fail("could not create \(chunk.type) icon chunk")
        }
        body.append(type)
        appendBigEndian(UInt32(png.count + 8), to: &body)
        body.append(png)
    }

    var icon = Data("icns".utf8)
    appendBigEndian(UInt32(body.count + 8), to: &icon)
    icon.append(body)
    try FileManager.default.createDirectory(
        at: output.deletingLastPathComponent(),
        withIntermediateDirectories: true
    )
    try icon.write(to: output, options: .atomic)
    print("macOS icon: \(output.path)")
} catch {
    fail("could not build macOS icon: \(error.localizedDescription)")
}

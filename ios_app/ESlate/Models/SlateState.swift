import Foundation

// MARK: - Clapperboard State Model

public struct SlateState: Codable, Equatable {
    public static let defaultVisibilities: [String: Bool] = [
        "roll": true,
        "scene": true,
        "take": true,
        "prod": true,
        "dir": true,
        "dop": true,
        "cam": true,
        "sound": true,
        "loc": true,
        "light": true,
        "date": true,
        "timecode": true,
        "note": true,
        "bat": true
    ]

    public var roll: String
    public var scene: String
    public var take: String
    public var prod: String
    public var director: String
    public var dop: String
    public var note: String
    public var date: String
    public var timecode: String
    public var tc_slot: Int
    public var tc_size: Int
    public var tc_fps: String
    public var tc_running: Bool
    public var cam: String
    public var sound: String
    public var loc: String
    public var light: String
    public var layout: Int
    public var invert: Bool
    public var vis: [String: Bool]

    public var timecodeSlot: Int {
        get { tc_slot }
        set { tc_slot = newValue }
    }
    public var timecodeSize: Int {
        get { tc_size }
        set { tc_size = newValue }
    }
    public var timecodeFps: String {
        get { tc_fps }
        set { tc_fps = newValue }
    }
    public var timecodeRunning: Bool {
        get { tc_running }
        set { tc_running = newValue }
    }

    public init(
        roll: String = "A001",
        scene: String = "35A",
        take: String = "1",
        prod: String = "NEBRALTA 🌸",
        director: String = "DIRECTOR",
        dop: String = "FELIPELOTAS1",
        note: String = "ISO 120",
        date: String = "25.09.2026",
        timecode: String = "00:00:00",
        tc_slot: Int = 0,
        tc_size: Int = 1,
        tc_fps: String = "24",
        tc_running: Bool = true,
        cam: String = "A",
        sound: String = "SYNC",
        loc: String = "INT",
        light: String = "DAY",
        layout: Int = 0,
        invert: Bool = false,
        vis: [String: Bool]? = nil
    ) {
        self.roll = roll.uppercased()
        self.scene = scene.uppercased()
        self.take = take.uppercased()
        self.prod = prod.uppercased()
        self.director = director.uppercased()
        self.dop = dop.uppercased()
        self.note = note.uppercased()
        self.date = date.uppercased()
        self.timecode = timecode.uppercased()
        self.tc_slot = max(0, min(2, tc_slot))
        self.tc_size = max(0, min(2, tc_size))
        self.tc_fps = tc_fps
        self.tc_running = tc_running
        self.cam = cam.uppercased()
        self.sound = sound.uppercased()
        self.loc = loc.uppercased()
        self.light = light.uppercased()
        self.layout = max(0, min(2, layout))
        self.invert = invert
        self.vis = Self.defaultVisibilities
        if let customVis = vis {
            for (key, val) in customVis {
                self.vis[key] = val
            }
        }
    }

    private enum CodingKeys: String, CodingKey {
        case roll, scene, take, prod, dop, note, date, cam, sound, loc, light
        case dir
        case director
        case layout
        case layoutMode = "layout_mode"
        case invert
        case invertedMode = "inverted_mode"
        case vis
        case visibilities
        case timecode
        case tc
        case tc_slot
        case tc_size
        case tc_fps
        case tc_running
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)

        let rawRoll = try container.decodeIfPresent(String.self, forKey: .roll) ?? "A001"
        let rawScene = try container.decodeIfPresent(String.self, forKey: .scene) ?? "35A"
        let rawTake = try container.decodeIfPresent(String.self, forKey: .take) ?? "1"
        let rawProd = try container.decodeIfPresent(String.self, forKey: .prod) ?? "NEBRALTA 🌸"
        let rawDir = try container.decodeIfPresent(String.self, forKey: .dir)
            ?? container.decodeIfPresent(String.self, forKey: .director)
            ?? "DIRECTOR"
        let rawDop = try container.decodeIfPresent(String.self, forKey: .dop) ?? "FELIPELOTAS1"
        let rawNote = try container.decodeIfPresent(String.self, forKey: .note) ?? "ISO 120"
        let rawDate = try container.decodeIfPresent(String.self, forKey: .date) ?? "25.09.2026"
        let rawTimecode = try container.decodeIfPresent(String.self, forKey: .timecode)
            ?? container.decodeIfPresent(String.self, forKey: .tc)
            ?? "00:00:00"
        let rawTcSlot = try container.decodeIfPresent(Int.self, forKey: .tc_slot) ?? 0
        let rawTcSize = try container.decodeIfPresent(Int.self, forKey: .tc_size) ?? 1
        let rawTcFps = try container.decodeIfPresent(String.self, forKey: .tc_fps) ?? "24"
        let rawTcRunning = try container.decodeIfPresent(Bool.self, forKey: .tc_running) ?? true
        let rawCam = try container.decodeIfPresent(String.self, forKey: .cam) ?? "A"
        let rawSound = try container.decodeIfPresent(String.self, forKey: .sound) ?? "SYNC"
        let rawLoc = try container.decodeIfPresent(String.self, forKey: .loc) ?? "INT"
        let rawLight = try container.decodeIfPresent(String.self, forKey: .light) ?? "DAY"

        let rawLayout = try container.decodeIfPresent(Int.self, forKey: .layout)
            ?? container.decodeIfPresent(Int.self, forKey: .layoutMode)
            ?? 0
        let rawInvert = try container.decodeIfPresent(Bool.self, forKey: .invert)
            ?? container.decodeIfPresent(Bool.self, forKey: .invertedMode)
            ?? false

        let rawVis = try container.decodeIfPresent([String: Bool].self, forKey: .vis)
            ?? container.decodeIfPresent([String: Bool].self, forKey: .visibilities)

        self.init(
            roll: rawRoll,
            scene: rawScene,
            take: rawTake,
            prod: rawProd,
            director: rawDir,
            dop: rawDop,
            note: rawNote,
            date: rawDate,
            timecode: rawTimecode,
            tc_slot: rawTcSlot,
            tc_size: rawTcSize,
            tc_fps: rawTcFps,
            tc_running: rawTcRunning,
            cam: rawCam,
            sound: rawSound,
            loc: rawLoc,
            light: rawLight,
            layout: rawLayout,
            invert: rawInvert,
            vis: rawVis
        )
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(roll.uppercased(), forKey: .roll)
        try container.encode(scene.uppercased(), forKey: .scene)
        try container.encode(take.uppercased(), forKey: .take)
        try container.encode(prod.uppercased(), forKey: .prod)
        try container.encode(director.uppercased(), forKey: .dir)
        try container.encode(dop.uppercased(), forKey: .dop)
        try container.encode(note.uppercased(), forKey: .note)
        try container.encode(date.uppercased(), forKey: .date)
        try container.encode(timecode.uppercased(), forKey: .timecode)
        try container.encode(tc_slot, forKey: .tc_slot)
        try container.encode(tc_size, forKey: .tc_size)
        try container.encode(tc_fps, forKey: .tc_fps)
        try container.encode(tc_running, forKey: .tc_running)
        try container.encode(cam.uppercased(), forKey: .cam)
        try container.encode(sound.uppercased(), forKey: .sound)
        try container.encode(loc.uppercased(), forKey: .loc)
        try container.encode(light.uppercased(), forKey: .light)
        try container.encode(layout, forKey: .layout)
        try container.encode(invert, forKey: .invert)
        try container.encode(vis, forKey: .vis)
    }

    // MARK: - Alphanumeric Increment Algorithms

    public mutating func incrementTake() {
        self.take = Self.computeIncrementTake(self.take)
    }

    public mutating func incrementRoll() {
        self.roll = Self.computeIncrementRoll(self.roll)
    }

    public mutating func syncDateFromSystem() {
        let formatter = DateFormatter()
        formatter.dateFormat = "dd.MM.yyyy"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        self.date = formatter.string(from: Date()).uppercased()
    }

    public static func computeIncrementTake(_ takeStr: String) -> String {
        let trimmed = takeStr.trimmingCharacters(in: .whitespacesAndNewlines)
        if let val = Int(trimmed), val >= 0 {
            return String(val + 1)
        }

        var digitCount = 0
        for char in trimmed {
            if char.isNumber {
                digitCount += 1
            } else {
                break
            }
        }

        if digitCount > 0 {
            let prefixIndex = trimmed.index(trimmed.startIndex, offsetBy: digitCount)
            let digitsSub = String(trimmed[..<prefixIndex])
            let suffixSub = String(trimmed[prefixIndex...])
            if let num = Int(digitsSub) {
                return "\(num + 1)\(suffixSub)"
            }
        }
        return "1"
    }

    public static func computeIncrementRoll(_ rollStr: String) -> String {
        let trimmed = rollStr.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            return "A001"
        }

        var numStart: String.Index?
        for idx in trimmed.indices {
            if trimmed[idx].isNumber {
                numStart = idx
                break
            }
        }

        if let start = numStart {
            var numEnd = start
            while numEnd < trimmed.endIndex && trimmed[numEnd].isNumber {
                numEnd = trimmed.index(after: numEnd)
            }

            let prefix = String(trimmed[..<start])
            let numStr = String(trimmed[start..<numEnd])
            let suffix = String(trimmed[numEnd...])
            let numLen = numStr.count

            if let val = Int(numStr) {
                let nextVal = val + 1
                let formatted = String(format: "%0\(numLen)d", nextVal)
                return "\(prefix)\(formatted)\(suffix)"
            }
        } else if trimmed.count == 1, let firstChar = trimmed.first, firstChar.isLetter {
            let upper = Character(firstChar.uppercased())
            if upper == "Z" {
                return "A"
            }
            if let ascii = upper.asciiValue {
                let nextChar = Character(UnicodeScalar(ascii + 1))
                return String(nextChar)
            }
        }
        return "A001"
    }

    // MARK: - Timecode Camera Jam & Math Engine

    public mutating func jamSyncTOD() {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        self.timecode = formatter.string(from: Date())
        self.tc_running = true
    }

    public mutating func applySecondOffset(seconds deltaSeconds: Int) {
        let (h, m, s) = parseTimecodeComponents()
        var totalSec = h * 3600 + m * 60 + s + deltaSeconds
        let daySec = 86400
        while totalSec < 0 { totalSec += daySec }
        totalSec %= daySec
        self.timecode = String(format: "%02d:%02d:%02d", totalSec / 3600, (totalSec % 3600) / 60, totalSec % 60)
    }

    public mutating func applyFrameOffset(frames deltaFrames: Int) {
        let fpsVal = TimecodeFps(rawValue: tc_fps)?.frameRateValue ?? 24.0
        let (h, m, s) = parseTimecodeComponents()
        let totalFrames = Int(round(Double(h * 3600 + m * 60 + s) * fpsVal)) + deltaFrames
        let framesInDay = Int(round(86400.0 * fpsVal))
        var wrappedFrames = totalFrames % framesInDay
        while wrappedFrames < 0 { wrappedFrames += framesInDay }

        let totalSeconds = Int(floor((Double(wrappedFrames) + 0.5) / fpsVal))
        self.timecode = String(format: "%02d:%02d:%02d", (totalSeconds / 3600) % 24, (totalSeconds % 3600) / 60, totalSeconds % 60)
    }

    public func parseTimecodeComponents() -> (Int, Int, Int) {
        let parts = timecode.split(separator: ":").compactMap { Int($0) }
        let h = parts.count > 0 ? max(0, min(23, parts[0])) : 0
        let m = parts.count > 1 ? max(0, min(59, parts[1])) : 0
        let s = parts.count > 2 ? max(0, min(59, parts[2])) : 0
        return (h, m, s)
    }
}

// MARK: - Timecode Enums

public enum TimecodeSlot: Int, Codable, CaseIterable {
    case topHeader = 0
    case lowerBar = 1
    case centerHero = 2

    public var displayName: String {
        switch self {
        case .topHeader: return "Top Header (Superior)"
        case .lowerBar: return "Lower Bar (Inferior)"
        case .centerHero: return "Center Hero (Central)"
        }
    }

    public var shortName: String {
        switch self {
        case .topHeader: return "TOP"
        case .lowerBar: return "BAR"
        case .centerHero: return "HERO"
        }
    }
}

public enum TimecodeSize: Int, Codable, CaseIterable {
    case medium = 0
    case large = 1
    case giantXL = 2

    public var displayName: String {
        switch self {
        case .medium: return "Medio (24px)"
        case .large: return "Grande (40px)"
        case .giantXL: return "Gigante XL (64px)"
        }
    }

    public var shortName: String {
        switch self {
        case .medium: return "MED"
        case .large: return "LRG"
        case .giantXL: return "XL"
        }
    }
}

public enum TimecodeFps: String, Codable, CaseIterable {
    case fps23_976 = "23.976"
    case fps24 = "24"
    case fps25 = "25"
    case fps29_97 = "29.97"
    case fps30 = "30"

    public var frameRateValue: Double {
        switch self {
        case .fps23_976: return 23.976
        case .fps24: return 24.0
        case .fps25: return 25.0
        case .fps29_97: return 29.97
        case .fps30: return 30.0
        }
    }

    public var opcodeIndex: UInt8 {
        switch self {
        case .fps23_976: return 0
        case .fps24: return 1
        case .fps25: return 2
        case .fps29_97: return 3
        case .fps30: return 4
        }
    }
}

// MARK: - Slate Command Opcodes

public enum SlateCommandOpcode: UInt8 {
    case incrementTake = 0x01
    case incrementRoll = 0x02
    case toggleInvert = 0x03
    case forceRefresh = 0x04
    case dismissBoot = 0x05
    case setLayout = 0x06
    case toggleTimecode = 0x07
    case setTimecodeSlot = 0x08
    case setTimecodeSize = 0x09
    case setTimecodeFps = 0x0A
    case setTimecodeVisibility = 0x0B
    case jamSyncTOD = 0x0C

    public func buildPacket(parameter: UInt8? = nil) -> Data {
        if let param = parameter {
            return Data([self.rawValue, param])
        } else {
            return Data([self.rawValue])
        }
    }

    public func buildPacket(parameters: [UInt8]) -> Data {
        var data = Data([self.rawValue])
        data.append(contentsOf: parameters)
        return data
    }
}

// MARK: - Hardware Button Event

public struct ButtonEvent: Equatable {
    public enum EventCode: UInt8 {
        case key1Take = 0x01
        case key2Roll = 0x02
        case key0Invert = 0x03
        case bootDismiss = 0x04
        case unknown = 0xFF
    }

    public let code: EventCode
    public let counter: UInt16

    public init(data: Data) {
        guard data.count >= 3 else {
            self.code = .unknown
            self.counter = 0
            return
        }
        self.code = EventCode(rawValue: data[0]) ?? .unknown
        let high = UInt16(data[1])
        let low = UInt16(data[2])
        self.counter = (high << 8) | low
    }
}

// MARK: - Battery Telemetry

public struct BatteryTelemetry: Equatable {
    public let millivolts: UInt16
    public let percentage: UInt8
    public let flags: UInt8

    public var isChargingOrFull: Bool {
        return (flags & 0x01) != 0
    }

    public var isLowBattery: Bool {
        return (flags & 0x02) != 0
    }

    public var voltageString: String {
        let volts = Double(millivolts) / 1000.0
        return String(format: "%.2fV", volts)
    }

    public init(millivolts: UInt16 = 4150, percentage: UInt8 = 100, flags: UInt8 = 0x01) {
        self.millivolts = millivolts
        self.percentage = percentage
        self.flags = flags
    }

    public init?(data: Data) {
        guard data.count >= 4 else { return nil }
        let high = UInt16(data[0])
        let low = UInt16(data[1])
        self.millivolts = (high << 8) | low
        self.percentage = data[2]
        self.flags = data[3]
    }
}

// MARK: - BLE Packet Chunker

public struct BLEPacketChunker {
    public static func chunkPayload(_ payload: Data, mtu: Int = 23) -> [Data] {
        let maxAttPayload = mtu - 3
        let maxSliceSize = min(max(maxAttPayload - 3, 1), 240)

        if payload.count <= maxSliceSize {
            var chunk = Data([0x00, 0x01, UInt8(payload.count)])
            chunk.append(payload)
            return [chunk]
        }

        let totalChunks = Int(ceil(Double(payload.count) / Double(maxSliceSize)))
        guard totalChunks <= 255 else { return [] }

        var chunks: [Data] = []
        for seq in 0..<totalChunks {
            let start = seq * maxSliceSize
            let end = min(start + maxSliceSize, payload.count)
            let slice = payload.subdata(in: start..<end)
            var chunk = Data([UInt8(seq), UInt8(totalChunks), UInt8(slice.count)])
            chunk.append(slice)
            chunks.append(chunk)
        }
        return chunks
    }
}

// MARK: - BLE Reassembly Engine

public class BLEReassemblyEngine {
    public enum ReassemblyStatus {
        case inProgress
        case complete(Data)
        case error(String)
    }

    private var buffer = Data()
    private var expectedSeq: UInt8 = 0
    private var totalChunks: UInt8 = 0
    private var startTimestamp: Date?
    private var inProgress = false
    private let guardTimeout: TimeInterval = 3.0

    public init() {}

    public func reset() {
        buffer.removeAll()
        expectedSeq = 0
        totalChunks = 0
        startTimestamp = nil
        inProgress = false
    }

    public func feedChunk(_ chunk: Data) -> ReassemblyStatus {
        let now = Date()

        guard chunk.count >= 3 else {
            reset()
            return .error("ERROR_HEADER_TOO_SHORT")
        }

        let seq = chunk[0]
        let total = chunk[1]
        let chunkLen = Int(chunk[2])
        let payload = chunk.subdata(in: 3..<chunk.count)

        guard payload.count == chunkLen else {
            reset()
            return .error("ERROR_LENGTH_MISMATCH")
        }

        guard total >= 1, seq < total else {
            reset()
            return .error("ERROR_INVALID_HEADER")
        }

        if inProgress, let start = startTimestamp, now.timeIntervalSince(start) > guardTimeout {
            reset()
            if seq != 0 {
                return .error("ERROR_TIMEOUT")
            }
        }

        if !inProgress {
            guard seq == 0 else {
                reset()
                return .error("ERROR_OUT_OF_ORDER")
            }
            inProgress = true
            expectedSeq = 0
            totalChunks = total
            startTimestamp = now
            buffer.removeAll()
        } else {
            guard seq == expectedSeq, total == totalChunks else {
                reset()
                return .error("ERROR_OUT_OF_ORDER")
            }
        }

        buffer.append(payload)
        expectedSeq += 1

        if expectedSeq == totalChunks {
            let result = buffer
            reset()
            return .complete(result)
        }

        return .inProgress
    }
}

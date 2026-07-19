// On-disk library under Documents/ (visible in the Files app via
// UIFileSharingEnabled, so users can drop ROMs — and sibling .sav files —
// straight into roms/):
//   roms/    imported or dropped .gb / .gbc (+ optional sibling .sav / .rplg)
//   saves/   battery RAM, one <name>.sav per ROM basename (+ mgb.sav)
//   states/  savestates, <name>.ss<slot>
import Foundation
import RetroPlugKit

// A thin desktop `.rplg` project sidecar — raw JSON only (the `.rplg.zip`
// export variant is not supported here). Decoding is forward-tolerant like the
// desktop's role-config path: unknown keys are ignored, and only the first
// system's SameBoy role is read — enough to carry model + fast-boot across.
struct RplgProject: Decodable {
    struct System: Decodable {
        let romPath: String?
        let roles: [Role]?
    }
    struct Role: Decodable {
        let kind: String?
        let config: Config?
    }
    // One forward-tolerant bag for every role we read — the sameboy role uses
    // model/fastBoot, the lsdj-sync role uses mode/tempoDivisor/autoStart.
    struct Config: Decodable {
        let model: String?
        let fastBoot: Bool?
        let mode: String?
        let tempoDivisor: Int?
        let autoStart: Bool?

        // Desktop MODEL_VALUES strings (settingsEnums.ts) → the bridge enum.
        var sameboyModel: RPSameBoyModel? {
            switch model {
            case "auto":   return .auto
            case "dmgB":   return .dmgB
            case "mgb":    return .mgb
            case "sgb":    return .sgb
            case "sgbPal": return .sgbPal
            case "sgb2":   return .sgb2
            case "cgb0":   return .cgb0
            case "cgbA":   return .cgbA
            case "cgbB":   return .cgbB
            case "cgbC":   return .cgbC
            case "cgbD":   return .cgbD
            case "cgbE":   return .cgbE
            case "agb":    return .agb
            case "gbp":    return .gbp
            default:       return nil
            }
        }

        // Desktop LSDJ_MODE_VALUES strings (settingsEnums.ts) → the bridge
        // enum. "keyboard" maps to nil (it needs a host key feed — a later
        // phase on desktop too), keeping the current mode rather than
        // half-applying.
        var midiSyncMode: RPMidiSyncMode? {
            switch mode {
            case "off":                return .off
            case "midiPassthrough":    return .mgb
            case "midiSync":           return .midiSync
            case "midiSyncArduinoboy": return .midiSyncArduinoboy
            case "midiMap":            return .midiMap
            case "keyboardMidi":       return .keyboardMidi
            case "midiOut":            return .midiOut
            case "masterSync":         return .masterSync
            default:                   return nil
            }
        }
    }
    let schemaVersion: String?
    let systems: [System]?

    var sameboyConfig: Config? {
        systems?.first?.roles?.first { $0.kind == "sameboy" }?.config
    }

    var lsdjSyncConfig: Config? {
        systems?.first?.roles?.first { $0.kind == "lsdj-sync" }?.config
    }
}

struct RomEntry: Identifiable, Equatable, Hashable {
    let fileName: String
    var id: String { fileName }
    var displayName: String { (fileName as NSString).deletingPathExtension }
}

@MainActor
final class RomLibrary: ObservableObject {
    // Sorted most-recently-played first, then by name.
    @Published private(set) var roms: [RomEntry] = []

    let romsDir: URL
    let savesDir: URL
    let statesDir: URL

    private let fm = FileManager.default
    private let playsKey = "library.lastPlayed" // [fileName: Date]

    init() {
        let docs = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        romsDir = docs.appendingPathComponent("roms", isDirectory: true)
        savesDir = docs.appendingPathComponent("saves", isDirectory: true)
        statesDir = docs.appendingPathComponent("states", isDirectory: true)
        for dir in [romsDir, savesDir, statesDir] {
            try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        refresh()
    }

    func refresh() {
        let names = (try? fm.contentsOfDirectory(atPath: romsDir.path)) ?? []
        let plays = lastPlayed()
        roms = names
            .filter { ["gb", "gbc"].contains(($0 as NSString).pathExtension.lowercased()) }
            .map(RomEntry.init)
            .sorted { a, b in
                let pa = plays[a.fileName] ?? .distantPast
                let pb = plays[b.fileName] ?? .distantPast
                if pa != pb { return pa > pb }
                return a.fileName.localizedCaseInsensitiveCompare(b.fileName) == .orderedAscending
            }
    }

    // Copy picked files into the library. Multi-select lets a .gb and its
    // .sav come in together (the picker's security scope doesn't extend to
    // directory siblings, so we can't discover the .sav ourselves).
    func importFiles(at urls: [URL]) throws {
        for url in urls {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            let destDir: URL
            switch url.pathExtension.lowercased() {
            case "gb", "gbc": destDir = romsDir
            case "rplg":      destDir = romsDir
            case "sav":       destDir = savesDir
            default:          continue
            }
            let dest = destDir.appendingPathComponent(url.lastPathComponent)
            if fm.fileExists(atPath: dest.path) { try fm.removeItem(at: dest) }
            try fm.copyItem(at: url, to: dest)
        }
        refresh()
    }

    func delete(_ entry: RomEntry) {
        try? fm.removeItem(at: romsDir.appendingPathComponent(entry.fileName))
        refresh()
    }

    func markPlayed(_ entry: RomEntry) {
        var plays = lastPlayed()
        plays[entry.fileName] = Date()
        UserDefaults.standard.set(plays, forKey: playsKey)
        refresh()
    }

    func romData(_ entry: RomEntry) throws -> Data {
        try Data(contentsOf: romsDir.appendingPathComponent(entry.fileName))
    }

    // -- Desktop project sidecars (.rplg) --------------------------------------
    // Prefers the desktop's sibling convention (<rom>.rplg); falls back to any
    // .rplg in roms/ whose romPath names this ROM. Projects stamped with a
    // schema newer than we understand are ignored rather than half-applied.

    func project(for entry: RomEntry) -> RplgProject? {
        if let project = decodeProject(at: romsDir.appendingPathComponent(entry.displayName + ".rplg")) {
            return project
        }
        let names = (try? fm.contentsOfDirectory(atPath: romsDir.path)) ?? []
        for name in names where (name as NSString).pathExtension.lowercased() == "rplg" {
            guard let project = decodeProject(at: romsDir.appendingPathComponent(name)) else { continue }
            let romPath = project.systems?.first?.romPath ?? ""
            if (romPath as NSString).lastPathComponent == entry.fileName { return project }
        }
        return nil
    }

    private func decodeProject(at url: URL) -> RplgProject? {
        guard let data = try? Data(contentsOf: url),
              let project = try? JSONDecoder().decode(RplgProject.self, from: data) else { return nil }
        if let version = project.schemaVersion.flatMap(Int.init), version > 3 { return nil }
        return project
    }

    // -- Battery RAM ---------------------------------------------------------
    // Reads prefer saves/<name>.sav, falling back to a sibling .sav dropped
    // next to the ROM in roms/ (Files-app workflow). Writes always land in
    // saves/ so the imported original is never clobbered.

    func sram(for entry: RomEntry) -> Data? {
        if let data = try? Data(contentsOf: savesDir.appendingPathComponent(entry.displayName + ".sav")) {
            return data
        }
        return try? Data(contentsOf: romsDir.appendingPathComponent(entry.displayName + ".sav"))
    }

    func writeSram(_ data: Data?, for entry: RomEntry) {
        guard let data else { return }
        try? data.write(to: savesDir.appendingPathComponent(entry.displayName + ".sav"),
                        options: .atomic)
    }

    // mGB keeps its synth settings in cartridge RAM — persist like any save.
    func mgbSram() -> Data? {
        try? Data(contentsOf: savesDir.appendingPathComponent("mgb.sav"))
    }

    func writeMgbSram(_ data: Data?) {
        guard let data else { return }
        try? data.write(to: savesDir.appendingPathComponent("mgb.sav"), options: .atomic)
    }

    // -- Savestates -----------------------------------------------------------

    func state(key: String, slot: Int) -> Data? {
        try? Data(contentsOf: stateURL(key: key, slot: slot))
    }

    func writeState(_ data: Data, key: String, slot: Int) throws {
        try data.write(to: stateURL(key: key, slot: slot), options: .atomic)
    }

    private func stateURL(key: String, slot: Int) -> URL {
        statesDir.appendingPathComponent("\(key).ss\(slot)")
    }

    private func lastPlayed() -> [String: Date] {
        (UserDefaults.standard.dictionary(forKey: playsKey) ?? [:])
            .compactMapValues { $0 as? Date }
    }
}

// UserDefaults-backed player settings (SameBoy model, fast boot, gain).
// Mutate through EmulatorController.apply(...) so the running emulator and
// the persisted value stay in sync.
import Foundation
import RetroPlugKit

@MainActor
final class PlayerSettings: ObservableObject {
    @Published var model: RPSameBoyModel {
        didSet { defaults.set(Int(model.rawValue), forKey: Keys.model) }
    }
    @Published var fastBoot: Bool {
        didSet { defaults.set(fastBoot, forKey: Keys.fastBoot) }
    }
    @Published var gainDb: Double {
        didSet { defaults.set(gainDb, forKey: Keys.gain) }
    }
    @Published var syncMode: RPMidiSyncMode {
        didSet { defaults.set(Int(syncMode.rawValue), forKey: Keys.syncMode) }
    }
    @Published var syncTempoDivisor: Int {
        didSet { defaults.set(syncTempoDivisor, forKey: Keys.syncTempoDivisor) }
    }
    @Published var syncAutoStart: Bool {
        didSet { defaults.set(syncAutoStart, forKey: Keys.syncAutoStart) }
    }

    private let defaults = UserDefaults.standard
    private enum Keys {
        static let model = "settings.model"
        static let fastBoot = "settings.fastBoot"
        static let gain = "settings.gainDb"
        static let syncMode = "settings.syncMode"
        static let syncTempoDivisor = "settings.syncTempoDivisor"
        static let syncAutoStart = "settings.syncAutoStart"
    }

    init() {
        let rawModel = defaults.object(forKey: Keys.model) as? Int
        model = rawModel.flatMap { RPSameBoyModel(rawValue: UInt32($0)) } ?? .cgbC
        fastBoot = defaults.object(forKey: Keys.fastBoot) as? Bool ?? true
        gainDb = defaults.object(forKey: Keys.gain) as? Double ?? 0
        let rawSync = defaults.object(forKey: Keys.syncMode) as? Int
        syncMode = rawSync.flatMap { RPMidiSyncMode(rawValue: UInt8($0)) } ?? .mgb
        syncTempoDivisor = defaults.object(forKey: Keys.syncTempoDivisor) as? Int ?? 1
        syncAutoStart = defaults.object(forKey: Keys.syncAutoStart) as? Bool ?? false
    }
}

extension RPMidiSyncMode: @retroactive CaseIterable {
    public static var allCases: [RPMidiSyncMode] {
        [.mgb, .midiSync, .midiSyncArduinoboy, .midiMap,
         .keyboardMidi, .midiOut, .masterSync, .off]
    }

    var displayName: String {
        switch self {
        case .off:                return "Off"
        case .mgb:                return "mGB notes"
        case .midiSync:           return "LSDj MIDI sync"
        case .midiSyncArduinoboy: return "LSDj Arduinoboy sync"
        case .midiMap:            return "LSDj MIDI map"
        case .keyboardMidi:       return "LSDj keyboard MIDI"
        case .midiOut:            return "LSDj MIDI out"
        case .masterSync:         return "LSDj master sync"
        @unknown default:         return "Unknown"
        }
    }
}

extension RPSameBoyModel: @retroactive CaseIterable {
    public static var allCases: [RPSameBoyModel] {
        [.auto, .dmgB, .mgb, .sgb, .sgbPal, .sgb2,
         .cgb0, .cgbA, .cgbB, .cgbC, .cgbD, .cgbE, .agb, .gbp]
    }

    var displayName: String {
        switch self {
        case .auto:   return "Auto"
        case .dmgB:   return "Game Boy (DMG-B)"
        case .mgb:    return "Game Boy Pocket"
        case .sgb:    return "Super Game Boy"
        case .sgbPal: return "Super Game Boy (PAL)"
        case .sgb2:   return "Super Game Boy 2"
        case .cgb0:   return "Game Boy Color (CPU-0)"
        case .cgbA:   return "Game Boy Color (CPU-A)"
        case .cgbB:   return "Game Boy Color (CPU-B)"
        case .cgbC:   return "Game Boy Color (CPU-C)"
        case .cgbD:   return "Game Boy Color (CPU-D)"
        case .cgbE:   return "Game Boy Color (CPU-E)"
        case .agb:    return "Game Boy Advance"
        case .gbp:    return "Game Boy Player"
        @unknown default: return "Unknown"
        }
    }
}

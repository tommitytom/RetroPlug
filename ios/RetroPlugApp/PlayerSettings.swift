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
    // Per-mode MIDI channel assignments, 1-based 1–16, indexed by
    // RPMidiChannelSetting (the Arduinoboy Editor's per-application channel
    // grid). Mutate through EmulatorController.apply(midiChannel:for:).
    @Published var midiChannels: [Int] {
        didSet { defaults.set(midiChannels, forKey: Keys.midiChannels) }
    }
    // mGB base channel: 0 = the five per-voice assignments; 1–12 = the voices
    // sit contiguously at base..base+4 (per-instance channel blocks).
    @Published var mgbBaseChannel: Int {
        didSet { defaults.set(mgbBaseChannel, forKey: Keys.mgbBaseChannel) }
    }
    // MI.OUT CC matrix per voice (PU1/PU2/WAV/NOI): mode (0 = single CC,
    // 1 = hi-digit select), scaling flag, and 7 CC numbers per voice
    // (flat, voice * 7 + index). Mutate through EmulatorController.
    @Published var midiOutCcModes: [Int] {
        didSet { defaults.set(midiOutCcModes, forKey: Keys.midiOutCcModes) }
    }
    @Published var midiOutCcScaling: [Bool] {
        didSet { defaults.set(midiOutCcScaling, forKey: Keys.midiOutCcScaling) }
    }
    @Published var midiOutCcNumbers: [Int] {
        didSet { defaults.set(midiOutCcNumbers, forKey: Keys.midiOutCcNumbers) }
    }

    // The Arduinoboy firmware factory defaults — matches kChannelDefaults in
    // the audio unit.
    static let defaultMidiChannels: [Int] = [
        1, 1, 1, 1,     // slave sync, master sync, keyboard, MIDI map
        1, 2, 3, 4, 5,  // mGB PU1/PU2/WAV/NOI/POLY
        1, 2, 3, 4,     // MIDI out note channels per voice
        1, 2, 3, 4,     // MIDI out CC channels per voice
    ]

    func midiChannel(_ setting: RPMidiChannelSetting) -> Int {
        midiChannels[Int(setting.rawValue)]
    }

    // The firmware's CC matrix factory defaults — matches the audio unit's
    // seeding (multi mode, scaling on, CC#s 1/2/3/7/10/11/12 per voice).
    static let defaultMidiOutCcModes = [1, 1, 1, 1]
    static let defaultMidiOutCcScaling = [true, true, true, true]
    static let defaultMidiOutCcNumbers: [Int] = (0..<4).flatMap { _ in [1, 2, 3, 7, 10, 11, 12] }

    private let defaults = UserDefaults.standard
    private enum Keys {
        static let model = "settings.model"
        static let fastBoot = "settings.fastBoot"
        static let gain = "settings.gainDb"
        static let syncMode = "settings.syncMode"
        static let syncTempoDivisor = "settings.syncTempoDivisor"
        static let syncAutoStart = "settings.syncAutoStart"
        static let midiChannels = "settings.midiChannels"
        static let midiOutCcModes = "settings.midiOutCcModes"
        static let midiOutCcScaling = "settings.midiOutCcScaling"
        static let midiOutCcNumbers = "settings.midiOutCcNumbers"
        static let mgbBaseChannel = "settings.mgbBaseChannel"
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
        let stored = defaults.array(forKey: Keys.midiChannels) as? [Int] ?? []
        midiChannels = Self.defaultMidiChannels.indices.map { i in
            min(max(i < stored.count ? stored[i] : Self.defaultMidiChannels[i], 1), 16)
        }
        let storedModes = defaults.array(forKey: Keys.midiOutCcModes) as? [Int] ?? []
        midiOutCcModes = Self.defaultMidiOutCcModes.indices.map { i in
            min(max(i < storedModes.count ? storedModes[i] : Self.defaultMidiOutCcModes[i], 0), 1)
        }
        let storedScaling = defaults.array(forKey: Keys.midiOutCcScaling) as? [Bool] ?? []
        midiOutCcScaling = Self.defaultMidiOutCcScaling.indices.map { i in
            i < storedScaling.count ? storedScaling[i] : Self.defaultMidiOutCcScaling[i]
        }
        let storedNumbers = defaults.array(forKey: Keys.midiOutCcNumbers) as? [Int] ?? []
        midiOutCcNumbers = Self.defaultMidiOutCcNumbers.indices.map { i in
            min(max(i < storedNumbers.count ? storedNumbers[i] : Self.defaultMidiOutCcNumbers[i], 0), 127)
        }
        mgbBaseChannel = min(max(defaults.object(forKey: Keys.mgbBaseChannel) as? Int ?? 0, 0), 12)
    }
}

extension RPMidiSyncMode: @retroactive CaseIterable {
    public static var allCases: [RPMidiSyncMode] {
        [.mgb, .midiSync, .midiSyncArduinoboy, .midiMap,
         .keyboardMidi, .midiOut, .masterSync, .noteOut, .off]
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
        case .noteOut:            return "GB note out"
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

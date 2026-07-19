// The app's control plane: owns the AVAudioEngine hosting the audio unit
// in-process, and mediates everything SwiftUI does to the emulator through
// the RetroPlugKit CoreBridge (loads, saves, buttons, settings, MIDI).
import AVFAudio
import RetroPlugKit
import SwiftUI

// The in-app (in-process) registration. Deliberately a DIFFERENT subtype from
// the AUv3 extension ('mgbs'): with the same description, instantiate resolves
// to the extension and returns an out-of-process proxy — the cast to
// RetroPlugAudioUnit fails and the CoreBridge (screen/buttons/ROM) is
// unreachable. 'mgbl' = local.
let kLocalComponentDescription: AudioComponentDescription = {
    var desc = AudioComponentDescription()
    desc.componentType = kAudioUnitType_MusicDevice          // 'aumu'
    desc.componentSubType = 0x6D67626C                        // 'mgbl'
    desc.componentManufacturer = 0x5250746D                   // 'RPtm'
    return desc
}()

enum LoadedContent: Equatable {
    case none
    case mgb
    case rom(RomEntry)
}

@MainActor
final class EmulatorController: ObservableObject {
    @Published private(set) var status = "starting audio…"
    @Published private(set) var auUnit: RetroPlugAudioUnit?
    @Published private(set) var loaded: LoadedContent = .none
    @Published private(set) var controllerConnected = false
    @Published var lastError: String?

    let library = RomLibrary()
    let settings = PlayerSettings()

    private let engine = AVAudioEngine()
    private var unit: AVAudioUnit?
    private var controllerInput: ControllerInput?

    func start() async {
        guard auUnit == nil else { return }
        // Physical game controllers share the touch controls' press() path.
        controllerInput = ControllerInput(
            press: { [weak self] button, down in self?.press(button, down: down) },
            connectionChanged: { [weak self] connected in self?.controllerConnected = connected })
        do {
            // setActive(true) blocks, so configure the session off the main thread
            // (iOS has no async activate API — that's watchOS-only).
            try await Task.detached(priority: .userInitiated) {
                let session = AVAudioSession.sharedInstance()
                try session.setCategory(.playback, mode: .default)
                try session.setActive(true)
            }.value

            AUAudioUnit.registerSubclass(RetroPlugAudioUnit.self,
                                         as: kLocalComponentDescription,
                                         name: "local: RetroPlug mGB",
                                         version: 1)
            let unit = try await AVAudioUnit.instantiate(with: kLocalComponentDescription, options: [])
            engine.attach(unit)
            engine.connect(unit, to: engine.mainMixerNode, format: nil)
            try engine.start()
            self.unit = unit
            guard let au = unit.auAudioUnit as? RetroPlugAudioUnit else {
                status = "internal error: audio unit loaded out of process"
                return
            }
            auUnit = au
            // Push persisted settings into the freshly built unit.
            au.setGainDb(Float(settings.gainDb))
            au.setFastBoot(settings.fastBoot)
            au.setMidiSyncMode(settings.syncMode)
            au.setSyncTempoDivisor(UInt(settings.syncTempoDivisor))
            au.setSyncAutoStart(settings.syncAutoStart)
            try? au.setModel(settings.model)
            status = "ready"
        } catch {
            status = "audio setup failed: \(error.localizedDescription)"
        }
    }

    // -- Content --------------------------------------------------------------

    func loadMgb() {
        guard let au = auUnit else { return }
        saveSramNow()
        do {
            try au.loadEmbeddedMGB(withSram: library.mgbSram())
            loaded = .mgb
            lastError = nil
            // Loading the MIDI synth implies the note passthrough — leaving a
            // previous project's LSDj sync mode active would mute the pads.
            if settings.syncMode != .mgb { apply(syncMode: .mgb) }
        } catch {
            lastError = error.localizedDescription
        }
    }

    func load(_ entry: RomEntry) {
        guard let au = auUnit else { return }
        saveSramNow()
        // A .rplg sidecar (thin desktop project) carries per-system settings;
        // apply them before the load so the ROM boots on the right model.
        let project = library.project(for: entry)
        if let config = project?.sameboyConfig {
            if let model = config.sameboyModel, model != settings.model {
                apply(model: model)
            }
            if let fastBoot = config.fastBoot, fastBoot != settings.fastBoot {
                apply(fastBoot: fastBoot)
            }
        }
        // The lsdj-sync role carries the MIDI translation the project expects
        // (e.g. LSDj slaved to the host clock via midiSync).
        if let config = project?.lsdjSyncConfig {
            if let mode = config.midiSyncMode { apply(syncMode: mode) }
            if let divisor = config.tempoDivisor { apply(syncTempoDivisor: divisor) }
            if let autoStart = config.autoStart { apply(syncAutoStart: autoStart) }
        }
        do {
            let rom = try library.romData(entry)
            try au.loadRomData(rom, sram: library.sram(for: entry), state: nil)
            loaded = .rom(entry)
            lastError = nil
            library.markPlayed(entry)
        } catch {
            lastError = error.localizedDescription
        }
    }

    func eject() {
        saveSramNow()
        loaded = .none
    }

    // -- Saves ----------------------------------------------------------------

    // Exact battery save of whatever is loaded (bypass-gate path; also the
    // scenePhase-background hook).
    func saveSramNow() {
        guard let au = auUnit else { return }
        switch loaded {
        case .none:           break
        case .mgb:            library.writeMgbSram(au.saveSram())
        case .rom(let entry): library.writeSram(au.saveSram(), for: entry)
        }
    }

    private var stateKey: String? {
        switch loaded {
        case .none:           return nil
        case .mgb:            return "mgb"
        case .rom(let entry): return entry.displayName
        }
    }

    func saveState() {
        guard let au = auUnit, let key = stateKey, let data = au.saveState() else { return }
        do {
            try library.writeState(data, key: key, slot: 0)
            lastError = nil
        } catch {
            lastError = error.localizedDescription
        }
    }

    func loadState() {
        guard let au = auUnit, let key = stateKey else { return }
        guard let data = library.state(key: key, slot: 0) else {
            lastError = "No saved state yet."
            return
        }
        do {
            try au.loadState(data)
            lastError = nil
        } catch {
            lastError = error.localizedDescription
        }
    }

    // -- Input ----------------------------------------------------------------

    func press(_ button: RPGameboyButton, down: Bool) {
        auUnit?.press(button, down: down)
    }

    func reset() {
        auUnit?.resetEmulator()
    }

    // -- Settings -------------------------------------------------------------

    func apply(model: RPSameBoyModel) {
        settings.model = model
        guard let au = auUnit else { return }
        do {
            try au.setModel(model) // reboots the game; SRAM survives
            lastError = nil
        } catch {
            lastError = error.localizedDescription
        }
    }

    func apply(fastBoot: Bool) {
        settings.fastBoot = fastBoot
        auUnit?.setFastBoot(fastBoot)
    }

    func apply(gainDb: Double) {
        settings.gainDb = gainDb
        auUnit?.setGainDb(Float(gainDb))
    }

    func apply(syncMode: RPMidiSyncMode) {
        settings.syncMode = syncMode
        auUnit?.setMidiSyncMode(syncMode)
    }

    func apply(syncTempoDivisor: Int) {
        let clamped = min(max(syncTempoDivisor, 1), 8)
        settings.syncTempoDivisor = clamped
        auUnit?.setSyncTempoDivisor(UInt(clamped))
    }

    func apply(syncAutoStart: Bool) {
        settings.syncAutoStart = syncAutoStart
        auUnit?.setSyncAutoStart(syncAutoStart)
    }

    // -- MIDI (mGB pads) ------------------------------------------------------

    func send(_ bytes: [UInt8]) {
        guard let block = unit?.auAudioUnit.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { buf in
            block(AUEventSampleTimeImmediate, 0, buf.count, buf.baseAddress!)
        }
    }

    func noteOn(channel: UInt8, note: UInt8)  { send([0x90 | channel, note, 100]) }
    func noteOff(channel: UInt8, note: UInt8) { send([0x80 | channel, note, 0]) }
}

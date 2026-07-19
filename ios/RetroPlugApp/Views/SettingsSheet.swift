// Emulator settings. All changes route through EmulatorController.apply(...)
// so they hit the running emulator and persist to UserDefaults together.
import RetroPlugKit
import SwiftUI

struct SettingsSheet: View {
    @EnvironmentObject var emu: EmulatorController
    @ObservedObject var settings: PlayerSettings
    @Environment(\.dismiss) private var dismiss

    // Set LSDj's PROJECT → SYNC to the matching mode; a .rplg sidecar
    // overrides all of this per project.
    private var syncFooter: String {
        switch settings.syncMode {
        case .mgb:
            return "Forwards MIDI to the cartridge — each mGB voice listens on its assigned channel."
        case .midiSync:
            return "LSDj (SYNC: MIDI) follows the host clock — a DAW transport via the AUv3, or incoming MIDI clock."
        case .midiSyncArduinoboy:
            return "Arduinoboy slave (SYNC: LSDJ) on the assigned channel: note 24 starts the clock, 25 stops it, 26–29 pick the divisor, 30+ jump to a song row."
        case .midiMap:
            return "Note-on jumps LSDj (SYNC: MIDI MAP) to a song row — the assigned channel plays rows 0–127, the next channel up rows 128–255."
        case .keyboardMidi:
            return "MIDI notes on the assigned channel act as LSDj's PS/2 keyboard (SYNC: KEYBD) — C-3 and up play notes, C-2 to B-2 are mute/cursor/table keys."
        case .midiOut:
            return "LSDj (SYNC: MI. OUT) plays the DAW: its MI.OUT commands come back out of the plugin as MIDI on the per-voice channels."
        case .masterSync:
            return "LSDj (SYNC: LSDJ) is the clock master — the plugin emits MIDI clock the host can follow, plus the song row as a note-on."
        case .noteOut:
            return "Notes are read straight from the Game Boy's sound hardware — works with any LSDj version (or any ROM), no MI.OUT build needed. Envelope → CC7, pan → CC10, duty → CC70, slides → pitch bend."
        default:
            return "Host MIDI is ignored."
        }
    }

    // One 1–16 channel picker bound to a channel-assignment slot (the
    // Arduinoboy Editor's per-application channel grid).
    private func channelPicker(_ label: String, _ setting: RPMidiChannelSetting) -> some View {
        Picker(label, selection: Binding(
            get: { settings.midiChannel(setting) },
            set: { emu.apply(midiChannel: $0, for: setting) }
        )) {
            ForEach(1...16, id: \.self) { channel in
                Text("\(channel)").tag(channel)
            }
        }
    }

    // The active mode's channel assignments, mirroring the Arduinoboy
    // Editor's section for that mode. midiSync is clock-only — no channels.
    @ViewBuilder
    private var channelSettings: some View {
        switch settings.syncMode {
        case .mgb:
            // Base channel wins over the per-voice pickers: the five voices
            // sit at base..base+4 so each plugin instance can take its own
            // channel block with one setting.
            Picker("Base channel", selection: Binding(
                get: { settings.mgbBaseChannel },
                set: { emu.apply(mgbBaseChannel: $0) }
            )) {
                Text("Custom").tag(0)
                ForEach(1...12, id: \.self) { base in
                    Text("\(base)–\(base + 4)").tag(base)
                }
            }
            if settings.mgbBaseChannel == 0 {
                channelPicker("PU1 channel", .mgbPu1)
                channelPicker("PU2 channel", .mgbPu2)
                channelPicker("WAV channel", .mgbWav)
                channelPicker("NOI channel", .mgbNoi)
                channelPicker("POLY channel", .mgbPoly)
            }
        case .midiSyncArduinoboy:
            channelPicker("MIDI channel", .arduinoboySlave)
        case .midiMap:
            channelPicker("MIDI channel", .midiMap)
        case .keyboardMidi:
            channelPicker("MIDI channel", .keyboard)
        case .masterSync:
            channelPicker("Row note channel", .masterSync)
        case .midiOut, .noteOut:
            channelPicker("PU1 note channel", .midiOutNotePu1)
            channelPicker("PU2 note channel", .midiOutNotePu2)
            channelPicker("WAV note channel", .midiOutNoteWav)
            channelPicker("NOI note channel", .midiOutNoteNoi)
            channelPicker("PU1 CC channel", .midiOutCcPu1)
            channelPicker("PU2 CC channel", .midiOutCcPu2)
            channelPicker("WAV CC channel", .midiOutCcWav)
            channelPicker("NOI CC channel", .midiOutCcNoi)
            // The CC matrix decodes LSDj's X command — MI.OUT only (Note Out
            // derives its CCs from the sound hardware instead).
            if settings.syncMode == .midiOut {
                ForEach(0..<4, id: \.self) { voice in
                    NavigationLink("\(Self.voiceNames[voice]) CC matrix") {
                        CcMatrixView(settings: settings, voice: voice)
                    }
                }
            }
        default:
            EmptyView()
        }
    }

    static let voiceNames = ["PU1", "PU2", "WAV", "NOI"]

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Model", selection: Binding(
                        get: { settings.model },
                        set: { emu.apply(model: $0) }
                    )) {
                        ForEach(RPSameBoyModel.allCases, id: \.rawValue) { model in
                            Text(model.displayName).tag(model)
                        }
                    }
                    Toggle("Fast boot", isOn: Binding(
                        get: { settings.fastBoot },
                        set: { emu.apply(fastBoot: $0) }
                    ))
                } footer: {
                    Text("Changing the model reboots the game. Battery saves survive; unsaved state does not.")
                }

                Section {
                    Picker("MIDI mode", selection: Binding(
                        get: { settings.syncMode },
                        set: { emu.apply(syncMode: $0) }
                    )) {
                        ForEach(RPMidiSyncMode.allCases, id: \.rawValue) { mode in
                            Text(mode.displayName).tag(mode)
                        }
                    }
                    if [.midiSync, .midiSyncArduinoboy].contains(settings.syncMode) {
                        Picker("Tempo divisor", selection: Binding(
                            get: { settings.syncTempoDivisor },
                            set: { emu.apply(syncTempoDivisor: $0) }
                        )) {
                            ForEach([1, 2, 4, 8], id: \.self) { divisor in
                                Text("1/\(divisor)").tag(divisor)
                            }
                        }
                        Toggle("Start LSDj with transport", isOn: Binding(
                            get: { settings.syncAutoStart },
                            set: { emu.apply(syncAutoStart: $0) }
                        ))
                    }
                    channelSettings
                } header: {
                    Text("MIDI")
                } footer: {
                    Text(syncFooter)
                }

                Section("Volume") {
                    HStack {
                        Slider(value: Binding(
                            get: { settings.gainDb },
                            set: { emu.apply(gainDb: $0) }
                        ), in: -24...6, step: 1)
                        Text("\(Int(settings.gainDb)) dB")
                            .font(.caption.monospaced())
                            .frame(width: 52, alignment: .trailing)
                    }
                }

                Section {
                    NavigationLink("Acknowledgements & Licenses") { AboutView() }
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}

// One MI.OUT voice's CC matrix (the editor's CC Mode / CC SCALING / CC#0–6
// row): how LSDj's X command on that voice becomes CC messages.
private struct CcMatrixView: View {
    @EnvironmentObject var emu: EmulatorController
    @ObservedObject var settings: PlayerSettings
    let voice: Int

    private var modeFooter: String {
        if settings.midiOutCcModes[voice] == 1 {
            return settings.midiOutCcScaling[voice]
                ? "X commands: the first digit picks CC#0–6 below, the second digit is the value, scaled to 0–120."
                : "X commands: the first digit picks CC#0–6 below; the raw command value is sent unchanged."
        }
        return settings.midiOutCcScaling[voice]
            ? "The whole X command value goes to CC#0, scaled from 0–111 up to 0–127."
            : "The whole X command value (0–111) goes to CC#0 unchanged."
    }

    var body: some View {
        Form {
            Section {
                Picker("CC mode", selection: Binding(
                    get: { settings.midiOutCcModes[voice] },
                    set: { emu.apply(midiOutCcMode: $0, voice: voice) }
                )) {
                    Text("Single CC").tag(0)
                    Text("7-CC select").tag(1)
                }
                Toggle("Scale values", isOn: Binding(
                    get: { settings.midiOutCcScaling[voice] },
                    set: { emu.apply(midiOutCcScaling: $0, voice: voice) }
                ))
            } footer: {
                Text(modeFooter)
            }
            Section("CC numbers") {
                ForEach(0..<7, id: \.self) { index in
                    Picker("CC#\(index)", selection: Binding(
                        get: { settings.midiOutCcNumbers[voice * 7 + index] },
                        set: { emu.apply(midiOutCcNumber: $0, index: index, voice: voice) }
                    )) {
                        ForEach(0...127, id: \.self) { cc in
                            Text("\(cc)").tag(cc)
                        }
                    }
                }
            }
        }
        .navigationTitle("\(SettingsSheet.voiceNames[voice]) CC matrix")
        .navigationBarTitleDisplayMode(.inline)
    }
}

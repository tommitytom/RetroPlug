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
            return "Forwards MIDI to the cartridge — mGB plays notes on channels 1–4 (pu1/pu2/wav/noi)."
        case .midiSync:
            return "LSDj (SYNC: MIDI) follows the host clock — a DAW transport via the AUv3, or incoming MIDI clock."
        case .midiSyncArduinoboy:
            return "Arduinoboy slave (SYNC: LSDJ): note 24 starts the clock, 25 stops it, 26–29 pick the divisor, 30+ jump to a song row."
        case .midiMap:
            return "Note-on jumps LSDj (SYNC: MIDI MAP) to a song row — channel 1 rows 0–127, channel 2 rows 128–255."
        case .keyboardMidi:
            return "MIDI notes act as LSDj's PS/2 keyboard (SYNC: KEYBD) — C-3 and up play notes, C-2 to B-2 are mute/cursor/table keys."
        case .midiOut:
            return "LSDj (SYNC: MI. OUT) plays the DAW: its MI.OUT commands come back out of the plugin as MIDI."
        case .masterSync:
            return "LSDj (SYNC: LSDJ) is the clock master — the plugin emits MIDI clock the host can follow."
        default:
            return "Host MIDI is ignored."
        }
    }

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

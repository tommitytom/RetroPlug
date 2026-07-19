// The playing screen: LCD on top, touch controls (or the mGB MIDI pads)
// below, toolbar for reset/saves/eject and the settings sheet.
import RetroPlugKit
import SwiftUI

struct PlayerView: View {
    @EnvironmentObject var emu: EmulatorController
    @State private var showSettings = false

    private var title: String {
        switch emu.loaded {
        case .none:           return "RetroPlug"
        case .mgb:            return "mGB"
        case .rom(let entry): return entry.displayName
        }
    }

    var body: some View {
        VStack(spacing: 16) {
            GameBoyScreenView { buffer, capacity in
                emu.auUnit?.copyFrame(into: buffer, capacityPixels: UInt(capacity)) ?? false
            }
            .aspectRatio(CGFloat(GameBoyScreenView.pixelWidth) / CGFloat(GameBoyScreenView.pixelHeight),
                         contentMode: .fit)
            .frame(maxWidth: .infinity)

            if case .mgb = emu.loaded {
                MgbPadsView()
            } else if emu.controllerConnected {
                Label("Game controller connected", systemImage: "gamecontroller")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .padding(.top, 8)
            } else {
                TouchControlsView { button, down in
                    emu.press(button, down: down)
                }
                .padding(.horizontal, 8)
            }

            if let error = emu.lastError {
                Text(error).font(.caption).foregroundStyle(.red)
            }
            Spacer(minLength: 0)
        }
        .padding()
        .navigationTitle(title)
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .topBarLeading) {
                Button("Eject") { emu.eject() }
            }
            ToolbarItem(placement: .topBarTrailing) {
                Menu {
                    Button("Reset", systemImage: "arrow.counterclockwise") { emu.reset() }
                    Divider()
                    Button("Save State", systemImage: "square.and.arrow.down") { emu.saveState() }
                    Button("Load State", systemImage: "square.and.arrow.up") { emu.loadState() }
                    Button("Save Battery (SRAM)", systemImage: "battery.100") { emu.saveSramNow() }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
            }
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    showSettings = true
                } label: {
                    Image(systemName: "gearshape")
                }
            }
        }
        .sheet(isPresented: $showSettings) {
            SettingsSheet(settings: emu.settings)
                .environmentObject(emu)
        }
    }
}

// The mGB performance surface: one row of note pads per Game Boy channel
// (MIDI ch 1-4 = pu1 / pu2 / wav / noi).
private struct MgbPadsView: View {
    var body: some View {
        VStack(spacing: 12) {
            PadGrid(channel: 0, name: "Pulse 1 (ch 1)")
            PadGrid(channel: 1, name: "Pulse 2 (ch 2)")
            PadGrid(channel: 2, name: "Wave (ch 3)")
            PadGrid(channel: 3, name: "Noise (ch 4)")
        }
    }
}

struct PadGrid: View {
    @EnvironmentObject var emu: EmulatorController
    let channel: UInt8
    let name: String
    private let notes: [UInt8] = [48, 52, 55, 60, 64, 67, 72, 76]

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(name).font(.caption).foregroundStyle(.secondary)
            HStack(spacing: 6) {
                ForEach(notes, id: \.self) { note in
                    Text("\(note)")
                        .font(.caption2.monospaced())
                        .frame(maxWidth: .infinity, minHeight: 44)
                        .background(.quaternary, in: RoundedRectangle(cornerRadius: 8))
                        .onLongPressGesture(minimumDuration: .infinity, pressing: { down in
                            if down { emu.noteOn(channel: channel, note: note) }
                            else    { emu.noteOff(channel: channel, note: note) }
                        }, perform: {})
                }
            }
        }
    }
}

// Container app for the AUv3 extension (iOS requires the extension ship
// inside an app) — and a standalone native player: boots to a start menu,
// loads .gb/.gbc ROMs or the embedded mGB synth, renders the LCD, and hosts
// the audio unit in-process via AVAudioEngine (see EmulatorController).
import SwiftUI

struct RootView: View {
    @EnvironmentObject var emu: EmulatorController

    var body: some View {
        NavigationStack {
            if case .none = emu.loaded {
                StartMenuView(library: emu.library)
            } else {
                PlayerView()
            }
        }
        .task { await emu.start() }
    }
}

@main
struct RetroPlugApp: App {
    @StateObject private var emu = EmulatorController()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            RootView().environmentObject(emu)
        }
        .onChange(of: scenePhase) { _, phase in
            // Battery RAM must survive the app being killed in the background.
            if phase == .background { emu.saveSramNow() }
        }
    }
}

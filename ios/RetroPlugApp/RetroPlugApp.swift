// Container app for the AUv3 extension (iOS requires the extension ship
// inside an app) — and a standalone native player: boots to a start menu,
// loads .gb/.gbc ROMs or the embedded mGB synth, renders the LCD, and hosts
// the audio unit in-process via AVAudioEngine (see EmulatorController).
import SwiftUI
import UIKit

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
        // Battery RAM must survive the app being killed in the background.
        // Notification instead of scenePhase onChange: the two-parameter
        // onChange is iOS 17+, the single-parameter one warns there, and the
        // deployment target is 16 — this form is clean on both.
        .onReceive(NotificationCenter.default.publisher(
            for: UIApplication.didEnterBackgroundNotification)) { _ in
            emu.saveSramNow()
        }
    }
}

@main
struct RetroPlugApp: App {
    @StateObject private var emu = EmulatorController()

    var body: some Scene {
        WindowGroup {
            RootView().environmentObject(emu)
        }
    }
}

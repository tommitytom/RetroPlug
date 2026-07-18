// Container app for the AUv3 extension (iOS requires the extension ship
// inside an app) — doubles as the standalone proof: hosts the audio unit
// in-process via AVAudioEngine and offers a small pad grid that sends MIDI
// notes to mGB (ch 1-4 = pu1 / pu2 / wav / noi).
import AVFAudio
import RetroPlugKit
import SwiftUI

let kComponentDescription: AudioComponentDescription = {
    var desc = AudioComponentDescription()
    desc.componentType = kAudioUnitType_MusicDevice          // 'aumu'
    desc.componentSubType = 0x6D676273                        // 'mgbs'
    desc.componentManufacturer = 0x5250746D                   // 'RPtm'
    return desc
}()

@MainActor
final class AudioManager: ObservableObject {
    @Published var status = "starting…"
    private let engine = AVAudioEngine()
    private var unit: AVAudioUnit?

    func start() async {
        do {
            try AVAudioSession.sharedInstance().setCategory(.playback, mode: .default)
            try AVAudioSession.sharedInstance().setActive(true)

            AUAudioUnit.registerSubclass(RetroPlugAudioUnit.self,
                                         as: kComponentDescription,
                                         name: "tommitytom: RetroPlug mGB",
                                         version: 1)
            let unit = try await AVAudioUnit.instantiate(with: kComponentDescription, options: [])
            engine.attach(unit)
            engine.connect(unit, to: engine.mainMixerNode, format: nil)
            try engine.start()
            self.unit = unit
            status = "running — tap a pad (mGB boots in ~2s)"
        } catch {
            status = "audio setup failed: \(error.localizedDescription)"
        }
    }

    func send(_ bytes: [UInt8]) {
        guard let block = unit?.auAudioUnit.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { buf in
            block(AUEventSampleTimeImmediate, 0, buf.count, buf.baseAddress!)
        }
    }

    func noteOn(channel: UInt8, note: UInt8)  { send([0x90 | channel, note, 100]) }
    func noteOff(channel: UInt8, note: UInt8) { send([0x80 | channel, note, 0]) }
}

struct PadGrid: View {
    @EnvironmentObject var audio: AudioManager
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
                            if down { audio.noteOn(channel: channel, note: note) }
                            else    { audio.noteOff(channel: channel, note: note) }
                        }, perform: {})
                }
            }
        }
    }
}

struct ContentView: View {
    @StateObject private var audio = AudioManager()

    var body: some View {
        VStack(spacing: 16) {
            Text("RetroPlug mGB — iOS spike").font(.headline)
            Text(audio.status).font(.caption).foregroundStyle(.secondary)
            PadGrid(channel: 0, name: "Pulse 1 (ch 1)")
            PadGrid(channel: 1, name: "Pulse 2 (ch 2)")
            PadGrid(channel: 2, name: "Wave (ch 3)")
            PadGrid(channel: 3, name: "Noise (ch 4)")
            Text("The AUv3 (RetroPlug mGB) is available in AUM / GarageBand / Cubasis after this app is installed.")
                .font(.footnote).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .padding()
        .environmentObject(audio)
        .task { await audio.start() }
    }
}

@main
struct RetroPlugApp: App {
    var body: some Scene {
        WindowGroup { ContentView() }
    }
}

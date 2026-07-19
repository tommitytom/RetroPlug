// Boot screen: load a ROM from Files, start the built-in mGB synth, or pick
// from the library (most recently played first).
import SwiftUI
import UniformTypeIdentifiers

struct StartMenuView: View {
    @EnvironmentObject var emu: EmulatorController
    @ObservedObject var library: RomLibrary
    @State private var importing = false

    private var romTypes: [UTType] {
        [UTType(importedAs: "com.toilville.retroplug.gb"),
         UTType(importedAs: "com.toilville.retroplug.gbc"),
         UTType(importedAs: "com.toilville.retroplug.sav"),
         UTType(importedAs: "com.toilville.retroplug.rplg")]
    }

    var body: some View {
        List {
            Section {
                Button {
                    importing = true
                } label: {
                    Label("Load ROM…", systemImage: "folder")
                }
                Button {
                    emu.loadMgb()
                } label: {
                    Label("Load mGB (MIDI synth)", systemImage: "pianokeys")
                }
            }

            Section("Library") {
                if library.roms.isEmpty {
                    Text("Import .gb / .gbc files (select the .sav and .rplg alongside to bring saves and project settings), or drop them into RetroPlug/roms in the Files app.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                ForEach(library.roms) { rom in
                    Button {
                        emu.load(rom)
                    } label: {
                        Label(rom.displayName, systemImage: "gamecontroller")
                    }
                }
                .onDelete { offsets in
                    offsets.map { library.roms[$0] }.forEach(library.delete)
                }
            }

            Section {
            } footer: {
                VStack(alignment: .leading, spacing: 4) {
                    Text(emu.status)
                    if let error = emu.lastError {
                        Text(error).foregroundStyle(.red)
                    }
                }
            }
        }
        .navigationTitle("RetroPlug")
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: romTypes,
                      allowsMultipleSelection: true) { result in
            switch result {
            case .success(let urls):
                do {
                    try library.importFiles(at: urls)
                    emu.lastError = nil
                } catch {
                    emu.lastError = error.localizedDescription
                }
            case .failure(let error):
                emu.lastError = error.localizedDescription
            }
        }
        .onAppear { library.refresh() }
    }
}

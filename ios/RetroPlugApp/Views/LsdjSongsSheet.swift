// LSDj song manager: the stored projects in the running cart's battery RAM,
// with tap-to-load (decompress into working memory + reboot) and a manual
// .sav import. Shows a hint instead of a list for non-LSDj carts.
import SwiftUI
import UniformTypeIdentifiers

struct LsdjSongsSheet: View {
    @EnvironmentObject var emu: EmulatorController
    @Environment(\.dismiss) private var dismiss

    @State private var songs: [LsdjSav.Song] = []
    @State private var activeSlot: Int?
    @State private var pendingLoad: LsdjSav.Song?
    @State private var showSavImporter = false

    private static let savTypes: [UTType] =
        [UTType(filenameExtension: "sav"), .data].compactMap { $0 }

    var body: some View {
        NavigationStack {
            Form {
                if songs.isEmpty {
                    Section {
                        Text("No LSDj songs found. Load an LSDj cartridge, or import a .sav below.")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    Section("Songs") {
                        ForEach(songs) { song in
                            Button {
                                // Only ask when the working song would lose
                                // something: dirty vs. its slot, or unknowable.
                                if emu.lsdjWorkingSongIsClean() == true {
                                    emu.loadLsdjSong(slot: song.slot)
                                    refresh()
                                } else {
                                    pendingLoad = song
                                }
                            } label: {
                                HStack {
                                    Text(song.name.isEmpty ? "(untitled)" : song.name)
                                        .font(.body.monospaced())
                                    Text(String(format: "v%02X", song.version))
                                        .font(.caption.monospaced())
                                        .foregroundStyle(.secondary)
                                    Spacer()
                                    if song.slot == activeSlot {
                                        Image(systemName: "play.circle")
                                            .foregroundStyle(.secondary)
                                    }
                                }
                            }
                            .tint(.primary)
                        }
                    }
                }

                Section {
                    Button("Load .sav File…", systemImage: "square.and.arrow.down.on.square") {
                        showSavImporter = true
                    }
                } footer: {
                    Text("Replaces the cartridge's battery RAM and reboots. The current battery save is written out first.")
                }
            }
            .navigationTitle("LSDj Songs")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .onAppear(perform: refresh)
        .confirmationDialog(
            "Load “\(pendingLoad.map { $0.name.isEmpty ? "(untitled)" : $0.name } ?? "")”?",
            isPresented: Binding(get: { pendingLoad != nil },
                                 set: { if !$0 { pendingLoad = nil } }),
            titleVisibility: .visible
        ) {
            Button("Load Song", role: .destructive) {
                if let song = pendingLoad { emu.loadLsdjSong(slot: song.slot) }
                pendingLoad = nil
                refresh()
            }
        } message: {
            Text("The working song has changes that aren't saved to a slot — loading will discard them. Save it in LSDj first to keep them.")
        }
        .fileImporter(isPresented: $showSavImporter,
                      allowedContentTypes: Self.savTypes) { result in
            guard case .success(let url) = result else { return }
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            do {
                emu.loadSav(try Data(contentsOf: url))
            } catch {
                emu.lastError = error.localizedDescription
            }
            refresh()
        }
    }

    private func refresh() {
        songs = emu.lsdjSongs()
        activeSlot = emu.lsdjActiveSlot()
    }
}

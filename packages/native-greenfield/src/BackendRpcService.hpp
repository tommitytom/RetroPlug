#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "project/Project.hpp"

#include "DspRuntime.hpp"

// The native side of the greenfield `Backend` (packages/retroplug-greenfield/src/backend.ts),
// exposed to the TS app + tests over rpcpp (reflect-cpp -> QuickJS object codec, same shape
// as cli/HarnessRpcService). Two halves: the fs / config / codec primitives (std::filesystem
// + miniz + config-dir resolution) and the emulator seam — the latter backed by a real
// `Project` of real `SameBoySystem`s (SameBoy-only for now). Method bodies in
// BackendRpcService.cpp.
//
// Byte OUTPUT rides rfl::Bytestring (msgpack BIN -> JS Uint8Array); a nullable read is
// std::optional (absent -> JS null). Byte INPUT rides std::vector<std::uint8_t>
// (reflect-cpp's binary reader is int-array-only). Zip entry DTOs mirror the harness.
struct BackendZipEntry { std::string name; rfl::Bytestring bytes; };            // unzip output
struct BackendZipInput { std::string name; std::vector<std::uint8_t> bytes; };  // zip input


// Mirrors the greenfield ConstructSpec (packages/retroplug-greenfield/src/backend.ts):
// concrete paths + an embedded-ROM marker + optional zip-import seed bytes. The optional
// string fields are absent (nullopt) rather than "" when the TS side has null.
struct BackendConstructSpec {
    std::string                              romPath;
    std::string                              embeddedRom;
    std::optional<std::string>               savPath;
    std::optional<std::string>               statePath;
    std::optional<std::uint32_t>             replaceId;
    std::optional<std::vector<std::uint8_t>> sramBytes;
    std::optional<std::vector<std::uint8_t>> stateBytes;
    // Optional LSDJ sync-role mode ("Off" / "MidiSync" / …). When set, seeds the
    // role at construct time (skipping the sniffer default) — mirrors the CLI
    // harness's loadRom. "Off" makes the role passive (no host clock).
    std::optional<std::string>               lsdjSyncMode;
};

class BackendRpcService {
public:
    BackendRpcService() = default;

    // --- filesystem ---
    std::optional<rfl::Bytestring> readFile(std::string path);
    bool writeFile(std::string path, std::vector<std::uint8_t> bytes);
    bool writeFileAtomic(std::string path, std::vector<std::uint8_t> bytes);
    bool fileExists(std::string path);
    bool rename(std::string from, std::string to);
    std::vector<std::string> listDir(std::string dir);
    bool deleteFile(std::string path);
    // No watcher yet (FileWatcher deferred) — always empty.
    std::vector<std::string> drainChangedPaths();

    // --- paths / config ---
    std::string canonicalize(std::string path);
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length);
    std::string configDir();

    // --- codec (miniz) ---
    rfl::Bytestring zip(std::vector<BackendZipInput> entries);
    std::vector<BackendZipEntry> unzip(std::vector<std::uint8_t> bytes);

    // --- LSDJ sav authoring (test/tooling) ---
    // JSON (an rp::lsdj::model::Sav, lenient) -> encoded .sav bytes. Lets a test author
    // song/sync state directly and boot LSDJ into it. Mirrors cli/HarnessRpcService::savFromJson.
    rfl::Bytestring savFromJson(std::string json);

    // --- emulator lifecycle / reads (a real SameBoySystem in a real Project) ---
    // TS hands concrete paths only; native builds + tracks the system, returning its id
    // (nullopt on an unreadable ROM). The reads pull the pump's latest bytes by id.
    std::optional<std::uint32_t> constructSystem(BackendConstructSpec spec);
    std::optional<std::uint32_t> duplicateSystem(std::uint32_t srcId, std::optional<std::string> savPath);
    std::optional<std::uint32_t> reloadSystem(std::uint32_t id);
    bool removeSystem(std::uint32_t id);
    // value is number|boolean on the TS side; booleans cross as 1/0. The stub applies
    // nothing observable, so it just reports whether the system exists.
    bool applySystemSetting(std::uint32_t id, std::string key, double value);
    // config is a JSON object stringified by the adapter (the stub ignores its contents).
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    // Debug: write a system's latest framebuffer to `path` as an RGB24 PNG (false if no frame
    // published / encode failed). Lets a headless test see what the ROM is actually showing.
    bool screenshot(std::uint32_t id, std::string path);

    // --- DSP-side JS runtime (a second, bare QuickJS context running the role kernel) ---
    // Compile the kernel bundle to QuickJS bytecode (on a scratch context), load it onto the DSP
    // context, then push the system structure. Bytecode + structure JSON cross as bytes; a JSValue
    // never does. The per-block drive happens inside renderAudio, not over RPC.
    std::optional<rfl::Bytestring> compileScript(std::string source);   // nullopt on compile error
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode);
    bool dspSetSystems(std::string json);

    // --- audio render / MIDI drive (drive the real cores, capture their sound) ---
    // sendMidi queues a MIDI message on one system; renderAudio advances the block runner N ms
    // and returns the mixed stereo bus as interleaved f32 (L,R,L,R…). setTransport/setBpm feed
    // the AudioBlockInfo for transport-driven ROMs (mGB needs neither).
    bool            sendMidi(std::uint32_t id, std::vector<std::uint8_t> bytes);
    // Enqueue a button transition on one system (button = GameboyButton value; down = press/release).
    // Spread across the block inside the core; a press+release around a short render is a tap.
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    rfl::Bytestring renderAudio(double ms);
    bool            setTransport(bool running);
    bool            setBpm(double bpm);

    // --- DSP runtime in the render loop ---
    // Stage a global host-MIDI message for the kernel's next render (consumed on its first block).
    // The kernel's midi-routing behaviour fans it to systems; with no routing role it reaches none.
    bool stageMidiIn(std::vector<std::uint8_t> bytes);

private:
    Project    project_;
    double     sampleRate_ = 44100.0;
    DspRuntime dsp_;

    // Audio-render scratch + simulated host transport (mirrors TestHarnessImpl).
    static constexpr std::uint32_t kBlockSize = 1024;
    double             bpm_              = 120.0;
    bool               transportPlaying_ = false;
    double             ppq_              = 0.0;
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;

    // DSP-in-render: whether a kernel is loaded (drives whether the per-block DSP stage runs) +
    // global host MIDI staged for the kernel (consumed on the first block of the next render).
    bool                            dspActive_ = false;
    std::vector<DspRuntime::MidiIn> pendingMidiIn_;
};

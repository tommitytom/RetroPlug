#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "lsdj/Effects.hpp"  // rp::lsdj::LsdjEffect (tagged union: gain/filter/dither)

// Shared DTOs for the Backend RPC surface (packages/retroplug/src/backend.ts).
// Binary crosses as rfl::Bytestring in BOTH directions (a JS Uint8Array): the qjs codec decodes a
// typed byte param straight into rfl::Bytestring (rpcpp's NativeAstCodec path), not a JSON int-array.
// A nullable read is std::optional (absent -> JS null). Zip entry DTOs mirror the harness.
struct BackendZipEntry { std::string name; rfl::Bytestring bytes; };  // unzip output
struct BackendZipInput { std::string name; rfl::Bytestring bytes; };  // zip input

// A raw RGBA8888 image (4 bytes/pixel, row-major, top-to-bottom) — the wire form for the host-facet PNG
// codec (pngEncode/pngDecode). Used both as the encode input and the decode output; `rgba` is
// width*height*4 bytes. The LSDJ font tile↔pixel mapping lives in TS (src/lsdj/rom); this is just the
// generic codec crossing so font import/export works from the plugin as well as the CLI.
struct PngImage { std::uint32_t width; std::uint32_t height; rfl::Bytestring rgba; };

// One source sample for the LSDJ kit compiler (harness-facet compileKit): a source audio file (WAV/MP3/
// FLAC, decoded natively by SampleCache/miniaudio), a 3-char slot name, an optional source window, and an
// effect chain (gain/filter/dither) applied during compilation. Mirrors rp::lsdj::CompileSampleSpec.
struct KitSampleSpec {
    std::string                       path;
    std::string                       name;
    std::optional<std::uint32_t>      offset;
    std::optional<std::uint32_t>      length;
    std::vector<rp::lsdj::LsdjEffect> effects;
};
// A whole-kit compile request: a 6-char kit name + up to 15 samples (extra dropped). `rotate` is the LSDJ
// 9.2.0+ frame rotation, derived from the TARGET ROM's version (absent → true). Output = a 16 KB kit bank
// (rfl::Bytestring) ready to drop into a ROM slot.
struct KitCompileSpec { std::string name; std::vector<KitSampleSpec> samples; std::optional<bool> rotate; };

// One source sample for the risa NES-DPCM kit compiler (harness-facet compileDmc): the same source/window/
// effects as a kit sample, plus the DMC playback-rate index (0..15 into the PAL rate table), a loop flag,
// and a 7-bit peak-normalize flag. Mirrors rp::risa::CompileDmcSampleSpec.
struct RisaDmcSampleSpec {
    std::string                       path;
    std::string                       name;
    std::optional<std::uint32_t>      offset;
    std::optional<std::uint32_t>      length;
    std::vector<rp::lsdj::LsdjEffect> effects;
    std::optional<std::uint32_t>      rate;      // PAL DPCM rate index 0..15 (default 12)
    std::optional<bool>               loop;
    std::optional<bool>               normalize; // 7-bit peak-normalize (default true)
};
struct RisaKitCompileSpec { std::string name; std::vector<RisaDmcSampleSpec> samples; };

// Monotonic audio-capture snapshot published by the background audio thread (energy = sum of
// squared samples, frames = total). A control-thread reader diffs two snapshots for a windowed RMS.
struct AudioCaptured { double energy; std::uint64_t frames; };

// One system's video frame for the UI (mirrors the harness HarnessFrame). `data` is raw XRGB8888,
// width*height*4 bytes (msgpack BIN -> JS Uint8Array); empty until `published`. Read from the
// concurrent FrameBufferTriple, so it is safe to fetch while the audio thread renders.
struct RpcFrame { std::uint32_t width; std::uint32_t height; bool published; rfl::Bytestring data; };

// One host MIDI-out message, under the system whose node sent it (an LSDj's rp.lsdj.serial-out, decoding MI.OUT or
// master sync, or the controller session driving a real Game Boy). `data` is the raw MIDI bytes (msgpack BIN -> JS
// Uint8Array). Accumulated across a renderAudio loop and returned by drainMidiOut for headless tests; the plugin
// drains Engine::midiOut() to the DAW directly instead.
struct RpcMidiOut { std::uint32_t system; std::uint32_t frame; rfl::Bytestring data; };

// renderBlocks (harness facet): render a scripted run of host blocks exactly as the plugin would hand
// them to the Engine, for the parity suite (test-native/parity). One block = one processBlock call.
// `ppq` and `controller` have no PluginDSP::run counterpart (the plugin never calls setPpq; controller
// traffic is the SDL standalone's path); the plugin's CC-automation stage and MIDI-out filter are not modelled.
struct RpcBlockMidi { std::uint32_t frame; std::vector<std::uint8_t> bytes; };
struct RpcRenderBlock {
    std::uint32_t                          frames;      // 1..8192
    std::vector<RpcBlockMidi>              midi;        // host MIDI, staged in list order (frames never go backwards)
    std::vector<std::vector<std::uint8_t>> controller;  // control-surface messages (1..4 bytes each)
    std::optional<bool>                    playing;     // transport, applied after the drain and before the block
    std::optional<double>                  bpm;
    std::optional<double>                  ppq;         // a locate
};
struct RpcRenderBlocksRequest { std::uint32_t outputs; std::vector<RpcRenderBlock> blocks; };
// The host MIDI out, tagged with the block (index into the request) it was emitted in.
struct RpcBlockMidiOut { std::uint32_t block; std::uint32_t system; std::uint32_t frame; rfl::Bytestring data; };
// `error` is empty on success; otherwise it says why the request was refused and nothing was rendered.
// `lanes` are planar f32, one per output, each the whole run long.
struct RpcRenderBlocksResult {
    std::string                  error;
    std::vector<rfl::Bytestring> lanes;
    std::vector<RpcBlockMidiOut> midiOut;
};

// putResource (docs/modular-graph-phase-c3-design.md section 4): one immutable byte block for the engine's
// ResourceTable, whose id a node's settings name as "res:<id>". `type` is "rom", "sram" or "state"; the payload is
// exactly one of `bytes` (a patched ROM, a battery or savestate seed TS holds), `path` (a file native reads, so a 32 MB
// GBA ROM and a .sav never cross the bridge) or `embedded` (a built-in ROM's marker, "mgb"). One spec struct with
// optionals rather than a tagged union: it is how every other field that may be absent is spelt here, and the RPC
// reads which source it got. An absent field is nullopt, never "".
struct BackendResourceSpec {
    std::string                    type;
    std::optional<rfl::Bytestring> bytes;
    std::optional<std::string>     path;
    std::optional<std::string>     embedded;
};
// Its answer: `id` (a live resource was created) or `error` (none was, and why). Exactly one is present.
struct RpcPutResourceResult {
    std::optional<std::uint32_t> id;
    std::optional<std::string>   error;
};

// setGraph: a document TS built, its systems in slot order, and the NES tap re-arm its edit makes. `tap`
// present re-arms (restore, then arm `tap.arm` if it is there); absent, the plan touches no tap. An arm names
// one of the slots and an export mode of 1 to 3, or the engine refuses the document ("tap").
struct RpcTapArm { std::uint32_t id; std::uint32_t mode; };
struct RpcTapRequest { std::optional<RpcTapArm> arm; };
struct RpcSetGraphRequest {
    std::string                  document;
    std::vector<std::uint32_t>   slots;
    std::optional<RpcTapRequest> tap;
};
struct RpcGraphError { std::string nodeId; std::string port; std::string code; std::string message; };
struct RpcSetGraphResult { bool ok; std::vector<RpcGraphError> errors; };

// loadScriptPack (docs/modular-graph-phase-c1-design.md section 7): a pack of script node types, by its id, its source
// (global code, as Engine::loadScriptPack takes it) and the memory limit each of its runtimes is held to, in bytes,
// absent or 0 for the default (64 MiB). The limit is a double so that one which is no whole number of bytes is refused,
// where an integer field would read it truncated (1.5 as 1, -1 as a limit past any heap). The result is the Engine's:
// `error` empty on success, `errors` the planner's when a reload's replan refused (empty for a refusal before any
// replan), and `types` the pack's on success, in registration order, each with its descriptor as the registry holds it.
struct RpcLoadScriptPackRequest {
    std::string           id;
    std::string           source;
    std::optional<double> memoryLimitBytes;
};
struct RpcScriptPackType { std::string id; std::string descriptor; };
struct RpcLoadScriptPackResult {
    bool                           ok;
    std::string                    error;
    std::vector<RpcGraphError>     errors;
    std::vector<RpcScriptPackType> types;
};

// A capability job (docs/modular-graph-phase-c5-design.md section 6, Engine::submitJob): the node it is for, the
// interface ("rp.system.memory/1", "rp.system.sram/1", "rp.system.rom/1") and its operation, and the arguments that
// operation reads. memory: `region` by name, `offset`, and `length` for a read (absent or 0: to the region's end) or
// `bytes` for a write; sram: `bytes` for a restore; rom: `length` for a header (absent or 0: the whole image).
struct RpcJobRequest {
    std::string                    nodeId;
    std::string                    capability;
    std::string                    op;
    std::optional<std::string>     region;
    std::optional<std::uint32_t>   offset;
    std::optional<std::uint32_t>   length;
    std::optional<rfl::Bytestring> bytes;
};
// Its answer: `id` (the job was taken; its completion comes through drainJobs) or `error` (it was refused, and
// why). Exactly one is present.
struct RpcJobSubmitResult {
    std::optional<std::uint32_t> id;
    std::optional<std::string>   error;
};
// One entry drainJobs hands back: a job that finished (`done`, then `ok` with its result in `bytes`, or `error`), or
// one still running that reported progress (`done` false, `progress` of `total`).
struct RpcJobCompletion {
    std::uint32_t   id;
    bool            done;
    bool            ok;
    std::string     error;
    rfl::Bytestring bytes;
    std::uint32_t   progress;
    std::uint32_t   total;
};

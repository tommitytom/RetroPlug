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

// One MIDI-out message a system's DSP kernel emitted (LSDj MI.OUT decoder → emitMidiOut). `data` is the
// raw MIDI bytes (msgpack BIN -> JS Uint8Array). Accumulated across a renderAudio loop and returned by
// drainMidiOut for headless tests — the plugin drains Engine::midiOut() to the DAW directly instead.
struct RpcMidiOut { std::uint32_t system; std::uint32_t frame; rfl::Bytestring data; };

// Mirrors the ConstructSpec (packages/retroplug/src/backend.ts): concrete
// paths + an embedded-ROM marker + optional zip-import seed bytes. The optional string fields are
// absent (nullopt) rather than "" when the TS side has null.
struct BackendConstructSpec {
    std::uint32_t                            id;        // TS owns the id counter; native never allocates
    std::string                              romPath;
    std::string                              embeddedRom;
    std::optional<std::string>               platform;  // TS Platform ("gb"/"nes"/"gba"/"sms"/"gg"); which system a multi-platform core builds
    std::optional<std::string>               core;      // TS Core ("sameboy"/"mesen"); the factory registry key. Absent → defaultCoreFor(platform)
    std::optional<std::string>               savPath;
    std::optional<std::string>               statePath;
    std::optional<std::uint32_t>             replaceId;
    std::optional<rfl::Bytestring> sramBytes;
    std::optional<rfl::Bytestring> stateBytes;
    // Effective ROM bytes to load INSTEAD of slurping romPath — a TS-supplied patched image (e.g. LSDj
    // asset overrides applied non-destructively at construct). romPath still travels for the watcher +
    // .sav resolution; only the loaded bytes differ. Honoured by the SameBoy backend (GB-only feature).
    std::optional<rfl::Bytestring> romBytes;
    std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
};

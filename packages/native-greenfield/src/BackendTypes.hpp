#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

// Shared DTOs for the greenfield Backend RPC surface (packages/retroplug-greenfield/src/backend.ts).
// Binary crosses as rfl::Bytestring in BOTH directions (a JS Uint8Array): the qjs codec decodes a
// typed byte param straight into rfl::Bytestring (rpcpp's NativeAstCodec path), not a JSON int-array.
// A nullable read is std::optional (absent -> JS null). Zip entry DTOs mirror the harness.
struct BackendZipEntry { std::string name; rfl::Bytestring bytes; };  // unzip output
struct BackendZipInput { std::string name; rfl::Bytestring bytes; };  // zip input

// Monotonic audio-capture snapshot published by the background audio thread (energy = sum of
// squared samples, frames = total). A control-thread reader diffs two snapshots for a windowed RMS.
struct AudioCaptured { double energy; std::uint64_t frames; };

// One system's video frame for the UI (mirrors the harness HarnessFrame). `data` is raw XRGB8888,
// width*height*4 bytes (msgpack BIN -> JS Uint8Array); empty until `published`. Read from the
// concurrent FrameBufferTriple, so it is safe to fetch while the audio thread renders.
struct GreenfieldFrame { std::uint32_t width; std::uint32_t height; bool published; rfl::Bytestring data; };

// One MIDI-out message a system's DSP kernel emitted (LSDj MI.OUT decoder → emitMidiOut). `data` is the
// raw MIDI bytes (msgpack BIN -> JS Uint8Array). Accumulated across a renderAudio loop and returned by
// drainMidiOut for headless tests — the plugin drains Engine::midiOut() to the DAW directly instead.
struct GreenfieldMidiOut { std::uint32_t system; std::uint32_t frame; rfl::Bytestring data; };

// Mirrors the greenfield ConstructSpec (packages/retroplug-greenfield/src/backend.ts): concrete
// paths + an embedded-ROM marker + optional zip-import seed bytes. The optional string fields are
// absent (nullopt) rather than "" when the TS side has null.
struct BackendConstructSpec {
    std::uint32_t                            id;        // TS owns the id counter; native never allocates
    std::string                              romPath;
    std::string                              embeddedRom;
    std::optional<std::string>               platform;  // TS Platform ("gb"/"nes"/"gba"); which system a multi-platform core builds
    std::optional<std::string>               core;      // TS Core ("sameboy"/"mesen"); the factory registry key
    std::optional<std::string>               savPath;
    std::optional<std::string>               statePath;
    std::optional<std::uint32_t>             replaceId;
    std::optional<rfl::Bytestring> sramBytes;
    std::optional<rfl::Bytestring> stateBytes;
    std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
};

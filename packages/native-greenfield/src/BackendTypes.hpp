#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

// Shared DTOs for the greenfield Backend RPC surface (packages/retroplug-greenfield/src/backend.ts).
// Byte OUTPUT rides rfl::Bytestring (msgpack BIN -> JS Uint8Array); a nullable read is
// std::optional (absent -> JS null). Byte INPUT rides std::vector<std::uint8_t> (reflect-cpp's
// binary reader is int-array-only). Zip entry DTOs mirror the harness.
struct BackendZipEntry { std::string name; rfl::Bytestring bytes; };            // unzip output
struct BackendZipInput { std::string name; std::vector<std::uint8_t> bytes; };  // zip input

// Monotonic audio-capture snapshot published by the background audio thread (energy = sum of
// squared samples, frames = total). A control-thread reader diffs two snapshots for a windowed RMS.
struct AudioCaptured { double energy; std::uint64_t frames; };

// One system's video frame for the UI (mirrors the harness HarnessFrame). `data` is raw XRGB8888,
// width*height*4 bytes (msgpack BIN -> JS Uint8Array); empty until `published`. Read from the
// concurrent FrameBufferTriple, so it is safe to fetch while the audio thread renders.
struct GreenfieldFrame { std::uint32_t width; std::uint32_t height; bool published; rfl::Bytestring data; };

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
    std::optional<std::vector<std::uint8_t>> sramBytes;
    std::optional<std::vector<std::uint8_t>> stateBytes;
    std::optional<std::string>               settings;  // backend "system"-role config JSON (construct-time)
};

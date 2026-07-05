#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

// The native side of the greenfield `Backend` (packages/retroplug-greenfield/src/backend.ts):
// the emulator-free fs / config / codec primitives, exposed to the TS app + tests over
// rpcpp (reflect-cpp -> QuickJS object codec, same shape as cli/HarnessRpcService). This
// is the "native-greenfield" seam — a clean host over std::filesystem + miniz + config-dir
// resolution, with NO Project / SystemBase (that arrives in increment 2). Method bodies in
// BackendRpcService.cpp.
//
// Byte OUTPUT rides rfl::Bytestring (msgpack BIN -> JS Uint8Array); a nullable read is
// std::optional (absent -> JS null). Byte INPUT rides std::vector<std::uint8_t>
// (reflect-cpp's binary reader is int-array-only). Zip entry DTOs mirror the harness.
struct BackendZipEntry { std::string name; rfl::Bytestring bytes; };            // unzip output
struct BackendZipInput { std::string name; std::vector<std::uint8_t> bytes; };  // zip input

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
};

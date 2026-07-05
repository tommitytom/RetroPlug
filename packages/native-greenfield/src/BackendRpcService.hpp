#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "project/Project.hpp"

// The native side of the greenfield `Backend` (packages/retroplug-greenfield/src/backend.ts),
// exposed to the TS app + tests over rpcpp (reflect-cpp -> QuickJS object codec, same shape
// as cli/HarnessRpcService). Two halves: the fs / config / codec primitives (std::filesystem
// + miniz + config-dir resolution) and the emulator seam — the latter backed by a real
// `Project` of `StubSystem`s (a SystemBase stand-in; no real core yet). Method bodies in
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

    // --- emulator lifecycle / reads (a StubSystem in a real Project) ---
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

private:
    Project project_;
    double  sampleRate_ = 44100.0;
};

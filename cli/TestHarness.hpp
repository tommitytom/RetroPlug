#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Embeds the txiki.js / QuickJS runtime in retroplug-cli and runs a bundled
// JavaScript test module (transpiled from TypeScript by esbuild) against an
// in-process emulator. Exposes an `emu`-style control surface plus a TAP test
// runner to the JS side via the Symbol.for("retroplug") namespace.
//
// Single-threaded by construction: each `emu` call drives Project/SystemBase
// directly the way cli/main.cpp does (no CommandQueue), so press → advance →
// read-memory is immediate and deterministic.
//
// PIMPL keeps the heavy txiki/quickjs headers (and their libwebsockets/mbedtls/
// sqlite transitive includes) out of this header — only TestHarness.cpp pulls
// them in.
class TjsHostRuntime;

class TestHarness {
public:
    TestHarness();
    ~TestHarness();

    TestHarness(const TestHarness&)            = delete;
    TestHarness& operator=(const TestHarness&) = delete;

    // Load + evaluate a bundled .js test module, run every registered test()
    // case, and emit TAP version 13 to stdout. Returns a process exit code:
    // 0 when all cases pass, 1 on any failure or a module-load error.
    int runFile(const std::string& jsPath);

    // Run the embedded end-user CLI bundle (bytecode, or a source file in dev).
    // `argv` (the user args, without argv[0]) is exposed to JS via
    // Symbol.for("retroplug").getArgv(); the bundle sets its exit code via
    // .exit(code). Returns that code, or 1 on an eval error.
    int runBundle(const std::uint8_t* bytecode, std::size_t len,
                  const std::vector<std::string>& argv);
    int runBundleFromFile(const std::string& path,
                          const std::vector<std::string>& argv);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    // The embedded txiki/QuickJS host (forward-declared so this header stays
    // free of the txiki includes — only TestHarness.cpp drives the runtime).
    std::unique_ptr<TjsHostRuntime> host_;
};

#pragma once

#include <memory>
#include <string>

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

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

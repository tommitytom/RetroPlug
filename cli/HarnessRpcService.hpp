#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "TestHarness.hpp"          // TestHarness::Impl (incomplete pointer is fine here)
#include "system/CpuState.hpp"      // rp::CpuRegister
#include "system/DebugTarget.hpp"   // rp::ProfiledFunction / DisasmLine / TraceLine / CallFrame / BreakInfo / BreakpointSpec

// Reflect-cpp DTOs for harness returns that aren't already plain rp:: structs.
// (rp::CpuRegister / ProfiledFunction / DisasmLine / TraceLine / CallFrame /
// BreakInfo / BreakpointSpec are reused verbatim.) Byte buffers use
// rfl::Bytestring so reflect-cpp routes them to msgpack BIN (-> Uint8Array),
// not an int array — see the FrameResponse note in PluginRpcService.hpp.
struct HarnessMidiEvent  { std::uint64_t sample; std::vector<std::uint8_t> bytes; };
struct HarnessSerialByte { std::uint64_t sample; std::uint8_t byte; };
struct HarnessFrame      { std::uint32_t width; std::uint32_t height; bool published; rfl::Bytestring data; };
struct HarnessKitSample  { std::string path; std::string name; };
struct HarnessPerSystemAudio { std::vector<rfl::Bytestring> systems; }; // one interleaved-stereo-f32 buffer per system

// The emulator/debug/fixture surface the cli test harness exposes to TypeScript,
// via rpcpp (reflect-cpp -> OpenRPC -> generated client) instead of the bespoke
// Symbol.for("retroplug") trampolines. A thin wrapper over TestHarness::Impl,
// which drives Project/SystemBase synchronously (it controls time). Compiled
// into retroplug-cli; the schema is dumped by `retroplug-cli --dump-harness-schema`
// (constructs the service with a null Impl — reflection never calls a method).
//
// NOTE (restructure-04): only a representative subset of methods is implemented
// in Stage 0 to prove the reflect-cpp -> TS -> sync-dispatch pipeline end to end.
// The remaining ~28 emu methods are ported (and the old trampolines deleted) as
// the test/harness/index.ts facade is flipped over to the generated client.
class HarnessRpcService {
public:
    explicit HarnessRpcService(TestHarness::Impl* impl) : h_(impl) {}

    // ROM / fixtures. Params use sentinels rather than std::optional: optionals
    // in method-param position produce nested wrapper types the codegen mangles
    // badly, and the trampolines already treat empty/0 as "absent". Binary INPUT
    // is std::vector<std::uint8_t> (reflect-cpp's generic reader rejects
    // std::byte, so rfl::Bytestring is output-only — the plugin never takes
    // binary input because it passes file paths). empty sram = none, empty
    // lsdjSyncMode = none, linkGroup 0 = none.
    std::uint32_t loadRom(std::string path,
                          std::vector<std::uint8_t> sram,
                          std::string lsdjSyncMode,
                          std::uint32_t linkGroup);

    // exec / transport
    void runMs(double ms);
    void press(std::uint32_t systemId, std::int32_t button, bool down);

    // captures
    std::vector<HarnessMidiEvent> drainMidi(std::uint32_t systemId);

    // memory / cpu
    rfl::Bytestring readMemory(std::uint32_t systemId, std::uint32_t type);
    std::vector<rp::CpuRegister> getRegisters(std::uint32_t systemId);

    // frame / audio
    HarnessFrame getFrame(std::uint32_t systemId);
    rfl::Bytestring getAudio(double ms);

    // debug (Mesen NES) — reused DTO return
    rp::BreakInfo runUntilBreak(std::uint32_t systemId, std::uint64_t maxCycles);

private:
    TestHarness::Impl* h_;
};

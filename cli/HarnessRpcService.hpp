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

    // ROM / SRAM / fixtures. Params use sentinels rather than std::optional:
    // optionals in method-param position produce nested wrapper types the codegen
    // mangles badly, and the trampolines already treat empty/0 as "absent".
    // Binary INPUT is std::vector<std::uint8_t> (reflect-cpp's generic reader
    // rejects std::byte, so rfl::Bytestring is output-only — the plugin never
    // takes binary input because it passes file paths). empty sram = none,
    // empty lsdjSyncMode = none, linkGroup 0 = none.
    std::uint32_t loadRom(std::string path,
                          std::vector<std::uint8_t> sram,
                          std::string lsdjSyncMode,
                          std::uint32_t linkGroup);
    rfl::Bytestring savFromJson(std::string json);
    bool loadSram(std::uint32_t systemId, std::vector<std::uint8_t> sram);
    void reset(std::uint32_t systemId);
    rfl::Bytestring readFile(std::string path);
    void writeFile(std::string path, std::vector<std::uint8_t> bytes);
    std::int32_t savRoundtripDiff(std::vector<std::uint8_t> sav);

    // exec / transport
    void runMs(double ms);
    void press(std::uint32_t systemId, std::int32_t button, bool down);
    void sendMidi(std::uint32_t systemId, std::vector<std::uint8_t> bytes);
    void setTransport(bool running);
    void setBpm(double bpm);

    // captures
    std::vector<HarnessMidiEvent> drainMidi(std::uint32_t systemId);
    std::vector<HarnessSerialByte> drainSerial(std::uint32_t systemId);

    // memory / cpu
    rfl::Bytestring readMemory(std::uint32_t systemId, std::uint32_t type);
    std::vector<rp::CpuRegister> getRegisters(std::uint32_t systemId);
    void setRegister(std::uint32_t systemId, std::string name, std::int64_t value);
    std::int32_t readCpu(std::uint32_t systemId, std::uint32_t addr);
    std::uint64_t step(std::uint32_t systemId);
    bool runUntilPc(std::uint32_t systemId, std::uint32_t pc, std::uint64_t maxCycles);

    // frame / audio
    HarnessFrame getFrame(std::uint32_t systemId);
    bool screenshot(std::uint32_t systemId, std::string path);
    rfl::Bytestring getAudio(double ms);
    HarnessPerSystemAudio runMsPerSystem(double ms);
    void writeWav(std::string path, std::vector<std::uint8_t> samples, std::uint32_t sampleRate);

    // project / kits
    void saveRplg(std::string path);
    std::uint32_t loadRplg(std::string path);
    void patchKit(std::uint32_t systemId, std::uint32_t slot, std::string name,
                  std::vector<HarnessKitSample> samples);

    // debug (Mesen NES) — reused rp:: DTO returns
    void beginProfile(std::uint32_t systemId);
    std::vector<rp::ProfiledFunction> readProfile(std::uint32_t systemId);
    bool loadLabels(std::uint32_t systemId, std::string path);
    std::vector<rp::DisasmLine> disassemble(std::uint32_t systemId, std::uint32_t addr, std::uint32_t count);
    void setTrace(std::uint32_t systemId, bool on);
    std::vector<rp::TraceLine> readTrace(std::uint32_t systemId, std::uint32_t count);
    std::vector<rp::CallFrame> getCallStack(std::uint32_t systemId);
    void setBreakpoints(std::uint32_t systemId, std::vector<rp::BreakpointSpec> bps);
    rp::BreakInfo runUntilBreak(std::uint32_t systemId, std::uint64_t maxCycles);
    rp::BreakInfo stepInto(std::uint32_t systemId);
    rp::BreakInfo stepOver(std::uint32_t systemId);
    rp::BreakInfo stepOut(std::uint32_t systemId);

private:
    TestHarness::Impl* h_;
};

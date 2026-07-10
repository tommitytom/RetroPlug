#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "AudioDriverRpcService.hpp"
#include "BackendTypes.hpp"
#include "Engine.hpp"
#include "EngineInvoker.hpp"
#include "EngineRpcService.hpp"
#include "HostRpcService.hpp"
#include "SystemFactory.hpp"

// One object per RPC server. Owns the shared Engine + invokers + factory + audio-running flag, and
// has-a the three concern-separated services (fs/config/codec, emulator/kernel, audio thread). Every
// RPC method forwards a single line to its sub-object — all logic lives in the sub-objects, while the
// single __rpcSend channel keeps one registration target. The method identifiers ARE the wire names
// (rpcpp derives each from the pointer-to-member), so they must match the greenfield Backend surface.
class BackendFacade {
public:
    BackendFacade();
    BackendFacade(const BackendFacade&)            = delete;
    BackendFacade& operator=(const BackendFacade&) = delete;

    // --- filesystem / config / codec / sav → host_ ---
    std::optional<rfl::Bytestring> readFile(std::string path) { return host_.readFile(std::move(path)); }
    bool writeFile(std::string path, rfl::Bytestring bytes) { return host_.writeFile(std::move(path), std::move(bytes)); }
    bool writeFileAtomic(std::string path, rfl::Bytestring bytes) { return host_.writeFileAtomic(std::move(path), std::move(bytes)); }
    bool fileExists(std::string path) { return host_.fileExists(std::move(path)); }
    bool rename(std::string from, std::string to) { return host_.rename(std::move(from), std::move(to)); }
    std::vector<std::string> listDir(std::string dir) { return host_.listDir(std::move(dir)); }
    bool deleteFile(std::string path) { return host_.deleteFile(std::move(path)); }
    std::vector<std::string> drainChangedPaths() { return host_.drainChangedPaths(); }
    std::string canonicalize(std::string path) { return host_.canonicalize(std::move(path)); }
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length) { return host_.readFilePrefix(std::move(path), length); }
    std::string configDir() { return host_.configDir(); }
    std::string version() { return host_.version(); }
    rfl::Bytestring zip(std::vector<BackendZipInput> entries) { return host_.zip(std::move(entries)); }
    std::vector<BackendZipEntry> unzip(rfl::Bytestring bytes) { return host_.unzip(std::move(bytes)); }
    rfl::Bytestring savFromJson(std::string json) { return host_.savFromJson(std::move(json)); }

    // --- emulator lifecycle / reads / kernel / MIDI / transport → engine_svc_ ---
    bool constructSystem(BackendConstructSpec spec) { return engine_svc_.constructSystem(std::move(spec)); }
    bool removeSystem(std::uint32_t id) { return engine_svc_.removeSystem(id); }
    bool applySystemSetting(std::uint32_t id, std::string key, double value) { return engine_svc_.applySystemSetting(id, std::move(key), value); }
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config) { return engine_svc_.applyRoleConfig(id, std::move(kind), std::move(config)); }
    std::optional<rfl::Bytestring> readState(std::uint32_t id) { return engine_svc_.readState(id); }
    std::optional<rfl::Bytestring> readSram(std::uint32_t id) { return engine_svc_.readSram(id); }
    bool screenshot(std::uint32_t id, std::string path) { return engine_svc_.screenshot(id, std::move(path)); }
    GreenfieldFrame getFrame(std::uint32_t id) { return engine_svc_.getFrame(id); }
    rp::ApuState getApuState(std::uint32_t id) { return engine_svc_.getApuState(id); }
    rp::PpuState getPpuState(std::uint32_t id) { return engine_svc_.getPpuState(id); }
    std::optional<std::uint8_t> readCpu(std::uint32_t id, std::uint32_t addr) { return engine_svc_.readCpu(id, addr); }
    bool writeCpu(std::uint32_t id, std::uint32_t addr, std::uint32_t value) { return engine_svc_.writeCpu(id, addr, value); }
    rfl::Bytestring readMemory(std::uint32_t id, std::uint32_t memType) { return engine_svc_.readMemory(id, memType); }
    std::vector<rp::CpuRegister> getCpuRegisters(std::uint32_t id) { return engine_svc_.getCpuRegisters(id); }
    std::uint64_t stepInstruction(std::uint32_t id) { return engine_svc_.stepInstruction(id); }
    std::vector<rp::DebugEvent> drainEvents(std::uint32_t id) { return engine_svc_.drainEvents(id); }
    bool loadLabels(std::uint32_t id, std::string path) { return engine_svc_.loadLabels(id, std::move(path)); }
    bool setCpuRegister(std::uint32_t id, std::string name, std::uint32_t value) { return engine_svc_.setCpuRegister(id, std::move(name), value); }
    bool runUntilPc(std::uint32_t id, std::uint32_t target, std::uint64_t maxCycles) { return engine_svc_.runUntilPc(id, target, maxCycles); }
    bool setBreakpoints(std::uint32_t id, std::vector<rp::BreakpointSpec> bps) { return engine_svc_.setBreakpoints(id, std::move(bps)); }
    rp::BreakInfo runUntilBreak(std::uint32_t id, std::uint64_t maxCycles) { return engine_svc_.runUntilBreak(id, maxCycles); }
    bool setTrace(std::uint32_t id, bool on) { return engine_svc_.setTrace(id, on); }
    std::vector<rp::TraceLine> readTrace(std::uint32_t id, std::uint32_t count) { return engine_svc_.readTrace(id, count); }
    rp::BreakInfo stepInto(std::uint32_t id) { return engine_svc_.stepInto(id); }
    rp::BreakInfo stepOver(std::uint32_t id) { return engine_svc_.stepOver(id); }
    rp::BreakInfo stepOut(std::uint32_t id) { return engine_svc_.stepOut(id); }
    bool beginProfile(std::uint32_t id) { return engine_svc_.beginProfile(id); }
    std::vector<rp::ProfiledFunction> readProfile(std::uint32_t id) { return engine_svc_.readProfile(id); }
    std::vector<rp::DisasmLine> disassemble(std::uint32_t id, std::uint32_t addr, std::uint32_t count) { return engine_svc_.disassemble(id, addr, count); }
    std::vector<rp::CallFrame> getCallStack(std::uint32_t id) { return engine_svc_.getCallStack(id); }
    std::optional<rfl::Bytestring> compileScript(std::string source) { return engine_svc_.compileScript(std::move(source)); }
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode) { return engine_svc_.dspLoadKernel(std::move(bytecode)); }
    bool dspSetSystems(std::string json) { return engine_svc_.dspSetSystems(std::move(json)); }
    bool pressButton(std::uint32_t id, std::uint32_t button, bool down) { return engine_svc_.pressButton(id, button, down); }
    rfl::Bytestring renderAudio(double ms) { return engine_svc_.renderAudio(ms); }
    std::vector<rfl::Bytestring> renderAudioPerSystem(double ms) { return engine_svc_.renderAudioPerSystem(ms); }
    bool setTransport(bool running) { return engine_svc_.setTransport(running); }
    bool setBpm(double bpm) { return engine_svc_.setBpm(bpm); }
    bool setAudioRouting(std::uint32_t mode) { return engine_svc_.setAudioRouting(mode); }
    bool stageMidiIn(std::vector<std::uint8_t> bytes) { return engine_svc_.stageMidiIn(std::move(bytes)); }
    bool setSerialOutCapture(std::uint32_t id, bool on) { return engine_svc_.setSerialOutCapture(id, on); }
    std::vector<GreenfieldMidiOut> drainMidiOut() { return engine_svc_.drainMidiOut(); }

    // --- DSP-runtime allocation/GC profiling → engine_svc_ (spec/08-profiling.md; enabled off-profile) ---
    DspAllocStats dspAllocStats() { return engine_svc_.dspAllocStats(); }
    bool          dspResetAllocStats(bool disableAutoGc) { return engine_svc_.dspResetAllocStats(disableAutoGc); }
    DspGcResult   dspRunGc() { return engine_svc_.dspRunGc(); }
    bool                      dspTraceReset(bool arm) { return engine_svc_.dspTraceReset(arm); }
    std::vector<DspTraceSpan> dspTrace() { return engine_svc_.dspTrace(); }
    std::vector<std::string>  dspTraceNames() { return engine_svc_.dspTraceNames(); }

    // --- background audio thread → driver_ (test host only; the plugin drives run() directly) ---
    bool          startAudio() { return driver_.startAudio(); }
    bool          stopAudio() { return driver_.stopAudio(); }
    AudioCaptured audioCaptured() { return driver_.audioCaptured(); }
    bool          sleepMs(double ms) { return driver_.sleepMs(ms); }
    std::uint32_t systemCount() { return driver_.systemCount(); }
    std::uint32_t drainReleased() { return driver_.drainReleased(); }

    // --- DPF plugin driving: the host's run()/activate() replace the AudioDriverRpcService loop.
    // NOT wire methods — the plugin holds the facade directly and calls these from C++.
    void setSampleRate(double sr) { engine_.setSampleRate(sr); }
    // Enter/leave audio-active: audioRunning_ THEN active_ (the invariant that makes main-thread reads
    // fail-safe instead of racing run()); deactivate frees pending payloads + reclaims released cores.
    void pluginActivate();
    void pluginDeactivate();
    // Stage one host-MIDI message directly on the audio thread (bypasses the ring + its 4-byte cap;
    // safe because run() owns the Engine while active). Call before pluginProcessBlock.
    void stageMidiRaw(const std::uint8_t* data, std::size_t size) {
        engine_.stageMidi(std::vector<std::uint8_t>(data, data + size));
    }
    // One audio block: drain control-thread edits → set transport → render into the output channels
    // (the plugin's 4 stereo pairs; routed per audioRouting by the Engine's MultiOutRouter).
    void pluginProcessBlock(double bpm, bool playing, std::uint32_t frames,
                            float* const* outputs, std::uint32_t numOutputs);
    // The kernel's MIDI-out for the block just rendered (drain to the DAW, then clear).
    const std::vector<DspRuntime::MidiOut>& pluginMidiOut() const { return engine_.midiOut(); }
    void pluginClearMidiOut() { engine_.clearMidiOut(); }

private:
    // Shared state (owned here; the services hold references). Declaration order is load-bearing —
    // the services' member initializers below reference these.
    Engine         engine_;
    SystemFactory  factory_;
    QueuedInvoker  invoker_{engine_, engine_.registry()};  // the ONE mutation path (queue + inline flush)

    HostRpcService        host_;
    EngineRpcService      engine_svc_{engine_, factory_, invoker_};
    AudioDriverRpcService driver_{engine_, invoker_};
};

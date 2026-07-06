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
    bool writeFile(std::string path, std::vector<std::uint8_t> bytes) { return host_.writeFile(std::move(path), std::move(bytes)); }
    bool writeFileAtomic(std::string path, std::vector<std::uint8_t> bytes) { return host_.writeFileAtomic(std::move(path), std::move(bytes)); }
    bool fileExists(std::string path) { return host_.fileExists(std::move(path)); }
    bool rename(std::string from, std::string to) { return host_.rename(std::move(from), std::move(to)); }
    std::vector<std::string> listDir(std::string dir) { return host_.listDir(std::move(dir)); }
    bool deleteFile(std::string path) { return host_.deleteFile(std::move(path)); }
    std::vector<std::string> drainChangedPaths() { return host_.drainChangedPaths(); }
    std::string canonicalize(std::string path) { return host_.canonicalize(std::move(path)); }
    std::optional<rfl::Bytestring> readFilePrefix(std::string path, std::uint32_t length) { return host_.readFilePrefix(std::move(path), length); }
    std::string configDir() { return host_.configDir(); }
    rfl::Bytestring zip(std::vector<BackendZipInput> entries) { return host_.zip(std::move(entries)); }
    std::vector<BackendZipEntry> unzip(std::vector<std::uint8_t> bytes) { return host_.unzip(std::move(bytes)); }
    rfl::Bytestring savFromJson(std::string json) { return host_.savFromJson(std::move(json)); }

    // --- emulator lifecycle / reads / kernel / MIDI / transport → engine_svc_ ---
    std::optional<std::uint32_t> constructSystem(BackendConstructSpec spec) { return engine_svc_.constructSystem(std::move(spec)); }
    std::optional<std::uint32_t> duplicateSystem(std::uint32_t srcId, std::optional<std::string> savPath) { return engine_svc_.duplicateSystem(srcId, std::move(savPath)); }
    std::optional<std::uint32_t> reloadSystem(std::uint32_t id) { return engine_svc_.reloadSystem(id); }
    bool removeSystem(std::uint32_t id) { return engine_svc_.removeSystem(id); }
    bool applySystemSetting(std::uint32_t id, std::string key, double value) { return engine_svc_.applySystemSetting(id, std::move(key), value); }
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config) { return engine_svc_.applyRoleConfig(id, std::move(kind), std::move(config)); }
    std::optional<rfl::Bytestring> readState(std::uint32_t id) { return engine_svc_.readState(id); }
    std::optional<rfl::Bytestring> readSram(std::uint32_t id) { return engine_svc_.readSram(id); }
    bool screenshot(std::uint32_t id, std::string path) { return engine_svc_.screenshot(id, std::move(path)); }
    std::optional<rfl::Bytestring> compileScript(std::string source) { return engine_svc_.compileScript(std::move(source)); }
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode) { return engine_svc_.dspLoadKernel(std::move(bytecode)); }
    bool dspSetSystems(std::string json) { return engine_svc_.dspSetSystems(std::move(json)); }
    bool sendMidi(std::uint32_t id, std::vector<std::uint8_t> bytes) { return engine_svc_.sendMidi(id, std::move(bytes)); }
    bool pressButton(std::uint32_t id, std::uint32_t button, bool down) { return engine_svc_.pressButton(id, button, down); }
    rfl::Bytestring renderAudio(double ms) { return engine_svc_.renderAudio(ms); }
    bool setTransport(bool running) { return engine_svc_.setTransport(running); }
    bool setBpm(double bpm) { return engine_svc_.setBpm(bpm); }
    bool stageMidiIn(std::vector<std::uint8_t> bytes) { return engine_svc_.stageMidiIn(std::move(bytes)); }

    // --- background audio thread → driver_ ---
    bool          startAudio() { return driver_.startAudio(); }
    bool          stopAudio() { return driver_.stopAudio(); }
    AudioCaptured audioCaptured() { return driver_.audioCaptured(); }
    bool          sleepMs(double ms) { return driver_.sleepMs(ms); }
    std::uint32_t systemCount() { return driver_.systemCount(); }
    std::uint32_t drainReleased() { return driver_.drainReleased(); }

private:
    // Shared state (owned here; the services hold references). Declaration order is load-bearing —
    // the services' member initializers below reference these.
    Engine         engine_;
    SystemFactory  factory_;
    DirectInvoker  direct_{engine_};
    QueuedInvoker  queued_;
    EngineInvoker* active_ = &direct_;      // direct_ when quiescent, queued_ while the audio thread runs
    std::atomic<bool> audioRunning_{false};

    HostRpcService        host_;
    EngineRpcService      engine_svc_{engine_, factory_, active_, audioRunning_};
    AudioDriverRpcService driver_{engine_, queued_, direct_, active_, audioRunning_};
};

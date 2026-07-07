#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "BackendTypes.hpp"

class Engine;
class SystemFactory;
class EngineInvoker;

// The emulator surface (lifecycle / reads / DSP kernel / MIDI / transport) as a THIN RPC layer over
// (SystemFactory + Engine + EngineInvoker). No threading branches: mutations go through `active_`
// (which the audio driver points at direct_ or queued_); the genuinely-deferred live reads keep a
// single fail-safe guard against `audioRunning_` until the snapshot triple-buffers land.
class EngineRpcService {
public:
    EngineRpcService(Engine& engine, SystemFactory& factory, EngineInvoker*& active,
                     const std::atomic<bool>& audioRunning);

    // --- emulator lifecycle / reads ---
    std::optional<std::uint32_t> constructSystem(BackendConstructSpec spec);
    std::optional<std::uint32_t> duplicateSystem(std::uint32_t srcId, std::optional<std::string> savPath);
    std::optional<std::uint32_t> reloadSystem(std::uint32_t id);
    bool removeSystem(std::uint32_t id);
    bool applySystemSetting(std::uint32_t id, std::string key, double value);
    bool applyRoleConfig(std::uint32_t id, std::string kind, std::string config);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    bool screenshot(std::uint32_t id, std::string path);
    GreenfieldFrame getFrame(std::uint32_t id);

    // --- DSP-side JS runtime (the role kernel) ---
    std::optional<rfl::Bytestring> compileScript(std::string source);
    bool dspLoadKernel(std::vector<std::uint8_t> bytecode);
    bool dspSetSystems(std::string json);

    // --- audio render / input drive / transport ---
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    rfl::Bytestring renderAudio(double ms);
    bool            setTransport(bool running);
    bool            setBpm(double bpm);
    bool            setAudioRouting(std::uint32_t mode);
    bool            stageMidiIn(std::vector<std::uint8_t> bytes);

private:
    Engine&                  engine_;
    SystemFactory&           factory_;
    EngineInvoker*&          active_;       // the facade's invoker pointer (direct_ or queued_)
    const std::atomic<bool>& audioRunning_; // gates the deferred live reads

    static constexpr std::uint32_t kBlockSize = 1024;
    std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
    std::vector<float>       scratchR_;
};

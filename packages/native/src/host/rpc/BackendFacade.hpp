#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/AudioDriverRpcService.hpp"
#include "host/rpc/BackendTypes.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/DebugRpcService.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "host/rpc/HostRpcService.hpp"
#include "system/SystemFactory.hpp"

// Owns the shared Engine + invoker + factory and has-a the three concern-separated services
// (fs/config/codec, emulator/kernel, audio thread). It is the RPC server's primary object, but it no
// longer forwards every wire method: the per-facet register functions (BackendRpcRegistration.hpp)
// mount each service's methods directly onto the server via rpcpp's cross-object addMethod, so a host
// exposes exactly the facets it's allowed to. The service accessors below hand those instances to the
// register functions. The only members that stay here are the non-wire plugin-driving methods the DPF
// host calls in C++ (the plugin never routes audio through RPC).
class BackendFacade {
public:
    BackendFacade();
    BackendFacade(const BackendFacade&)            = delete;
    BackendFacade& operator=(const BackendFacade&) = delete;

    // Service instances for the per-facet register functions to mount (see BackendRpcRegistration.hpp).
    HostRpcService&        host()   { return host_; }
    EngineRpcService&      engine() { return engine_svc_; }
    DebugRpcService&       debug()  { return debug_; }
    AudioDriverRpcService& driver() { return driver_; }

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
    DebugRpcService       debug_{engine_};  // live-core inspection (CLI-only facet)
    AudioDriverRpcService driver_{engine_, invoker_};
};

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/SystemTypes.hpp"  // SystemId
#include "transport/SpscRing.hpp"

#include "DspCommand.hpp"
#include "DspEvent.hpp"

class Engine;
class SystemBase;
class SnapshotRegistry;

// Where "apply now vs apply on the audio thread" lives — the ONLY threading-aware layer above the
// Engine. The RPC methods call an EngineInvoker without knowing which mode they're in. Both
// concrete invokers drive the SAME Engine methods (one definition each).
class EngineInvoker {
public:
    virtual ~EngineInvoker() = default;
    virtual void adoptSystem(std::unique_ptr<SystemBase> sys)               = 0;
    virtual void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) = 0;
    virtual void removeSystem(SystemId id)                                 = 0;
    virtual void loadKernel(std::vector<std::uint8_t> bytecode)            = 0;
    virtual void setSystems(std::string json)                             = 0;
    virtual void stageMidi(std::vector<std::uint8_t> bytes)               = 0;
    virtual void setBpm(double bpm)                                       = 0;
    virtual void setTransport(bool playing)                              = 0;
    virtual void setAudioRouting(std::uint8_t mode)                      = 0;
    virtual void applyConfigField(SystemId id, std::uint8_t field, double value) = 0;
    virtual void pressButton(SystemId id, std::uint8_t button, bool down) = 0;
};

// Quiescent / CLI: straight through to the Engine on the calling thread. Removed/displaced cores
// are deleted here (the returned unique_ptr drops).
class DirectInvoker final : public EngineInvoker {
public:
    explicit DirectInvoker(Engine& engine) : engine_(engine) {}
    void adoptSystem(std::unique_ptr<SystemBase> sys) override;
    void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) override;
    void removeSystem(SystemId id) override;
    void loadKernel(std::vector<std::uint8_t> bytecode) override;
    void setSystems(std::string json) override;
    void stageMidi(std::vector<std::uint8_t> bytes) override;
    void setBpm(double bpm) override;
    void setTransport(bool playing) override;
    void setAudioRouting(std::uint8_t mode) override;
    void applyConfigField(SystemId id, std::uint8_t field, double value) override;
    void pressButton(SystemId id, std::uint8_t button, bool down) override;

private:
    Engine& engine_;
};

// Threaded: the producer half (control thread) enqueues a POD DspCommand; drainInto() (audio
// thread) applies each command INTO the Engine and routes removed/displaced cores back through the
// release ring for the control thread to delete via popReleased(). A full command ring drops the op
// (rare, user-initiated structural edits; the ring is 256 deep) — heap payloads are freed on the
// drop so nothing leaks.
class QueuedInvoker final : public EngineInvoker {
public:
    // Holds the registry so the two paths that DELETE a claimed-but-unadopted core on the control
    // thread (a full command ring in adopt/replace; freePending after join) also free its slot.
    explicit QueuedInvoker(SnapshotRegistry& registry) : registry_(&registry) {}

    // producer half (control thread)
    void adoptSystem(std::unique_ptr<SystemBase> sys) override;
    void replaceSystem(SystemId id, std::unique_ptr<SystemBase> sys) override;
    void removeSystem(SystemId id) override;
    void loadKernel(std::vector<std::uint8_t> bytecode) override;
    void setSystems(std::string json) override;
    void stageMidi(std::vector<std::uint8_t> bytes) override;
    void setBpm(double bpm) override;
    void setTransport(bool playing) override;
    void setAudioRouting(std::uint8_t mode) override;
    void applyConfigField(SystemId id, std::uint8_t field, double value) override;
    void pressButton(SystemId id, std::uint8_t button, bool down) override;

    // consumer half (audio thread): apply every queued command into the Engine.
    void drainInto(Engine& engine);
    // control thread: pop one released core (nullptr when empty) to delete off the audio thread.
    std::unique_ptr<SystemBase> popReleased();
    // after the audio thread is joined: free un-applied command payloads (single-accessor again).
    void freePending();

private:
    void handBackReleased(SystemBase* sys);  // audio thread → release ring

    SpscRing<DspCommand, 256> commands_;   // control → audio
    SpscRing<DspEvent, 256>   released_;    // audio  → control (raw SystemBase* to delete)
    SnapshotRegistry*         registry_ = nullptr;
};

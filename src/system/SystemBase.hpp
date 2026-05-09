#pragma once

#include <cstdint>

#include "system/SystemTypes.hpp"
#include "system/SystemConfig.hpp"
#include "transport/FrameBufferTriple.hpp"

// Polymorphic runtime representation of one emulator instance. Owned by the
// DSP thread inside Project. Concrete subclasses: SameBoySystem (Step 1),
// MesenSystem (Step 10).
//
// Persistence: snapshotConfig() returns a plain-data SystemConfig that the
// DSP can serialize via reflectcpp from getState(). The runtime polymorphic
// state never enters the JSON path.
class SystemBase {
public:
    explicit SystemBase(SystemId id) : id_(id) {}
    virtual ~SystemBase() = default;

    SystemBase(const SystemBase&) = delete;
    SystemBase& operator=(const SystemBase&) = delete;

    SystemId id() const { return id_; }

    virtual SystemKind kind() const = 0;

    virtual void onActivate(double sampleRate) = 0;
    virtual void onDeactivate() {}
    virtual void onSampleRateChanged(double sampleRate) = 0;
    virtual void onReset() {}

    // Audio-thread per-block entry. `outs[0]` and `outs[1]` are planar L/R
    // buffers (DPF convention). Implementations must SUM into outs, not
    // overwrite, so multiple systems can mix into a single output pair.
    virtual void onProcess(const AudioBlockInfo& info, float* const* outs) = 0;

    virtual void onMidi(const void* /*events*/, std::uint32_t /*count*/) {}

    // Returns nullptr for systems without video (or before activation).
    virtual FrameBufferTriple* framebuffer() { return nullptr; }

    // Round-trips current state back to a plain-data config. Called from
    // Plugin::getState (rare; off-path). May allocate.
    virtual SystemConfig snapshotConfig() const = 0;

private:
    SystemId id_;
};

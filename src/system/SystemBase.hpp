#pragma once

#include <cstdint>
#include <vector>

#include "system/InputTypes.hpp"
#include "system/SystemTypes.hpp"
#include "system/SystemConfig.hpp"
#include "transport/FrameBufferTriple.hpp"
#include "transport/MidiTypes.hpp"

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

    // Audio-thread MIDI delivery. Project::dispatchMidi calls this once per
    // batch with the routing-filtered events that target this system. Default
    // is a no-op; concrete systems override to fan out to roles or to drive
    // their serial buffer.
    virtual void onMidi(const ::MidiEvent* /*events*/, std::uint32_t /*count*/) {}

    // Audio-thread: enqueue a button transition. The system applies it at the
    // next opportunity (typically: spread across the next audio block so
    // multi-press sequences don't all collapse to one sample).
    virtual void pressButton(GameboyButton /*button*/, bool /*down*/) {}

    // Returns nullptr for systems without video (or before activation).
    virtual FrameBufferTriple* framebuffer() { return nullptr; }

    // Per-block MIDI output, drained by PluginDSP into DPF's writeMidiEvent
    // after onProcess. Roles are expected to push into this in onProcessBlock
    // (step 09+). The audio-thread driver is responsible for clearing it at
    // the top of each block. Empty until step 09 fills it.
    std::vector<::MidiEvent>&       midiOut()       { return midiOut_; }
    const std::vector<::MidiEvent>& midiOut() const { return midiOut_; }

    // Round-trips current state back to a plain-data config. Called from
    // Plugin::getState (rare; off-path). May allocate.
    virtual SystemConfig snapshotConfig() const = 0;

protected:
    std::vector<::MidiEvent> midiOut_;

private:
    SystemId id_;
};

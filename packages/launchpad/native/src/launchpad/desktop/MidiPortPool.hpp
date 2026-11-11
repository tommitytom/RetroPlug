#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "midi/MidiBackend.hpp"

namespace rp::launchpad {

// Single-direction MIDI ports opened and driven from TS on the UI thread, by integer handle. This is what a
// control-surface SCAN is built from (open every input, write a probe out of each output in turn, read what
// came back), and it is deliberately the generic shape rather than a scanner: the probe, the timing and the
// meaning of the answers are all TS's, so a second kind of device (or an extension that needs to talk to some box
// once to configure it) needs no native code.
//
// Not real-time. Inputs queue what arrives (on the backend's callback thread) behind a mutex, and TS reads
// the queue once a frame. Every call is UI-thread only.
class MidiPortPool {
public:
    using Message = std::vector<std::uint8_t>;

    /** Fired on the UI thread when the number of open INPUTS goes 0 -> 1 (`held` true) and back to 0. A
     *  Windows MIDI input is exclusive, so the host takes its shared stream's hardware inputs down while the
     *  pool holds any - otherwise a scan could not hear the very interface the user picked as their input,
     *  which is exactly the rig a DIN-attached control surface is on. Outputs deliberately do not count: the
     *  host's output is the user's synth, and cutting it for a scan would hang held notes; a scan simply
     *  cannot probe a port that output holds. Tied to the pool rather than to a hook TS calls, so a scan that
     *  dies half way still gives the inputs back on close. */
    using InputsHeldFn = std::function<void(bool held)>;

    /** Messages kept per input between reads. A port with a sequencer on it can talk continuously and is
     *  read once a frame; overflow drops the newest and counts it. */
    static constexpr std::size_t kMaxQueued = 256;

    explicit MidiPortPool(rp::midi::MidiBackend& backend) : backend_(backend) {}
    ~MidiPortPool() { closeAll(); }
    MidiPortPool(const MidiPortPool&)            = delete;
    MidiPortPool& operator=(const MidiPortPool&) = delete;

    void setOnInputsHeld(InputsHeldFn fn) { onInputsHeld_ = std::move(fn); }

    /** Open several inputs as ONE held edge: a scan opens every input at once, and opening them one by one
     *  would cost the host an audio stop/start for every leading input that refuses. A handle per name, 0
     *  for each that would not open (lastError() says why, for the first). */
    std::vector<int> openInputs(const std::vector<std::string>& names);
    /** Open one output. A handle > 0, or 0 when it would not open. */
    int openOutput(const std::string& name);
    std::string lastError() const { return error_; }

    /** Write to an output handle. False for an unknown handle or an input. */
    bool send(int handle, const std::uint8_t* data, std::size_t n);

    /** Move out what an input handle received since the last read. Empty for an output / unknown handle. */
    std::vector<Message> read(int handle);

    /** Messages an input dropped to a full queue since it opened. */
    std::uint64_t dropped(int handle) const;

    void        close(int handle);
    void        closeAll();
    std::size_t inputCount() const { return inputs_; }

private:
    struct Queue {
        std::mutex           m;
        std::vector<Message> messages;
        std::uint64_t        dropped = 0;
    };
    struct Entry {
        bool                                input = false;
        std::shared_ptr<Queue>              queue;  // shared with the receiver, so it outlives a late callback
        std::unique_ptr<rp::midi::MidiPort> port;   // LAST: destroyed first, cancelling the callback
    };

    int openOne(bool input, const std::string& name);  // no held edges
    void heldEdge(bool held) {
        if (onInputsHeld_) onInputsHeld_(held);
    }

    rp::midi::MidiBackend& backend_;
    InputsHeldFn           onInputsHeld_;
    std::map<int, Entry> ports_;
    int                  next_   = 1;
    std::size_t          inputs_ = 0;
    std::string          error_;
};

}  // namespace rp::launchpad

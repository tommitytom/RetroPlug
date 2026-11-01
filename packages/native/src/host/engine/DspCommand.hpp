#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rp::host { struct Job; }

namespace rp::host {

// A staged message's target: the document id of the node it is aimed at, and its bytes. The control thread allocates
// one per staged message (EngineInvoker::stageNodeMidi) and the audio thread deletes it after handing the bytes to the
// node the plan running at that point of the drain places under that id: a document may replace the node between the
// push and the drain, so the id is resolved there and not here.
struct StagedNodeMessage {
    std::string               node;
    std::vector<std::uint8_t> bytes;
};

}  // namespace rp::host

// A control-thread → audio-thread command for the engine, carried on the GraphHost's one ring as an
// opaque HostCommand (docs/modular-graph-phase-b1-design.md section 5.1), in push order with the plan
// swaps. POD / trivially copyable so it rides the ring as bytes. A long MIDI message (rare, user-initiated)
// crosses as a raw OWNING pointer the audio thread deletes, the accepted non-RT-on-rare-op pattern;
// StageMidi is inline for a channel message (fits in 4 bytes) and owns an `ext` vector otherwise. A
// capability write crosses as a pointer to a job record the control thread keeps and frees. Structure
// never comes this way: TS sends it as graph documents.
struct DspCommand {
    enum class Kind : std::uint8_t {
        None = 0, StageMidi = 3,
        SetBpm = 7, SetTransport = 8, PressButton = 10,
        SetPpq = 12, CapabilityWrite = 14,
        StageNodeMidi = 17,
        // The NES tap arming every structural change does (docs/modular-graph-phase-b1-design.md section
        // 3.2): restore whatever is armed, then arm `id` to `mode` if `arm`. Only ever the trailer of a
        // setGraph's swap, so it lands with the plan it was made for.
        SyncTap = 16,
    };

    Kind kind = Kind::None;
    union {
        // `len` bytes inline when the message fits; else `ext` owns the whole message and `len` is 0.
        // `frame` places it in the next block.
        struct { std::uint8_t data[4]; std::uint8_t len; std::uint32_t frame; std::vector<std::uint8_t>* ext; } stageMidi;
        struct { double value; } setBpm;                          // transport as a queued op
        struct { bool value; } setTransport;
        struct { std::uint32_t id; std::uint8_t button; bool down; } pressButton; // joypad transition → core
        struct { double value; } setPpq;                         // host playhead jump (locate)
        // A capability job's write into a core's memory (host/engine/CapabilityJobs.hpp). NOT an owning payload: the
        // record and its bytes are the control thread's, which frees them once the block thread has stored the job's
        // terminal state, so applying one allocates and frees nothing.
        struct { rp::host::Job* job; } capabilityWrite;
        // MIDI aimed at ONE NODE of the document, by id, handed to it through rp.stage/1 (a system node's core ingress,
        // a device node's device). The id is the document's, so it keeps its meaning across a plan that replaces the
        // node under it, and it does not fit the ring's slot: `ext` OWNS the id and the bytes, and the audio thread
        // deletes it once the running plan answered.
        struct { rp::host::StagedNodeMessage* ext; } stageNodeMidi;
        struct { bool arm; std::uint8_t mode; std::uint32_t id; } syncTap;  // what it arms after the restore
    };

    DspCommand() : kind(Kind::None), stageMidi{{0, 0, 0, 0}, 0, 0, nullptr} {}
};

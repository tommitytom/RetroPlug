#include "host/graph/HostIoNodes.hpp"

#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "graph/node.hpp"
#include "host/graph/HostServices.hpp"

namespace rp::host {

// Named, so its JSON Schema $defs key is the same under every compiler.
struct MidiOutSettings {
    // The system each input's messages go out as, an input per entry: in1 takes the first's. Empty (the default):
    // no inputs.
    std::vector<std::uint32_t> systems;
};

namespace {

// Every node here needs the engine's host I/O, so none activates without it.
struct HostIoNode {
    const EngineHostIo* io = nullptr;
    void bind_host(const rp_host* h) noexcept {
        io = static_cast<const EngineHostIo*>(h->get_interface(h, RP_ENGINE_HOST_IO_INTERFACE));
    }
    bool activate(double, std::uint32_t) { return io != nullptr; }
};

struct MidiIn : HostIoNode {
    static constexpr const char* id = "rp.host.midi-in";
    static constexpr const char* name = "MIDI In";
    static constexpr const char* category = "host";
    struct {
        rp::events_out::sized<rp::capacity{.messages = kHostMidiInMessages, .bytes = kHostMidiInBytes}> midi;
    } outputs;
    void process(const rp::Block&) noexcept { io->takeMidi(io->ctx, outputs.midi.queue); }
};

rp::graph::PortDesc eventsIn(std::string id) {
    rp::graph::PortDesc p;
    p.id = std::move(id);
    p.dir = rp::graph::PortDir::In;
    p.kind = rp::graph::PortKind::Events;
    return p;
}

// An input per entry of its settings' `systems` (docs/modular-graph-phase-b3b-design.md section 5), in1 to inN, each
// holding one producer's MIDI as it is, which goes out as that entry's system. The inputs are merged by frame
// (docs/lsdj-serial-out-design.md section 5), the k-way merge rp.core.merge makes (CoreNodes.cpp) but into the host's
// MIDI out: each step hands out the head with the earliest frame, the lower input on a tie, so a producer sending at
// real frames, as LSDj's serial out does, keeps its messages' times rather than bunching behind an earlier input's, and
// with every frame 0 the merge is the inputs' concatenation, input by input, each in queue order. An rp_queue is
// frame-ordered by contract (rp_node.h), which rp::graph::reserveMessage keeps for every node of RetroPlug's (each
// writes its queues through it), so an input arrives frame-sorted. A message still goes out at the later of its frame
// and the last handed out in the block, and at the block's last frame at most: a guard against a node that breaks the
// contract (one on the C ABI writes its own queue) and against a frame at or past the block's end, which nothing
// refuses (a script node's send carries one as given, and wraps a negative one through ToUint32). It keeps
// Engine::midiOut frame-sorted and inside the block, as the plugin relies on to write a DAW's events in order and in
// place (DPF hands each frame to CLAP, VST3 and LV2 unchecked). The merge allocates nothing (a head index per input,
// reset each block) and costs messages times inputs; the sink it hands each message to does allocate, io->midiOut
// appending a std::vector per message to Engine::midiOut (HostServices.hpp). Two entries may name one system (a
// controller sending to a real Game Boy under the id of a system whose own MI.OUT also goes out). No entries, no
// inputs: a document whose roles send no host MIDI places it all the same, and it sends nothing.
// The inputs are appended here, none reflected, since their count is the settings'.
struct MidiOut : HostIoNode {
    static constexpr const char* id = "rp.host.midi-out";
    static constexpr const char* name = "MIDI Out";
    static constexpr const char* category = "host";
    using Settings = MidiOutSettings;
    // A change of systems that keeps the inputs is a swap and one that moves them a rebuild. It holds nothing a
    // rebuild loses, so its prepare reads only what create wrote, never what a commit writes, and setGraph need not
    // wait for a swap to be adopted.
    static constexpr bool stateless_prepare = true;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.systems.size() > kMidiOutMaxInputs)
            return "systems names each input's system: at most " + std::to_string(kMidiOutMaxInputs) + " inputs";
        return std::nullopt;
    }

    static void adjust_layout(rp::graph::Layout& l, const Settings& s) {
        for (std::size_t k = 1; k <= s.systems.size() && k <= kMidiOutMaxInputs; ++k)
            l.ports.push_back(eventsIn("in" + std::to_string(k)));
    }

    // Copied at each commit, which must not allocate.
    std::array<std::uint32_t, kMidiOutMaxInputs> systems{};
    std::uint32_t                                 inputCount = 0;
    void on_settings(const Settings& s) noexcept {
        inputCount = 0;
        for (std::size_t k = 0; k < s.systems.size() && k < kMidiOutMaxInputs; ++k)
            systems[inputCount++] = s.systems[k];
    }

    // Each input's next message this block, an index into its queue; only process reads or writes them.
    std::array<std::uint32_t, kMidiOutMaxInputs> heads{};

    void process(const rp::Block& b) noexcept {
        const std::uint32_t inputs = inputCount < b.raw.input_count ? inputCount : b.raw.input_count;
        for (std::uint32_t k = 0; k < inputs; ++k) heads[k] = 0;
        const std::uint32_t end = b.frames() - 1;  // the block's last frame: a block that runs has one
        std::uint32_t       last = 0;               // the latest frame handed out this block
        for (;;) {
            std::uint32_t next = inputs;  // the input whose head goes out next: none yet
            std::uint32_t frame = 0;
            for (std::uint32_t k = 0; k < inputs; ++k) {
                const rp_queue& q = *b.input(k).u.queue;
                if (heads[k] >= q.count) continue;
                if (next == inputs || q.items[heads[k]].frame < frame) {  // strictly earlier: a tie keeps the lower
                    next = k;
                    frame = q.items[heads[k]].frame;
                }
            }
            if (next == inputs) return;
            const rp_queue&   q = *b.input(next).u.queue;
            const rp_message& m = q.items[heads[next]++];
            const auto        data = rp::graph::messageData(q, m);
            if (m.frame > last) last = m.frame < end ? m.frame : end;
            io->midiOut(io->ctx, systems[next], last, data.data(), data.size());
        }
    }
};

}  // namespace

const rp_package* hostIoPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<MidiIn>(), rp::node_type<MidiOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.host.io","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::host

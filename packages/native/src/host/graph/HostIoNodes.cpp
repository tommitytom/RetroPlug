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

// Named, so their JSON Schema $defs keys are the same under every compiler.
struct MidiOutSettings {
    // The system each input's messages go out as, an input per entry: in1 takes the first's. Empty (the default):
    // no inputs.
    std::vector<std::uint32_t> systems;
};

struct CartMirrorSettings {
    // The system the cart mirrors (a system id, as a system node's {"core": <id>}).
    std::uint64_t system = 0;
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
// holding one producer's MIDI as it is, which goes out as that entry's system: input by input, each in queue order,
// never merged by frame, so the order a document wires its producers in is the host's order across them. Two entries
// may name one system (a controller sending to a real Game Boy under the id of a system whose own MI.OUT also goes
// out). No entries, no inputs: a document whose roles send no host MIDI places it all the same, and it sends nothing.
// The inputs are appended here, none reflected, since their count is the settings'.
struct MidiOut : HostIoNode {
    static constexpr const char* id = "rp.host.midi-out";
    static constexpr const char* name = "MIDI Out";
    static constexpr const char* category = "host";
    using Settings = MidiOutSettings;
    // A change of systems that keeps the inputs is a swap and one that moves them a rebuild (it holds nothing a
    // rebuild loses), so its prepare reads only what create wrote, never what a commit writes, and setGraph need not
    // wait for a swap to be adopted.
    static constexpr bool stateless_prepare = true;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.systems.size() > kMidiOutMaxSystems)
            return "systems names each input's system: at most " + std::to_string(kMidiOutMaxSystems) + " inputs";
        return std::nullopt;
    }

    static void adjust_layout(rp::graph::Layout& l, const Settings& s) {
        for (std::size_t k = 1; k <= s.systems.size() && k <= kMidiOutMaxSystems; ++k)
            l.ports.push_back(eventsIn("in" + std::to_string(k)));
    }

    // Copied at each commit, which must not allocate.
    std::array<std::uint32_t, kMidiOutMaxSystems> systems{};
    std::uint32_t                                 inputCount = 0;
    void on_settings(const Settings& s) noexcept {
        inputCount = 0;
        for (std::size_t k = 0; k < s.systems.size() && k < kMidiOutMaxSystems; ++k)
            systems[inputCount++] = s.systems[k];
    }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t k = 0; k < inputCount && k < b.raw.input_count; ++k) {
            const rp_queue& q = *b.input(k).u.queue;
            for (std::uint32_t i = 0; i < q.count; ++i) {
                const rp_message& m = q.items[i];
                const auto data = rp::graph::messageData(q, m);
                io->midiOut(io->ctx, systems[k], m.frame, data.data(), data.size());
            }
        }
    }
};

struct ControllerIn : HostIoNode {
    static constexpr const char* id = "rp.host.controller-in";
    static constexpr const char* name = "Control Surface In";
    static constexpr const char* category = "host";
    struct {
        rp::events_out::sized<rp::capacity{.messages = kControllerInMessages, .bytes = kControllerInBytes}> midi;
        rp::control_out connected;
    } outputs;
    // `connected` holds 1 or 0 from frame 0: a change at frame 0 on the block the flag flips, since a stream's start
    // value is the last block's end.
    void process(const rp::Block&) noexcept {
        io->takeControllerMidi(io->ctx, outputs.midi.queue);
        const double connected = io->controllerConnected(io->ctx) ? 1.0 : 0.0;
        if (connected != outputs.connected.queue->start) outputs.connected.push(0, connected);
    }
};

struct ControllerOut : HostIoNode {
    static constexpr const char* id = "rp.host.controller-out";
    static constexpr const char* name = "Control Surface Out";
    static constexpr const char* category = "host";
    struct { rp::events_in midi; } inputs;
    void process(const rp::Block&) noexcept {
        for (const rp_message& m : inputs.midi.messages()) {
            const auto data = inputs.midi.data(m);
            io->controllerOut(io->ctx, data.data(), data.size());
        }
    }
};

// What the cart mirror's system's FIFO is fed, in the order the kernel stage the kernel node replaced fed it: its MIDI,
// then its bytes, each in queue order. An empty message is skipped, as that stage skipped it.
struct CartMirror : HostIoNode {
    static constexpr const char* id = "rp.n8.cart-mirror";
    static constexpr const char* name = "N8 Cart Mirror";
    static constexpr const char* category = "host";
    using Settings = CartMirrorSettings;
    // Its settings change nothing it does (the engine reads them off the document), so a change is a swap, and its
    // prepare reads nothing a commit writes.
    static constexpr bool stateless_prepare = true;
    struct {
        rp::events_in midi;
        rp::bytes_in  bytes;
    } inputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.system == 0 || s.system > UINT32_MAX) return "system names the system the cart mirrors: 1 to 4294967295";
        return std::nullopt;
    }

    void process(const rp::Block&) noexcept {
        for (const rp_message& m : inputs.midi.messages()) {
            const auto data = inputs.midi.data(m);
            if (!data.empty()) io->cartBytes(io->ctx, m.frame, data.data(), data.size(), false);
        }
        for (const rp_message& m : inputs.bytes.messages()) {
            const auto data = inputs.bytes.data(m);
            if (!data.empty()) io->cartBytes(io->ctx, m.frame, data.data(), data.size(), (m.flags & RP_MESSAGE_FLUSH) != 0);
        }
    }
};

}  // namespace

const rp_package* hostIoPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<MidiIn>(), rp::node_type<MidiOut>(), rp::node_type<ControllerIn>(),
                                                 rp::node_type<ControllerOut>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.host.io","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

const rp_package* n8Package() {
    static const rp_node_type* const kTypes[] = {rp::node_type<CartMirror>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.n8","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

std::optional<std::uint32_t> cartMirrorSystem(std::string_view settings) {
    const rp_settings s{settings.data(), settings.size(), RP_SETTINGS_JSON};
    const auto parsed = rp::detail::parseSettings<CartMirror>(&s);
    if (!parsed) return std::nullopt;
    return static_cast<std::uint32_t>(parsed->system);  // validate kept it to 1..UINT32_MAX
}

}  // namespace rp::host

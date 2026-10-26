#include "host/graph/HostIoNodes.hpp"

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "graph/node.hpp"
#include "host/graph/HostServices.hpp"

namespace rp::host {

// Named, so its JSON Schema $defs key is the same under every compiler.
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

// The emitting system's id, little-endian, ahead of each message's MIDI (design section 4.4): private to the
// kernel's one midiOut port and this node, until each protocol node has a port of its own.
constexpr std::size_t kSystemPrefix = 4;

struct MidiOut : HostIoNode {
    static constexpr const char* id = "rp.host.midi-out";
    static constexpr const char* name = "MIDI Out";
    static constexpr const char* category = "host";
    struct { rp::events_in midi; } inputs;
    void process(const rp::Block&) noexcept {
        for (const rp_message& m : inputs.midi.messages()) {
            const auto data = inputs.midi.data(m);
            if (data.size() < kSystemPrefix) {  // names no system: nowhere to go
                io->dropped(io->ctx);
                continue;
            }
            const std::uint32_t system = std::uint32_t{data[0]} | std::uint32_t{data[1]} << 8 |
                                         std::uint32_t{data[2]} << 16 | std::uint32_t{data[3]} << 24;
            io->midiOut(io->ctx, system, m.frame, data.data() + kSystemPrefix, data.size() - kSystemPrefix);
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

// What the cart mirror's system's FIFO is fed, in the order the kernel stage feeds it today: its MIDI, then its
// bytes, each in queue order. An empty message is skipped, as the kernel stage skips it.
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

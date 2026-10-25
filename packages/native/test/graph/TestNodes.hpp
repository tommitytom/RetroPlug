#pragma once

// Test-only node types for the graph-core tests: sources and sinks to observe a graph, a coupled pair,
// a node that faults, a node that reshapes, and nodes that exist only to trip planner checks. Tests reach a
// node's state with rp::peer_as<T>(host.node("id")) while quiescent.

#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "graph/node.hpp"

namespace rp::graph::test {

// A constant on every channel (value * gain), plus one note-on per block at frame 0. Counts the frames it
// has produced, which is the state the swap tests watch.
struct SourceSettings {
    float         value = 1.0f;
    std::uint32_t channels = 2;
};

struct Source {
    static constexpr const char* id = "rp.test.source";
    using Settings = SourceSettings;
    struct {
        rp::audio_out<rp::stereo> out;
        rp::events_out            events;
    } outputs;
    struct { rp::float_param<"Gain", rp::range{0, 10, 1}> gain; } params;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.channels < 1 || s.channels > 8) return "channels must be 1 to 8";
        return std::nullopt;
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, layoutForCount(s.channels));
    }

    float         value = 1.0f;
    std::uint64_t frames = 0;
    void on_settings(const Settings& s) noexcept { value = s.value; }

    void process(const rp::Block& b) noexcept {
        const float v = value * static_cast<float>(params.gain.start());
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = v;
        const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(frames & 0x7f), 100};
        outputs.events.push(0, note);
        frames += b.frames();
    }
};

// Records what reaches it, in fixed arrays (no allocation, so it is safe in the no-heap test too).
struct Sink {
    static constexpr const char* id = "rp.test.sink";
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;

    std::array<float, 8>         first{};       // sample 0 of each channel, last block
    std::array<float, 8>         last{};        // final sample of each channel, last block
    std::uint32_t                channels = 0;
    std::uint32_t                blocks = 0;
    std::uint32_t                eventCount = 0;  // last block
    std::array<std::uint32_t, 16> eventFrames{};
    std::array<std::uint8_t, 16> eventBytes{};  // second byte of each message
    double                       controlStart = 0.0;
    std::uint32_t                controlChanges = 0;
    double                       controlEnd = 0.0;

    void process(const rp::Block& b) noexcept {
        channels = inputs.in.channel_count;
        for (std::uint32_t c = 0; c < channels && c < first.size(); ++c) {
            first[c] = inputs.in[c][0];
            last[c] = inputs.in[c][b.frames() - 1];
        }
        eventCount = 0;
        for (const auto& m : inputs.events.messages()) {
            if (eventCount < eventFrames.size()) {
                eventFrames[eventCount] = m.frame;
                eventBytes[eventCount] = m.size > 1 ? inputs.events.data(m)[1] : 0;
            }
            ++eventCount;
        }
        controlStart = inputs.control.start();
        controlChanges = static_cast<std::uint32_t>(inputs.control.changes().size());
        controlEnd = graph::controlEnd(*inputs.control.queue);
        ++blocks;
    }
};

// One of a coupled pair. Each step() advances by one frame; the shared log records the interleaving. The
// log allocates, which is fine for a test node and nothing else: a step that throws under noexcept
// terminates the process.
inline std::vector<std::string>& linkLog() {
    static std::vector<std::string> log;
    return log;
}

struct LinkedSettings {
    std::string name = "a";
};

struct Linked {
    static constexpr const char* id = "rp.test.linked";
    using Settings = LinkedSettings;
    struct { rp::link link; } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;

    const Settings* settings = nullptr;
    std::uint32_t   produced = 0;
    std::uint32_t   peerCount = 0;
    void on_settings(const Settings& s) noexcept { settings = &s; }
    void set_peers(std::span<rp_node* const> peers) noexcept { peerCount = static_cast<std::uint32_t>(peers.size()); }

    void prepare(const rp::Block&) noexcept { produced = 0; }
    bool step(std::uint32_t target) noexcept {
        if (produced < target) {
            ++produced;
            linkLog().push_back(settings->name);
        }
        return produced >= target;
    }
    void finish(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = static_cast<float>(peerCount);
    }
    void process(const rp::Block&) noexcept {}
};

// Links but has no rp.step/1, which the planner must refuse.
struct NoStep {
    static constexpr const char* id = "rp.test.nostep";
    struct { rp::link link; } inputs;
    void process(const rp::Block&) noexcept {}
};

// Outputs 1.0 until it faults on block `failAt`.
struct FaultySettings {
    std::uint32_t failAt = 2;
};

struct Faulty {
    static constexpr const char* id = "rp.test.faulty";
    using Settings = FaultySettings;
    struct { rp::audio_out<rp::mono> out; } outputs;
    std::uint32_t failAt = 2, blocks = 0;
    void on_settings(const Settings& s) noexcept { failAt = s.failAt; }
    rp_status process(const rp::Block& b) noexcept {
        if (++blocks >= failAt) return RP_FAULT;
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 1.0f;
        return RP_OK;
    }
};

// Declares a heap, so the Pico target must reject it.
struct Heavy {
    static constexpr const char* id = "rp.test.heavy";
    static constexpr bool heap = true;
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block&) noexcept {}
};

// Copies stereo in to out and records whether the planner ever handed it aliased buffers.
struct Through {
    static constexpr const char* id = "rp.test.through";
    struct { rp::audio_in<rp::stereo> in; } inputs;
    struct { rp::audio_out<rp::stereo> out; } outputs;
    bool aliased = false;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < 2; ++c) {
            if (outputs.out[c] == inputs.in[c]) aliased = true;
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = inputs.in[c][i] + 1.0f;
        }
    }
};

// Ports and params named by its settings, so a settings change can alter its layout, and it keeps its
// instance through one (it reshapes); its prepare reads nothing but the new settings (stateless). Each block
// it pushes [0x90, the port's first letter, blocks] on every events output, records each input's message
// count and each knob's value by the port's first letter, and on its first block sets every level to 5.
// `blocks` is the state a rebuild would lose. Allocates nothing on the audio thread.
struct ShapeSettings {
    std::vector<std::string> ins;     // events inputs
    std::vector<std::string> outs;    // events outputs
    std::vector<std::string> levels;  // control outputs
    std::vector<std::string> knobs;   // float params, 0 to 100
    std::string              tag = "a";
};

struct Shape {
    static constexpr const char* id = "rp.test.shape";
    static constexpr bool reshapes = true;
    static constexpr bool stateless_prepare = true;
    using Settings = ShapeSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.tag.empty()) return "tag must not be empty";
        return std::nullopt;
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        const auto port = [&](const std::string& id, PortDir dir, PortKind kind) {
            PortDesc p;
            p.id = id;
            p.dir = dir;
            p.kind = kind;
            l.ports.push_back(p);
        };
        for (const auto& id : s.ins) port(id, PortDir::In, PortKind::Events);
        for (const auto& id : s.outs) port(id, PortDir::Out, PortKind::Events);
        for (const auto& id : s.levels) port(id, PortDir::Out, PortKind::Control);
        for (const auto& id : s.knobs) l.params.push_back({id, id, ParamType::Float, 0.0, 100.0, 0.0, {}, {}});
    }
    static std::uint8_t letter(const std::string& id) { return static_cast<std::uint8_t>(id[0] & 0x7f); }

    const Settings*                settings = nullptr;
    std::uint32_t                  blocks = 0;
    char                           ranWith = 0;     // the tag the last block ran with
    std::uint32_t                  ranOutputs = 0;  // and how many outputs it was handed
    std::array<std::uint32_t, 128> received{};      // the last block's messages per input, by first letter
    std::array<double, 128>        knobs{};         // the last block's value per knob, by first letter
    std::array<char, 16>           committed{};     // each settings' tag, in the order on_settings saw them
    std::uint32_t                  commits = 0;
    std::array<std::uint8_t, 16>   mask{};
    std::uint32_t                  maskSize = 0;
    std::uint32_t                  maskCalls = 0;
    char                           maskedWith = 0;  // the tag in force when the last mask arrived

    void on_settings(const Settings& s) noexcept {
        settings = &s;
        if (commits < committed.size()) committed[commits] = s.tag[0];
        ++commits;
    }
    void ports_changed(std::span<const std::uint8_t> connected) noexcept {
        maskSize = static_cast<std::uint32_t>(connected.size());
        for (std::size_t i = 0; i < connected.size() && i < mask.size(); ++i) mask[i] = connected[i];
        ++maskCalls;
        maskedWith = settings->tag[0];
    }
    void process(const rp::Block& b) noexcept {
        const Settings& s = *settings;
        ranWith = s.tag[0];
        ranOutputs = b.raw.output_count;
        for (std::uint32_t i = 0; i < s.ins.size(); ++i) received[letter(s.ins[i])] = b.input(i).u.queue->count;
        for (std::uint32_t o = 0; o < s.outs.size(); ++o) {
            const std::uint8_t m[3] = {0x90, letter(s.outs[o]), static_cast<std::uint8_t>(blocks & 0x7f)};
            graph::pushMessage(*b.output(o).u.queue, 0, m);
        }
        if (blocks == 0)
            for (std::uint32_t c = 0; c < s.levels.size(); ++c)
                graph::pushControl(*b.output(static_cast<std::uint32_t>(s.outs.size()) + c).u.control, 0, 5.0);
        for (std::uint32_t k = 0; k < s.knobs.size(); ++k) knobs[letter(s.knobs[k])] = b.param(k).start;
        ++blocks;
    }
};

inline const rp_package* testPackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Source>(), rp::node_type<Sink>(),  rp::node_type<Linked>(),  rp::node_type<NoStep>(),
        rp::node_type<Faulty>(), rp::node_type<Heavy>(), rp::node_type<Through>(), rp::node_type<Shape>(),
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph::test

#include "graph/nodes/CoreNodes.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <vector>

#include <rfl/Literal.hpp>

#include "graph/node.hpp"

namespace rp::graph {

// A named namespace, not an anonymous one: a Settings struct's JSON Schema $defs key is its qualified
// name, which compilers spell differently for an anonymous namespace.
namespace core {

constexpr std::uint32_t kMaxPorts = 16;

PortDesc audioPort(std::string id, PortDir dir, ChannelSpec spec) {
    PortDesc p;
    p.id = std::move(id);
    p.dir = dir;
    p.kind = PortKind::Audio;
    p.channels = std::move(spec);
    return p;
}

PortDesc queuePort(std::string id, PortDir dir, PortKind kind) {
    PortDesc p;
    p.id = std::move(id);
    p.dir = dir;
    p.kind = kind;
    return p;
}

std::optional<std::string> checkCount(std::uint32_t n, std::string_view what) {
    if (n < 1 || n > kMaxPorts)
        return std::string(what) + " must be between 1 and " + std::to_string(kMaxPorts);
    return std::nullopt;
}

std::uint32_t sumOf(const std::vector<std::uint32_t>& v) {
    std::uint32_t n = 0;
    for (auto c : v) n += c;
    return n;
}

std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels) {
    const auto n = layoutChannels(layout);
    if (!n) return "unknown layout \"" + std::string(layout) + "\"";
    if (*n != channels)
        return "layout \"" + std::string(layout) + "\" is " + std::to_string(*n) + " channels, not " + std::to_string(channels);
    return std::nullopt;
}

// ---- rp.core.add ------------------------------------------------------------------------------------------

using AddKind = rfl::Literal<"audio", "control">;

struct AddSettings {
    std::uint32_t inputs = 2;
    AddKind       kind = AddKind::make<"audio">();
};

struct Add {
    static constexpr const char* id = "rp.core.add";
    static constexpr const char* name = "+";
    static constexpr const char* category = "core/combine";
    using Settings = AddSettings;

    static std::optional<std::string> validate(const Settings& s) { return checkCount(s.inputs, "inputs"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        const bool control = s.kind.name() == "control";
        for (std::uint32_t k = 1; k <= s.inputs; ++k) {
            const std::string portId = "in" + std::to_string(k);
            if (control) l.ports.push_back(queuePort(portId, PortDir::In, PortKind::Control));
            else l.ports.push_back(audioPort(portId, PortDir::In, k == 1 ? ChannelSpec::any() : ChannelSpec::matching("in1")));
        }
        if (control) l.ports.push_back(queuePort("out", PortDir::Out, PortKind::Control));
        else l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::matching("in1")));
    }

    bool          control = false;
    std::uint32_t inputCount = 2;

    void on_settings(const Settings& s) noexcept {
        control = s.kind.name() == "control";
        inputCount = s.inputs;
    }

    void process(const rp::Block& b) noexcept {
        if (control) processControl(b);
        else processAudio(b);
    }

    // out = in1 + in2 + ... in input order: exactly the sum today's routers produce into a zeroed bus.
    void processAudio(const rp::Block& b) noexcept {
        const rp_audio_view& out = b.output(0).u.audio;
        for (std::uint32_t c = 0; c < out.channel_count; ++c) {
            float* dst = out.channels[c];
            std::memcpy(dst, b.input(0).u.audio.channels[c], b.frames() * sizeof(float));
            for (std::uint32_t k = 1; k < inputCount; ++k) {
                const float* src = b.input(k).u.audio.channels[c];
                for (std::uint32_t i = 0; i < b.frames(); ++i) dst[i] += src[i];
            }
        }
    }

    // Walk every input's changes in frame order, emitting the running sum at each change frame.
    void processControl(const rp::Block& b) noexcept {
        rp_control_queue& out = *b.output(0).u.control;
        std::array<double, kMaxPorts> value{};
        std::array<std::uint32_t, kMaxPorts> next{};
        double sum = 0.0;
        for (std::uint32_t k = 0; k < inputCount; ++k) {
            value[k] = b.input(k).u.control->start;
            sum += value[k];
        }
        if (sum != out.start) pushControl(out, 0, sum);
        for (;;) {
            std::uint32_t frame = UINT32_MAX;
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const rp_control_queue& q = *b.input(k).u.control;
                if (next[k] < q.count) frame = std::min(frame, q.changes[next[k]].frame);
            }
            if (frame == UINT32_MAX) break;
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const rp_control_queue& q = *b.input(k).u.control;
                while (next[k] < q.count && q.changes[next[k]].frame == frame) value[k] = q.changes[next[k]++].value;
            }
            sum = 0.0;
            for (std::uint32_t k = 0; k < inputCount; ++k) sum += value[k];
            pushControl(out, frame, sum);
        }
    }
};

// ---- rp.core.merge -------------------------------------------------------------------------------------------

struct MergeSettings {
    std::uint32_t inputs = 2;
};

struct Merge {
    static constexpr const char* id = "rp.core.merge";
    static constexpr const char* name = "Merge";
    static constexpr const char* category = "core/combine";
    using Settings = MergeSettings;

    static std::optional<std::string> validate(const Settings& s) { return checkCount(s.inputs, "inputs"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        for (std::uint32_t k = 1; k <= s.inputs; ++k)
            l.ports.push_back(queuePort("in" + std::to_string(k), PortDir::In, PortKind::Events));
        l.ports.push_back(queuePort("out", PortDir::Out, PortKind::Events));
    }

    std::uint32_t inputCount = 2;
    void on_settings(const Settings& s) noexcept { inputCount = s.inputs; }

    // A k-way merge by frame; equal frames keep input order, so in1's message lands first.
    void process(const rp::Block& b) noexcept {
        rp_queue& out = *b.output(0).u.queue;
        std::array<std::uint32_t, kMaxPorts> next{};
        for (;;) {
            std::uint32_t pick = kMaxPorts;
            std::uint32_t frame = UINT32_MAX;
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const rp_queue& q = *b.input(k).u.queue;
                if (next[k] < q.count && q.items[next[k]].frame < frame) {
                    frame = q.items[next[k]].frame;
                    pick = k;
                }
            }
            if (pick == kMaxPorts) break;
            const rp_queue& q = *b.input(pick).u.queue;
            const rp_message& m = q.items[next[pick]++];
            pushMessage(out, m.frame, messageData(q, m), m.flags);
        }
    }
};

// ---- rp.core.join / rp.core.split ------------------------------------------------------------------------

struct JoinSettings {
    std::vector<std::uint32_t> inputs = {1, 1};
    std::string                layout = "stereo";
};

struct Join {
    static constexpr const char* id = "rp.core.join";
    static constexpr const char* name = "Join";
    static constexpr const char* category = "core/channels";
    using Settings = JoinSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkCount(static_cast<std::uint32_t>(s.inputs.size()), "inputs")) return e;
        for (auto c : s.inputs)
            if (c < 1) return std::string("every input needs at least one channel");
        return checkLayout(s.layout, sumOf(s.inputs));
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        for (std::size_t k = 0; k < s.inputs.size(); ++k)
            l.ports.push_back(audioPort("in" + std::to_string(k + 1), PortDir::In,
                                        ChannelSpec::fixed(s.inputs[k], layoutForCount(s.inputs[k]))));
        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(sumOf(s.inputs), s.layout)));
    }

    void process(const rp::Block& b) noexcept {
        const rp_audio_view& out = b.output(0).u.audio;
        std::uint32_t dst = 0;
        for (std::uint32_t k = 0; k < b.raw.input_count; ++k) {
            const rp_audio_view& in = b.input(k).u.audio;
            for (std::uint32_t c = 0; c < in.channel_count && dst < out.channel_count; ++c, ++dst)
                std::memcpy(out.channels[dst], in.channels[c], b.frames() * sizeof(float));
        }
    }
};

struct SplitSettings {
    std::vector<std::uint32_t> outputs = {1, 1};
    std::string                layout = "stereo";
};

struct Split {
    static constexpr const char* id = "rp.core.split";
    static constexpr const char* name = "Split";
    static constexpr const char* category = "core/channels";
    using Settings = SplitSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkCount(static_cast<std::uint32_t>(s.outputs.size()), "outputs")) return e;
        for (auto c : s.outputs)
            if (c < 1) return std::string("every output needs at least one channel");
        return checkLayout(s.layout, sumOf(s.outputs));
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(sumOf(s.outputs), s.layout)));
        for (std::size_t k = 0; k < s.outputs.size(); ++k)
            l.ports.push_back(audioPort("out" + std::to_string(k + 1), PortDir::Out,
                                        ChannelSpec::fixed(s.outputs[k], layoutForCount(s.outputs[k]))));
    }

    void process(const rp::Block& b) noexcept {
        const rp_audio_view& in = b.input(0).u.audio;
        std::uint32_t src = 0;
        for (std::uint32_t k = 0; k < b.raw.output_count; ++k) {
            const rp_audio_view& out = b.output(k).u.audio;
            for (std::uint32_t c = 0; c < out.channel_count && src < in.channel_count; ++c, ++src)
                std::memcpy(out.channels[c], in.channels[src], b.frames() * sizeof(float));
        }
    }
};

// ---- rp.core.downmix / rp.core.relabel ----------------------------------------------------------------------

struct DownmixSettings {
    std::string from = "stereo";
    std::string to = "mono";
};

struct Downmix {
    static constexpr const char* id = "rp.core.downmix";
    static constexpr const char* name = "Downmix";
    static constexpr const char* category = "core/channels";
    using Settings = DownmixSettings;

    static std::optional<std::string> validate(const Settings& s) {
        const auto from = layoutChannels(s.from);
        if (!from) return "unknown layout \"" + s.from + "\"";
        if (s.to == "mono") return std::nullopt;
        if (s.to == "stereo" && s.from == "5.1") return std::nullopt;
        return "downmix supports any layout to mono, and 5.1 to stereo; not " + s.from + " to " + s.to;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        const std::uint32_t from = layoutChannels(s.from).value_or(2);
        const std::uint32_t to = layoutChannels(s.to).value_or(1);
        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(from, s.from)));
        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(to, s.to)));
    }

    bool surround = false;  // 5.1 source: ITU-style coefficients, LFE dropped
    void on_settings(const Settings& s) noexcept { surround = s.from == "5.1"; }

    void process(const rp::Block& b) noexcept {
        const rp_audio_view& in = b.input(0).u.audio;
        const rp_audio_view& out = b.output(0).u.audio;
        const std::uint32_t n = b.frames();
        if (!surround) {
            // Equal sum (no scaling): a stereo -> mono downmix is L + R, as today's 1 Ch / Inst bus is.
            std::memcpy(out.channels[0], in.channels[0], n * sizeof(float));
            for (std::uint32_t c = 1; c < in.channel_count; ++c)
                for (std::uint32_t i = 0; i < n; ++i) out.channels[0][i] += in.channels[c][i];
            return;
        }
        constexpr float k = 0.70710678f;  // order L R C LFE Ls Rs
        const float* const* x = in.channels;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = x[0][i] + k * x[2][i] + k * x[4][i];
            const float r = x[1][i] + k * x[2][i] + k * x[5][i];
            if (out.channel_count == 2) {
                out.channels[0][i] = l;
                out.channels[1][i] = r;
            } else {
                out.channels[0][i] = l + r;
            }
        }
    }
};

struct RelabelSettings {
    std::uint32_t channels = 6;
    std::string   layout = "5.1";
};

struct Relabel {
    static constexpr const char* id = "rp.core.relabel";
    static constexpr const char* name = "Relabel";
    static constexpr const char* category = "core/channels";
    using Settings = RelabelSettings;

    static std::optional<std::string> validate(const Settings& s) { return checkLayout(s.layout, s.channels); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports.push_back(audioPort("in", PortDir::In, ChannelSpec::fixed(s.channels, "any")));
        l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::fixed(s.channels, s.layout)));
    }

    void process(const rp::Block& b) noexcept {
        const rp_audio_view& in = b.input(0).u.audio;
        const rp_audio_view& out = b.output(0).u.audio;
        for (std::uint32_t c = 0; c < out.channel_count; ++c)
            std::memcpy(out.channels[c], in.channels[c], b.frames() * sizeof(float));
    }
};

// ---- rp.core.delay ----------------------------------------------------------------------------------------------

constexpr std::uint32_t kMaxDelaySamples = 1u << 24;  // keeps the ring size and the int Time param in range

struct DelaySettings {
    std::uint32_t maxSamples = 48000;
    std::uint32_t channels = 2;
};

struct Delay {
    static constexpr const char* id = "rp.core.delay";
    static constexpr const char* name = "Delay";
    static constexpr const char* category = "dsp/time";
    using Settings = DelaySettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct { rp::int_param<"Time", rp::range{0, 48000, 0}, "samples"> time; } params;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.channels < 1 || s.channels > kMaxPorts) return std::string("channels must be between 1 and 16");
        if (s.maxSamples < 1 || s.maxSamples > kMaxDelaySamples)
            return "maxSamples must be between 1 and " + std::to_string(kMaxDelaySamples);
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
        l.params[0].max = static_cast<double>(s.maxSamples);
    }

    // One ring of maxSamples + 1 per channel, in host-supplied memory: no heap, so it runs on the Pico.
    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.maxSamples + 1) * s.channels * sizeof(float);
    }

    std::span<float> ring;
    std::uint32_t    length = 1;  // per channel
    std::uint32_t    write = 0;

    void attach(std::span<std::byte> mem) noexcept {
        ring = std::span<float>(reinterpret_cast<float*>(mem.data()), mem.size() / sizeof(float));
    }
    void on_settings(const Settings& s) noexcept { length = s.maxSamples + 1; }

    void process(const rp::Block& b) noexcept {
        const auto time = static_cast<std::uint32_t>(std::clamp(params.time.value(), 0, static_cast<int>(length - 1)));
        std::uint32_t w = write;
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            const std::uint32_t r = (w + length - time) % length;
            for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
                float* line = ring.data() + std::size_t(c) * length;
                line[w] = inputs.in[c][i];
                outputs.out[c][i] = line[r];
            }
            w = (w + 1) % length;
        }
        write = w;
    }
};

// ---- rp.core.constant ---------------------------------------------------------------------------------------------

struct ConstantSettings {
    double min = 0.0;
    double max = 1.0;
};

struct Constant {
    static constexpr const char* id = "rp.core.constant";
    static constexpr const char* name = "Constant";
    static constexpr const char* category = "core/source";
    using Settings = ConstantSettings;

    struct { rp::control_out out; } outputs;
    struct { rp::float_param<"Value", rp::range{0, 1, 0}> value; } params;

    static std::optional<std::string> validate(const Settings& s) {
        if (!(s.min <= s.max)) return std::string("min must not exceed max");
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.params[0].min = s.min;
        l.params[0].max = s.max;
        l.params[0].def = std::clamp(0.0, s.min, s.max);
    }

    // The output follows the param: its value at block start, then each change at its own frame.
    void process(const rp::Block&) noexcept {
        if (outputs.out.queue->start != params.value.start()) outputs.out.push(0, params.value.start());
        for (const auto& c : params.value.changes()) outputs.out.push(c.frame, c.value);
    }
};

// ---- rp.core.buffer-player / rp.core.buffer-recorder ------------------------------------------------------------

struct PlayerSettings {
    std::uint32_t channels = 2;
    std::string   layout = "stereo";
    bool          loop = true;
};

struct BufferPlayer {
    static constexpr const char* id = "rp.core.buffer-player";
    static constexpr const char* name = "Buffer Player";
    static constexpr const char* category = "core/source";
    using Settings = PlayerSettings;

    struct { rp::resource_in<"audio-buffer"> sample; } inputs;
    struct { rp::audio_out<rp::stereo> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) { return checkLayout(s.layout, s.channels); }
    static void adjust_layout(Layout& l, const Settings& s) { l.ports[1].channels = ChannelSpec::fixed(s.channels, s.layout); }

    bool          loop = true;
    std::uint32_t position = 0;
    void on_settings(const Settings& s) noexcept { loop = s.loop; }

    void process(const rp::Block& b) noexcept {
        const rp_resource* r = inputs.sample.get();
        const auto* buf = r ? static_cast<const rp_audio_buffer*>(r->payload) : nullptr;
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            const bool playing = buf && buf->frames > 0 && position < buf->frames;
            for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
                outputs.out[c][i] = (playing && c < buf->channels) ? buf->data[c][position] : 0.0f;
            if (playing && ++position >= buf->frames && loop) position = 0;
        }
    }
};

struct RecorderSettings {
    std::uint32_t channels = 2;
};

struct BufferRecorder {
    static constexpr const char* id = "rp.core.buffer-recorder";
    static constexpr const char* name = "Buffer Recorder";
    static constexpr const char* category = "core/sink";
    using Settings = RecorderSettings;

    struct {
        rp::audio_in<rp::channels<2, "any">>   in;
        rp::resource_write<"audio-buffer">     target;
    } inputs;
    struct { rp::resource_out<"audio-buffer"> buffer; } outputs;
    struct { rp::bool_param<"Record", true> record; } params;

    static std::optional<std::string> validate(const Settings& s) { return checkCount(s.channels, "channels"); }
    static void adjust_layout(Layout& l, const Settings& s) { l.ports[0].channels = ChannelSpec::fixed(s.channels, "any"); }

    std::uint32_t position = 0;

    // Publishes the target's handle on `buffer` every block, so readers wired from it are ordered after
    // this node and see this block's writes.
    void process(const rp::Block& b) noexcept {
        rp_resource* r = inputs.target.get();
        outputs.buffer.set(r);
        if (!r || !params.record.value()) return;
        auto* buf = static_cast<rp_audio_buffer*>(r->payload);
        for (std::uint32_t i = 0; i < b.frames() && position < buf->frames; ++i, ++position)
            for (std::uint32_t c = 0; c < buf->channels && c < inputs.in.channel_count; ++c)
                buf->data[c][position] = inputs.in[c][i];
    }
};

}  // namespace core

const rp_package* corePackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<core::Add>(),     rp::node_type<core::Merge>(),    rp::node_type<core::Join>(),
        rp::node_type<core::Split>(),   rp::node_type<core::Downmix>(),  rp::node_type<core::Relabel>(),
        rp::node_type<core::Delay>(),   rp::node_type<core::Constant>(), rp::node_type<core::BufferPlayer>(),
        rp::node_type<core::BufferRecorder>(),
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.core","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph

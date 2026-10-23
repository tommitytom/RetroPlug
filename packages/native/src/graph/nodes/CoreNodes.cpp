#include "graph/nodes/CoreNodes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <vector>

#include <rfl/Literal.hpp>

#include "graph/node.hpp"

namespace rp::graph {

// The settings structs live in a named namespace, not an anonymous one: a Settings struct's JSON Schema
// $defs key is its qualified name, which compilers spell differently for an anonymous namespace. Only
// they need the name; everything else below stays internal to this file.
namespace core {

using AddKind = rfl::Literal<"audio", "control">;

struct AddSettings {
    std::uint32_t inputs = 2;
    AddKind       kind = AddKind::make<"audio">();
};

struct MergeSettings {
    std::uint32_t inputs = 2;
};

struct JoinSettings {
    std::vector<std::uint32_t> inputs = {1, 1};
    std::string                layout = "stereo";
};

struct SplitSettings {
    std::vector<std::uint32_t> outputs = {1, 1};
    std::string                layout = "stereo";
};

struct DownmixSettings {
    std::string from = "stereo";
    std::string to = "mono";
};

struct RelabelSettings {
    std::uint32_t channels = 6;
    std::string   layout = "5.1";
};

struct DelaySettings {
    std::uint32_t maxSamples = 48000;
    std::uint32_t channels = 2;
};

struct ConstantSettings {
    double min = 0.0;
    double max = 1.0;
};

struct PlayerSettings {
    std::uint32_t channels = 2;
    std::string   layout = "stereo";
    bool          loop = true;
};

struct RecorderSettings {
    std::uint32_t channels = 2;
};

}  // namespace core

namespace {

using namespace core;

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

std::optional<std::string> checkChannels(std::uint64_t n, std::string_view what) {
    if (n < 1 || n > kMaxChannels)
        return std::string(what) + " must be between 1 and " + std::to_string(kMaxChannels) + " channels";
    return std::nullopt;
}

// A list of per-port channel counts: 1..kMaxPorts ports of at least one channel, kMaxChannels in all.
std::optional<std::string> checkChannelList(const std::vector<std::uint32_t>& counts, std::string_view what) {
    const auto ports = static_cast<std::uint32_t>(std::min<std::size_t>(counts.size(), UINT32_MAX));
    if (auto e = checkCount(ports, what)) return e;
    std::uint64_t total = 0;
    for (auto c : counts) {
        if (c < 1) return "every one of the " + std::string(what) + " needs at least one channel";
        total += c;
    }
    return checkChannels(total, std::string(what) + " together");
}

std::uint32_t sumOf(const std::vector<std::uint32_t>& v) {  // validated: at most kMaxChannels
    std::uint32_t n = 0;
    for (auto c : v) n += c;
    return n;
}

std::optional<std::string> checkLayout(std::string_view layout, std::uint32_t channels) {
    const auto n = layoutChannels(layout);
    if (!n) return "unknown layout " + jsonString(layout);
    if (*n != channels)
        return "layout " + jsonString(layout) + " is " + std::to_string(*n) + " channels, not " +
               std::to_string(channels);
    return std::nullopt;
}

// Emit a control value at a frame, skipping one equal to what the stream already holds.
struct ControlWriter {
    rp_control_queue& out;
    double            held;
    explicit ControlWriter(rp_control_queue& q) : out(q), held(q.start) {}
    void operator()(std::uint32_t frame, double value) noexcept {
        if (value == held) return;
        pushControl(out, frame, value);
        held = value;
    }
};

// ---- rp.core.add ------------------------------------------------------------------------------------------

bool isControl(const AddSettings& s) { return s.kind.value() == AddKind::value_of<"control">(); }

struct Add {
    static constexpr const char* id = "rp.core.add";
    static constexpr const char* name = "+";
    static constexpr const char* category = "core/combine";
    using Settings = AddSettings;

    static std::optional<std::string> validate(const Settings& s) { return checkCount(s.inputs, "inputs"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        const bool controlKind = isControl(s);
        for (std::uint32_t k = 1; k <= s.inputs; ++k) {
            const std::string portId = "in" + std::to_string(k);
            if (controlKind) l.ports.push_back(queuePort(portId, PortDir::In, PortKind::Control));
            else l.ports.push_back(audioPort(portId, PortDir::In, k == 1 ? ChannelSpec::any()
                                                                       : ChannelSpec::matching("in1")));
        }
        if (controlKind) l.ports.push_back(queuePort("out", PortDir::Out, PortKind::Control));
        else l.ports.push_back(audioPort("out", PortDir::Out, ChannelSpec::matching("in1")));
    }

    bool          control = false;
    std::uint32_t inputCount = 2;

    void on_settings(const Settings& s) noexcept {
        control = isControl(s);
        inputCount = s.inputs;
    }

    void process(const rp::Block& b) noexcept {
        if (control) processControl(b);
        else processAudio(b);
    }

    // out = 0 + in1 + in2 + ... in input order: exactly the sum today's routers produce into a zeroed bus
    // (starting from zero, not from a copy of in1, which differs when in1 holds -0.0).
    void processAudio(const rp::Block& b) noexcept {
        const rp_audio_view& out = b.output(0).u.audio;
        for (std::uint32_t c = 0; c < out.channel_count; ++c) {
            float* dst = out.channels[c];
            std::memset(dst, 0, b.frames() * sizeof(float));
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const float* src = b.input(k).u.audio.channels[c];
                for (std::uint32_t i = 0; i < b.frames(); ++i) dst[i] += src[i];
            }
        }
    }

    // Walk every input's changes in frame order, emitting the sum at each frame where it changes. Frame 0
    // folds every input's frame-0 changes into the block-start sum, so it is one change, not several.
    void processControl(const rp::Block& b) noexcept {
        ControlWriter emit(*b.output(0).u.control);
        std::array<double, kMaxPorts> value{};
        std::array<std::uint32_t, kMaxPorts> next{};
        for (std::uint32_t k = 0; k < inputCount; ++k) value[k] = b.input(k).u.control->start;
        for (std::uint32_t frame = 0;;) {
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const rp_control_queue& q = *b.input(k).u.control;
                while (next[k] < q.count && q.changes[next[k]].frame <= frame) value[k] = q.changes[next[k]++].value;
            }
            double sum = 0.0;
            for (std::uint32_t k = 0; k < inputCount; ++k) sum += value[k];
            emit(frame, sum);
            std::uint32_t following = UINT32_MAX;
            for (std::uint32_t k = 0; k < inputCount; ++k) {
                const rp_control_queue& q = *b.input(k).u.control;
                if (next[k] < q.count) following = std::min(following, q.changes[next[k]].frame);
            }
            if (following == UINT32_MAX) break;
            frame = following;
        }
    }
};

// ---- rp.core.merge -------------------------------------------------------------------------------------------

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

struct Join {
    static constexpr const char* id = "rp.core.join";
    static constexpr const char* name = "Join";
    static constexpr const char* category = "core/channels";
    using Settings = JoinSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannelList(s.inputs, "inputs")) return e;
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

struct Split {
    static constexpr const char* id = "rp.core.split";
    static constexpr const char* name = "Split";
    static constexpr const char* category = "core/channels";
    using Settings = SplitSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannelList(s.outputs, "outputs")) return e;
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

struct Downmix {
    static constexpr const char* id = "rp.core.downmix";
    static constexpr const char* name = "Downmix";
    static constexpr const char* category = "core/channels";
    using Settings = DownmixSettings;

    static std::optional<std::string> validate(const Settings& s) {
        const auto from = layoutChannels(s.from);
        if (!from) return "unknown layout " + jsonString(s.from);
        if (auto e = checkChannels(*from, "the source layout")) return e;
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

    bool toStereo = false;  // 5.1 -> stereo; every other downmix is to mono
    void on_settings(const Settings& s) noexcept { toStereo = s.to == "stereo"; }

    void process(const rp::Block& b) noexcept {
        const rp_audio_view& in = b.input(0).u.audio;
        const rp_audio_view& out = b.output(0).u.audio;
        const std::uint32_t n = b.frames();
        if (!toStereo) {
            // Anything -> mono is an equal sum, no scaling (so stereo -> mono is L + R, as today's 1 Ch /
            // Inst bus is), from any layout, 5.1 included.
            std::memcpy(out.channels[0], in.channels[0], n * sizeof(float));
            for (std::uint32_t c = 1; c < in.channel_count; ++c)
                for (std::uint32_t i = 0; i < n; ++i) out.channels[0][i] += in.channels[c][i];
            return;
        }
        // 5.1 -> stereo: ITU-style coefficients, LFE dropped. Channel order L R C LFE Ls Rs.
        constexpr float k = 0.70710678f;
        const float* const* x = in.channels;
        for (std::uint32_t i = 0; i < n; ++i) {
            out.channels[0][i] = x[0][i] + k * x[2][i] + k * x[4][i];
            out.channels[1][i] = x[1][i] + k * x[2][i] + k * x[5][i];
        }
    }
};

struct Relabel {
    static constexpr const char* id = "rp.core.relabel";
    static constexpr const char* name = "Relabel";
    static constexpr const char* category = "core/channels";
    using Settings = RelabelSettings;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "relabel")) return e;
        return checkLayout(s.layout, s.channels);
    }

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

constexpr std::uint32_t kMaxDelaySamples = 1u << 24;      // keeps the int Time param in range
constexpr std::uint64_t kMaxDelayRingSamples = 1ull << 26;  // 256 MiB of float across every channel

struct Delay {
    static constexpr const char* id = "rp.core.delay";
    static constexpr const char* name = "Delay";
    static constexpr const char* category = "dsp/time";
    using Settings = DelaySettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct { rp::int_param<"Time", rp::range{0, 48000, 0}, "samples"> time; } params;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a delay")) return e;
        if (s.maxSamples < 1 || s.maxSamples > kMaxDelaySamples)
            return "maxSamples must be between 1 and " + std::to_string(kMaxDelaySamples);
        if ((std::uint64_t(s.maxSamples) + 1) * s.channels > kMaxDelayRingSamples)
            return "maxSamples x channels must not exceed " + std::to_string(kMaxDelayRingSamples);
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

    // Time in samples as the ring can hold it: rounded, 0 for NaN, at most maxSamples.
    std::uint32_t timeOf(double v) const noexcept {
        if (!(v > 0.0)) return 0;
        const double t = std::round(v);
        return t >= double(length - 1) ? length - 1 : static_cast<std::uint32_t>(t);
    }

    // Sample-accurate: a Time change takes effect at its own frame.
    void process(const rp::Block& b) noexcept {
        const auto changes = params.time.changes();
        std::size_t next = 0;
        std::uint32_t time = timeOf(params.time.start());
        std::uint32_t w = write;
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            while (next < changes.size() && changes[next].frame <= i) time = timeOf(changes[next++].value);
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

struct Constant {
    static constexpr const char* id = "rp.core.constant";
    static constexpr const char* name = "Constant";
    static constexpr const char* category = "core/source";
    using Settings = ConstantSettings;

    struct { rp::control_out out; } outputs;
    struct { rp::float_param<"Value", rp::range{0, 1, 0}> value; } params;

    static std::optional<std::string> validate(const Settings& s) {
        if (!std::isfinite(s.min) || !std::isfinite(s.max)) return std::string("min and max must be finite numbers");
        if (!(s.min <= s.max)) return std::string("min must not exceed max");
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.params[0].min = s.min;
        l.params[0].max = s.max;
        l.params[0].def = std::clamp(0.0, s.min, s.max);
    }

    // The output follows the param: its value from frame 0 (the block-start value, or a frame-0 change),
    // then each later change at its own frame, skipping any that would not change the output.
    void process(const rp::Block&) noexcept {
        ControlWriter emit(*outputs.out.queue);
        const auto changes = params.value.changes();
        std::size_t next = 0;
        double first = params.value.start();
        while (next < changes.size() && changes[next].frame == 0) first = changes[next++].value;
        emit(0, first);
        for (; next < changes.size(); ++next) emit(changes[next].frame, changes[next].value);
    }
};

// ---- rp.core.buffer-player / rp.core.buffer-recorder ------------------------------------------------------------

struct BufferPlayer {
    static constexpr const char* id = "rp.core.buffer-player";
    static constexpr const char* name = "Buffer Player";
    static constexpr const char* category = "core/source";
    using Settings = PlayerSettings;

    struct { rp::resource_in<"audio-buffer"> sample; } inputs;
    struct { rp::audio_out<rp::stereo> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a player")) return e;
        return checkLayout(s.layout, s.channels);
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[1].channels = ChannelSpec::fixed(s.channels, s.layout);
    }

    bool               loop = true;
    std::uint32_t      position = 0;
    const rp_resource* playing = nullptr;  // the buffer `position` belongs to
    void on_settings(const Settings& s) noexcept { loop = s.loop; }

    void process(const rp::Block& b) noexcept {
        const rp_resource* r = inputs.sample.get();
        const auto* buf = r ? static_cast<const rp_audio_buffer*>(r->payload) : nullptr;
        if (r != playing) {  // a different buffer starts from its beginning
            playing = r;
            position = 0;
        }
        if (buf && loop && buf->frames > 0 && position >= buf->frames) position = 0;  // looping turned on at the end
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            const bool sounding = buf && position < buf->frames;
            for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
                outputs.out[c][i] = (sounding && c < buf->channels) ? buf->data[c][position] : 0.0f;
            if (sounding && ++position >= buf->frames && loop) position = 0;
        }
    }
};

struct BufferRecorder {
    static constexpr const char* id = "rp.core.buffer-recorder";
    static constexpr const char* name = "Buffer Recorder";
    static constexpr const char* category = "core/sink";
    using Settings = RecorderSettings;

    struct {
        rp::audio_in<rp::channels<2, "any">> in;
        rp::resource_write<"audio-buffer">   target;
    } inputs;
    struct { rp::resource_out<"audio-buffer"> buffer; } outputs;
    struct { rp::bool_param<"Record", true> record; } params;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a recorder"); }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

    std::uint32_t      position = 0;
    bool               recording = false;   // Record as the last frame saw it
    const rp_resource* writing = nullptr;   // the buffer `position` belongs to

    // Publishes the target's handle on `buffer` every block, so readers wired from it are ordered after
    // this node and see this block's writes. Record is frame-accurate; turning it on (or binding a new
    // target) starts from the buffer's first frame, and recording stops at its last.
    void process(const rp::Block& b) noexcept {
        rp_resource* r = inputs.target.get();
        outputs.buffer.set(r);
        if (r != writing) {
            writing = r;
            position = 0;
        }
        auto* buf = r ? static_cast<rp_audio_buffer*>(r->payload) : nullptr;
        const auto changes = params.record.changes();
        std::size_t next = 0;
        auto set = [&](double v) {
            const bool on = v >= 0.5;
            if (on && !recording) position = 0;
            recording = on;
        };
        set(params.record.start());
        for (std::uint32_t i = 0; i < b.frames(); ++i) {
            while (next < changes.size() && changes[next].frame <= i) set(changes[next++].value);
            if (!recording || !buf || position >= buf->frames) continue;
            for (std::uint32_t c = 0; c < buf->channels && c < inputs.in.channel_count; ++c)
                buf->data[c][position] = inputs.in[c][i];
            ++position;
        }
    }
};

}  // namespace

const rp_package* corePackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Add>(),     rp::node_type<Merge>(),    rp::node_type<Join>(),
        rp::node_type<Split>(),   rp::node_type<Downmix>(),  rp::node_type<Relabel>(),
        rp::node_type<Delay>(),   rp::node_type<Constant>(), rp::node_type<BufferPlayer>(),
        rp::node_type<BufferRecorder>(),
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.core","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph

#include "devtest/DevtestNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <string_view>

#include "graph/node.hpp"

namespace rp::devtest {

namespace {

int gLive = 0;

struct SourceSettings {
    std::uint32_t seed = 1;
};

struct Source {
    static constexpr const char* id = "rp.devtest.source";
    using Settings = SourceSettings;
    struct {
        rp::audio_out<rp::stereo> out;
        rp::events_out            events;
        rp::control_out           control;
    } outputs;

    Live          live;
    std::uint32_t state = 1;
    void on_settings(const Settings& s) noexcept { state = s.seed * 2654435761u + 1u; }

    std::uint32_t next() noexcept {  // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float sample() noexcept { return (static_cast<float>(next() >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.8f; }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = sample();
        std::uint32_t frame = 0;
        for (std::uint32_t k = next() % 4; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 8) {
            const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(next() & 0x7f), 100};
            outputs.events.push(frame, note);
        }
        frame = 0;
        for (std::uint32_t k = next() % 3; k > 0 && frame < b.frames(); --k, frame += 1 + next() % 16)
            outputs.control.push(frame, static_cast<double>(sample()) * 3.0);
    }
};

struct Digest {
    static constexpr const char* id = "rp.devtest.digest";
    struct {
        rp::audio_in<rp::any> in;
        rp::events_in         events;
        rp::control_in        control;
    } inputs;

    Live              live;
    rp_devtest_digest view{};

    void mix(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) view.hash = (view.hash ^ p[i]) * 1099511628211ull;
    }
    template <class V>
    void mix(const V& v) noexcept { mix(&v, sizeof v); }

    const void* get_interface(std::string_view want) noexcept {
        return want == RP_DEVTEST_DIGEST_INTERFACE ? &view : nullptr;
    }

    void process(const rp::Block& b) noexcept {
        mix(b.frames());
        mix(inputs.in.channel_count);
        for (std::uint32_t c = 0; c < inputs.in.channel_count; ++c) {
            mix(inputs.in[c], b.frames() * sizeof(float));
            for (std::uint32_t i = 0; i < b.frames(); ++i) view.heard = view.heard || inputs.in[c][i] != 0.0f;
        }
        for (const auto& m : inputs.events.messages()) {
            mix(m.frame);
            mix(m.flags);
            const auto data = inputs.events.data(m);
            mix(data.data(), data.size());
            view.heard = true;
        }
        mix(inputs.control.start());
        for (const auto& c : inputs.control.changes()) {
            mix(c.frame);
            mix(c.value);
            view.heard = true;
        }
        ++view.blocks;
    }
};

struct LockstepSettings {
    float mix = 0.5f;  // how much of its peer's last frame each frame takes
};

struct Lockstep {
    static constexpr const char* id = "rp.devtest.lockstep";
    using Settings = LockstepSettings;
    struct {
        rp::link               link;
        rp::audio_in<rp::mono> in;
    } inputs;
    struct { rp::audio_out<rp::mono> out; } outputs;
    struct { rp::float_param<"Drive", rp::range{0, 2, 1}> drive; } params;

    Live          live;
    float         mixing = 0.5f;
    rp_node*      peer = nullptr;
    const float*  in = nullptr;
    float*        out = nullptr;
    float         drive = 1.0f;
    float         last = 0.0f;
    std::uint32_t done = 0;

    void on_settings(const Settings& s) noexcept { mixing = s.mix; }
    void set_peers(std::span<rp_node* const> peers) noexcept { peer = peers.empty() ? nullptr : peers[0]; }

    void prepare(const rp::Block&) noexcept {
        in = inputs.in[0];
        out = outputs.out[0];
        drive = static_cast<float>(params.drive.latest());
        done = 0;
    }
    // A few frames a step, each taking in what the peer had reached when it ran.
    bool step(std::uint32_t target) noexcept {
        const Lockstep* other = rp::peer_as<Lockstep>(peer);
        for (std::uint32_t k = 0; k < 5 && done < target; ++k, ++done) {
            const float x = in[done] * drive + mixing * (other ? other->last : 0.0f);
            last = x / (1.0f + (x < 0 ? -x : x));
            out[done] = last;
        }
        return done >= target;
    }
    void finish(const rp::Block&) noexcept {}
    void process(const rp::Block&) noexcept {}
};

}  // namespace

int live() noexcept { return gLive; }

Live::Live() noexcept { ++gLive; }
Live::~Live() { --gLive; }

const rp_package* planPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Source>(), rp::node_type<Digest>(),
                                                 rp::node_type<Lockstep>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.plan","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::devtest

extern "C" const rp_package* rp_devtest_plan_package(void) { return rp::devtest::planPackage(); }

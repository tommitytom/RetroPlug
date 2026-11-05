#pragma once

// The node types the exported-plan cases run (docs/modular-graph-phase-e3-design.md section 8), one source built
// twice into retroplug-device-mode-test as EchoNode.hpp's are: in device mode by DeviceModeNodes.cpp, and on the
// desktop by DevicePlan.test.cpp. A plan the loader runs holds device-mode instances alone, so what feeds it and what
// reads it must be portable too, unlike DeviceMode.test.cpp's rig, which the desktop alone builds.
//
// - source: a deterministic stereo signal, messages and control changes, as the rig's noise is.
// - digest: an FNV-1a digest of everything that reaches its inputs, read through get_interface (kDigestInterface).
// - lockstep: a coupled node (rp.step/1 and rp.link/1, no portable rp.core or rp.dsp type couples): each step
//   advances it a few frames, mixing in what its peer last produced, so the round robin's order is in its output.
// - fault: fails at the step its settings name; the device check runs its device build, and the loader's cases
//   export a plan holding it. It fails only as the device builds it: the exporter plans its desktop build, which
//   activates, so a plan that names it exports and the loader is what sees it fail.

#include <cstdint>
#include <span>

#include "DeviceModeEntries.hpp"
#include "graph/node.hpp"

namespace rp::graph::devtest {

// How many of these nodes are alive, in the build that counts them: the loader's unwinding is seen leaving none. Each
// build has its own (the device-mode objects' is rp_devmode's), and rp_devtest_plan_live_device_mode() answers the
// device's.
inline int gLive = 0;
struct Live {
    Live() noexcept { ++gLive; }
    ~Live() { --gLive; }
    Live(const Live&) = delete;
    Live& operator=(const Live&) = delete;
};

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

    Live       live;
    DigestView view{};

    void mix(const void* data, std::size_t n) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < n; ++i) view.hash = (view.hash ^ p[i]) * 1099511628211ull;
    }
    template <class V>
    void mix(const V& v) noexcept { mix(&v, sizeof v); }

    const void* get_interface(std::string_view want) noexcept { return want == kDigestInterface ? &view : nullptr; }

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
        rp::link              link;
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

struct FaultSettings {
    rp::choice<"none", "activate", "process"> at;
    rp::resource_ref<"rom">                   rom;
};

// A create fails on its resource, a "rom" the device check's host does not hold; `at` makes activate refuse or the
// block fault.
struct Fault {
    static constexpr const char* id = "rp.devtest.fault";
    using Settings = FaultSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    Live          live;
    std::uint32_t at = 0;
    void on_settings(const Settings& s) noexcept { at = s.at.value(); }

#ifdef RP_DEVICE
    bool activate(double, std::uint32_t) noexcept { return at != 1; }
#else
    bool activate(double, std::uint32_t) noexcept { return true; }
#endif
    void deactivate() noexcept {}

    rp_status process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
        return at == 2 ? RP_FAULT : RP_OK;
    }
};

}  // namespace rp::graph::devtest

#pragma once

// The test's own node types, one source built twice into retroplug-device-mode-test: in device mode by
// DeviceModeNodes.cpp, and on the desktop by DeviceMode.test.cpp, so the device's build can be held to the desktop's.
//
// - echo: a settings struct with a field of every kind a device-mode Settings may hold, copied into an EchoView
//   (DeviceModeEntries.hpp) for the test to read, so a decode is held to the desktop's JSON reading field for field.
// - pair: two resource fields, so a create whose second pin fails can be seen releasing the first.
// - shaper: an enum settings field and an enum_param, both shaping its audio, so the device's enum reading and its
//   enum_param render what the desktop's do.

#include <cstdint>
#include <cstring>
#include <string_view>

#include "DeviceModeEntries.hpp"
#include "graph/node.hpp"

// rp::graph::devtest, so the device-mode build of it is rp_devmode::graph::devtest (the device-mode objects' rename,
// cmake/tests.cmake) and the two never share a name; a named namespace, as a desktop Settings struct's schema needs.
namespace rp::graph::devtest {

enum class Mode : std::uint8_t { Off, Slow, Fast = 7 };

struct EchoSettings {
    bool                         flag = false;
    std::uint8_t                 u8 = 1;
    std::uint32_t                u32 = 2;
    std::int32_t                 i32 = -3;
    std::int64_t                 i64 = 4;
    float                        f = 0.5f;
    double                       d = 0.25;
    rp::choice<"one", "two", "three"> pick;
    rp::text<8>                  name = "echo";
    rp::list<std::uint16_t, 4>   counts = {1, 2};
    rp::resource_ref<"rom">      rom;
    Mode                         mode = Mode::Slow;
};

struct Echo {
    static constexpr const char* id = "rp.devtest.echo";
    using Settings = EchoSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    EchoView view{};

    void on_settings(const Settings& s) noexcept {
        view.flag = s.flag;
        view.u8 = s.u8;
        view.u32 = s.u32;
        view.i32 = s.i32;
        view.i64 = s.i64;
        view.f = s.f;
        view.d = s.d;
        view.pick = s.pick.value();
        view.nameSize = static_cast<std::uint32_t>(s.name.size());
        std::memcpy(view.name, s.name.data(), s.name.size());
        view.countsSize = static_cast<std::uint32_t>(s.counts.size());
        for (std::size_t i = 0; i < s.counts.size(); ++i) view.counts[i] = s.counts[i];
        view.rom = s.rom.id;
        view.romResolved = s.rom.resource != nullptr;
        view.mode = static_cast<int>(s.mode);
    }

    const void* get_interface(std::string_view want) noexcept { return want == kEchoInterface ? &view : nullptr; }

    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};

struct PairSettings {
    rp::resource_ref<"rom">  rom;
    rp::resource_ref<"sram"> sram;
};

struct Pair {
    static constexpr const char* id = "rp.devtest.pair";
    using Settings = PairSettings;
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};

// The curve is a settings enum (read by its enumerator's name), the shape an enum_param (driven by index).
enum class Curve : std::int8_t { Soft = -1, Hard = 1, Fold = 2 };
enum class Shape { Clip, Square, Invert };

struct ShaperSettings {
    Curve         curve = Curve::Soft;
    std::uint32_t channels = 2;
};

struct Shaper {
    static constexpr const char* id = "rp.devtest.shaper";
    using Settings = ShaperSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct { rp::enum_param<Shape, "Shape", Shape::Square> shape; } params;

#ifndef RP_DEVICE
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    Curve curve = Curve::Soft;
    void on_settings(const Settings& s) noexcept { curve = s.curve; }

    float bend(float x) const noexcept {
        switch (curve) {
            case Curve::Soft: return x / (1.0f + (x < 0 ? -x : x));
            case Curve::Hard: return x > 0.5f ? 0.5f : x < -0.5f ? -0.5f : x;
            case Curve::Fold: return x > 0.5f ? 1.0f - x : x < -0.5f ? -1.0f - x : x;
        }
        return x;
    }

    void process(const rp::Block& b) noexcept {
        const Shape shape = params.shape.value();
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) {
                const float y = bend(inputs.in[c][i]);
                outputs.out[c][i] = shape == Shape::Clip ? y : shape == Shape::Square ? y * y : -y;
            }
    }
};

}  // namespace rp::graph::devtest

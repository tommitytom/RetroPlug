// The fixture pack's node types (packages/native/test/packs/fixture/CMakeLists.txt): a constant tone and a gain, built
// on the node SDK as any C++ package's are. The broken variants compile this same file with other definitions, so
// each differs from the good pack in exactly the way its manifest or its CMake says.
//
//   RP_PACKFIXTURE_PREFIX      the type ids' prefix ("rp.packfixture": rp.packfixture.tone, rp.packfixture.gain)
//   RP_PACKFIXTURE_PACKAGE     the rp_package's id ("rp.packfixture")
//   RP_PACKFIXTURE_TONE_ID     the tone's id in place of PREFIX.tone (the duplicate variant takes a built-in's)
//   RP_PACKFIXTURE_EXTRA_TYPE  defined: a third type, PREFIX.extra, which the undeclared variant's manifest leaves out
//   RP_PACKFIXTURE_ENTRY       the C entry's name (rp_packfixture_package): FixtureSecond.cpp compiles this file again,
//                              under another prefix and entry, as the pair variant's second package
//
// The types are in an unnamed namespace, so two translation units of this file in one module are two sets of types.

#include <cstdint>
#include <iterator>

#include "graph/node.hpp"

#ifndef RP_PACKFIXTURE_PREFIX
#define RP_PACKFIXTURE_PREFIX "rp.packfixture"
#endif
#ifndef RP_PACKFIXTURE_PACKAGE
#define RP_PACKFIXTURE_PACKAGE "rp.packfixture"
#endif
#ifndef RP_PACKFIXTURE_TONE_ID
#define RP_PACKFIXTURE_TONE_ID RP_PACKFIXTURE_PREFIX ".tone"
#endif
#ifndef RP_PACKFIXTURE_ENTRY
#define RP_PACKFIXTURE_ENTRY rp_packfixture_package
#endif

namespace rp::packfixture {

// Outside the unnamed namespace: a settings struct is named for its schema (graph/node.hpp), and the two translation
// units of the pair variant define it identically.
struct ToneSettings {
    float level = 0.25f;  // every sample of both channels
};

namespace {

// A constant on both channels: what a test reads back to know the pack's code ran.
struct Tone {
    static constexpr const char* id = RP_PACKFIXTURE_TONE_ID;
    using Settings = ToneSettings;
    struct { rp::audio_out<rp::stereo> out; } outputs;

    float level = 0.25f;
    void on_settings(const Settings& s) noexcept { level = s.level; }
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = level;
    }
};

// Its input scaled by its param.
struct Gain {
    static constexpr const char* id = RP_PACKFIXTURE_PREFIX ".gain";
    struct { rp::audio_in<rp::stereo> in; } inputs;
    struct { rp::audio_out<rp::stereo> out; } outputs;
    struct { rp::float_param<"Gain", rp::range{0, 4, 1}> gain; } params;

    void process(const rp::Block& b) noexcept {
        const float g = static_cast<float>(params.gain.latest());
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
            for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[c][i] = inputs.in[c][i] * g;
    }
};

#ifdef RP_PACKFIXTURE_EXTRA_TYPE
struct Extra {
    static constexpr const char* id = RP_PACKFIXTURE_PREFIX ".extra";
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.0f;
    }
};
#endif

const rp_package* package() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Tone>(),
        rp::node_type<Gain>(),
#ifdef RP_PACKFIXTURE_EXTRA_TYPE
        rp::node_type<Extra>(),
#endif
    };
    static const rp_package kPackage{RP_ABI_VERSION, "{\"id\":\"" RP_PACKFIXTURE_PACKAGE "\",\"version\":\"1.0.0\"}",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace

}  // namespace rp::packfixture

extern "C" const rp_package* RP_PACKFIXTURE_ENTRY(void) { return rp::packfixture::package(); }

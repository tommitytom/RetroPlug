#pragma once

#include <cstdint>
#include <span>

// Generic plugin description for the dpf.js framework: the runtime parameter
// list + identity, supplied by the consumer. The framework reads whatever the
// consumer provides (LvglJsEngine adapts to any/zero parameters), so a plugin
// with no parameters is valid.
//
// Deliberately free of DPF: `ParamSpec::hints` is a plain uint32_t; the consumer
// fills it with DPF's kParameterIs* flags. (DistrhoPluginInfo.h remains the
// compile-time DPF identity — this struct is the runtime truth.)
namespace dpfjs {

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* shortName;
    const char* unit;
    float       min;
    float       max;
    float       def;
    std::uint32_t hints;
};

struct PluginDescriptor {
    const char*               name       = "dpf.js";
    const char*               uri        = "urn:dpfjs:plugin";
    std::uint32_t             numInputs  = 0;
    std::uint32_t             numOutputs = 2;
    std::span<const ParamSpec> parameters = {};
};

// A zero-parameter default (used by the seam probe + any consumer that declares
// no parameters).
inline constexpr PluginDescriptor kEmptyDescriptor{};

} // namespace dpfjs

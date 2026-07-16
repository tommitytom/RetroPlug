#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Compile an ES5 translator script to QuickJS bytecode on a transient, bare QuickJS context
// (deliberately NOT the DSP runtime's context — so the DSP side is proven never to re-parse
// source). This is the "control-plane compiles the script" step of the DSP-JS-runtime design
// (packages/retroplug/plans/03-dsp-js-runtime.md): the bytecode is the only thing
// that crosses into the DSP heap.
//
// Returns the bytecode bytes (JS_WriteObject / JS_WRITE_OBJ_BYTECODE) or nullopt on a compile
// error. The bytecode is version-locked to this exact qjs-ng build, which is fine: the
// compiler and DSP contexts are the same `qjs`.
namespace dsp {

std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source);

} // namespace dsp

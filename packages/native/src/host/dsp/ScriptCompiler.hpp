#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Compile JS source, as global code, to QuickJS bytecode on a transient, bare QuickJS context. The
// engine-graph tests compile their toy script nodes with it (ScriptRig.hpp, KernelNode.test.cpp), and
// load the bytecode into a ScriptHost as a bundle chunk. (It compiled the scripts the control plane
// sent the native DSP runtime, through the compileScript RPC, until both were deleted.)
//
// Returns the bytecode bytes (JS_WriteObject / JS_WRITE_OBJ_BYTECODE) or nullopt on a compile
// error. The bytecode is version-locked to this exact qjs-ng build, which is fine: the
// compiler and the script host's runtimes are the same `qjs`.
namespace dsp {

std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source);

} // namespace dsp

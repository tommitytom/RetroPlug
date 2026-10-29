#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Compile JS source, as global code, to QuickJS bytecode on a transient, bare QuickJS runtime. Engine::loadScriptPack
// compiles a script node pack's source with it, on the control thread, and loads the bytecode into the pack's runtimes
// as its one chunk (docs/modular-graph-phase-c1-design.md section 4); the engine-graph tests compile their toy script
// nodes with it (ScriptRig.hpp, ProtocolNodes.test.cpp), and load the bytecode into a ScriptHost as a bundle chunk.
//
// Returns the bytecode bytes (JS_WriteObject / JS_WRITE_OBJ_BYTECODE), or nullopt, with the reason in *error when it is
// given. A compile error reads "the source does not compile: " and QuickJS's own text, after the line it was found on
// when QuickJS says (a syntax error does), as "the source does not compile: line 3: SyntaxError: unexpected token in
// expression: ';'"; a runtime or context that could not be made, or bytecode that could not be written, says that
// instead. The parser recurses on the calling thread, so it is held to `stackBytes` of JS stack (the script host's,
// ScriptHost::kStackBytes: the callers pass it, so this header needs nothing of the host). QuickJS's parser records a
// column as well, but the one a syntax error carries is wherever the parser last set it (the line's first column, as
// often as not), so it is left out. The bytecode is version-locked to this exact qjs-ng build, which is fine: the
// compiler and the script host's runtimes are the same `qjs`; and it is why a pack arrives as source, never as
// bytecode.
namespace dsp {

std::optional<std::vector<std::uint8_t>> compileToBytecode(const std::string& source, std::size_t stackBytes,
                                                           std::string* error = nullptr);

} // namespace dsp

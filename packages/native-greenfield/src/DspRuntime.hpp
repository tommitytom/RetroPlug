#pragma once

#include <cstdint>
#include <vector>

// Forward-declared so quickjs.h stays out of this header (it's C, wrapped in extern "C" in
// the .cpp). These match quickjs.h's `typedef struct JSRuntime JSRuntime;` tags.
struct JSRuntime;
struct JSContext;

// The DSP-side JS runtime: a second, BARE QuickJS context (no txiki) that runs a translator
// script per block, fed only by bytes. Proves the two-runtime byte seam from
// packages/retroplug-greenfield/plans/03-dsp-js-runtime.md — the script crosses as bytecode,
// config as bytes, and per-block I/O as structured bytes; a JSValue never crosses.
//
// Minimal first cut: one input (MidiIn[] + BlockInfo), config as a JSON string the script
// parses once into pre-allocated slots, and one output sink `emitMidiOut(frame, [bytes])`
// bound onto the context global. No audio thread, no RT queue yet — the per-block drive is a
// direct call (doc-03's "functional first cut").
class DspRuntime {
public:
    struct MidiIn  { std::uint32_t frame = 0; std::vector<std::uint8_t> data; };
    struct MidiOut { std::uint32_t frame = 0; std::vector<std::uint8_t> data; };
    struct BlockInfo {
        std::uint32_t frames          = 0;
        double        sampleRate       = 44100.0;
        double        tempo            = 120.0;
        double        ppqPosBlockStart = 0.0;
        bool          transportPlaying = false;
    };

    DspRuntime();
    ~DspRuntime();
    DspRuntime(const DspRuntime&) = delete;
    DspRuntime& operator=(const DspRuntime&) = delete;

    // Instantiate a script from QuickJS bytecode: JS_ReadObject + JS_EvalFunction runs the ES5
    // global code (defining the `setConfig` / `onBlock` globals). Re-loading swaps behavior
    // (hot-reload). Returns false on a read/eval exception.
    bool loadScript(const std::vector<std::uint8_t>& bytecode);

    // Hand the script a config blob (a JSON string in the first cut). Calls the global
    // `setConfig`, which parses once and overwrites its pre-allocated slots. False if no
    // script is loaded / no `setConfig` / it threw.
    bool setConfig(const std::vector<std::uint8_t>& bytes);

    // Run one block: build the JS input, call the global `onBlock`; the bound `emitMidiOut`
    // sink fills the returned list. Empty when no script is loaded.
    std::vector<MidiOut> runBlock(const std::vector<MidiIn>& midi, const BlockInfo& block);

private:
    JSRuntime* rt_     = nullptr;
    JSContext* ctx_    = nullptr;
    bool       loaded_ = false;
    // Per-block collector; its address is the context opaque, so the emitMidiOut C sink
    // appends here. Cleared at the start of each runBlock (address stays stable).
    std::vector<MidiOut> out_;
};

#pragma once

#include <cstdint>
#include <vector>

// Forward-declared so quickjs.h stays out of this header (it's C, wrapped in extern "C" in
// the .cpp). These match quickjs.h's `typedef struct JSRuntime JSRuntime;` tags.
struct JSRuntime;
struct JSContext;

// The DSP-side JS runtime: a second, BARE QuickJS context (no txiki) that runs the whole DSP role
// KERNEL (packages/retroplug-greenfield/src/dspKernel.ts) per block, fed only by bytes. Native is a
// dumb, role-agnostic runner: it loads the kernel as bytecode, pushes the system structure once,
// hands it per-block input, and reads back SYSTEM-ADDRESSED output sinks — a JSValue never crosses.
//
// The kernel bundle defines two globals when evaluated:
//   - setSystems(jsonString)   — the (rarely-changing) system + pipeline structure. Parsed once.
//   - processBlock(input)      — run one block over that structure. `input` is the dynamic data.
// and calls three bound C-function sink thunks as it runs (all system-addressed, so one context
// drives every system):
//   - pushSerialIn(system, frame, byte)          — serial-in sink → serialIn_
//   - emitMidiOut(system, frame, [bytes])        — host MIDI-out sink → midiOut_
//   - pressButton(system, frame, button, down)   — role-generated button sink → buttonOut_
// Tick state (the drift-exact PPQ clock) lives ENTIRELY in the JS kernel now (walkTicks) — native
// no longer owns a nextTick or an eachTick primitive.
//
// No audio thread, no RT queue yet — the per-block drive is a direct call (doc-03's first cut).
class DspRuntime {
public:
    // --- per-block input (crosses into the JS `processBlock`) ---
    struct MidiIn   { std::uint32_t frame = 0; std::vector<std::uint8_t> data; };  // global; routing assigns a system
    struct ButtonIn { std::uint32_t system = 0; std::uint32_t frame = 0; std::uint32_t button = 0; bool down = false; };
    struct KeyIn    { std::uint32_t system = 0; std::uint32_t frame = 0; std::uint32_t key = 0; bool down = false; };
    struct BlockInfo {
        std::uint32_t frames     = 0;
        double        sampleRate = 44100.0;
        double        tempo      = 120.0;
        double        ppqStart   = 0.0;
        bool          transport  = false;
    };

    // --- per-block output (the bound sinks fill these; the caller fans them to cores by system) ---
    struct SerialIn  { std::uint32_t system = 0; std::uint32_t frame = 0; std::uint8_t byte = 0; };
    struct MidiOut   { std::uint32_t system = 0; std::uint32_t frame = 0; std::vector<std::uint8_t> data; };
    struct ButtonOut { std::uint32_t system = 0; std::uint32_t frame = 0; std::uint32_t button = 0; bool down = false; };

    DspRuntime();
    ~DspRuntime();
    DspRuntime(const DspRuntime&) = delete;
    DspRuntime& operator=(const DspRuntime&) = delete;

    // Instantiate the kernel from QuickJS bytecode: JS_ReadObject + JS_EvalFunction runs the ES5
    // global code, which defines the `setSystems` / `processBlock` globals. Re-loading swaps the
    // kernel (hot-reload). Returns false on a read/eval exception.
    bool loadKernel(const std::vector<std::uint8_t>& bytecode);

    // Push the system structure (a JSON string in the first cut): calls the global `setSystems`,
    // which parses it once into the kernel. False if no kernel is loaded / no `setSystems` / it threw.
    bool setSystems(const std::vector<std::uint8_t>& json);

    // Run one block: build the JS input (block info + dynamic events), call the global `processBlock`;
    // the bound sinks fill serialIn_/midiOut_/buttonOut_ (all cleared at the top of the call). A
    // no-op (empty output) when no kernel is loaded.
    void processBlock(const std::vector<MidiIn>& midi,
                      const std::vector<ButtonIn>& buttons,
                      const std::vector<KeyIn>& keys,
                      const BlockInfo& block);

    // --- public for the bound C-function thunks only (the SameBoySystem idiom) ---
    // The DspRuntime is the context opaque, so the sink thunks reach these. All three are cleared at
    // the top of each processBlock; the caller reads them after the call and fans them to cores.
    std::vector<SerialIn>  serialIn_;   // pushSerialIn sink
    std::vector<MidiOut>   midiOut_;    // emitMidiOut sink
    std::vector<ButtonOut> buttonOut_;  // pressButton sink

private:
    JSRuntime* rt_     = nullptr;
    JSContext* ctx_    = nullptr;
    bool       loaded_ = false;
};

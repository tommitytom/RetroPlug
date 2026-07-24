#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Forward-declared so quickjs.h stays out of this header (it's C, wrapped in extern "C" in
// the .cpp). These match quickjs.h's `typedef struct JSRuntime JSRuntime;` tags.
struct JSRuntime;
struct JSContext;

// --- DSP-runtime allocation profiling (spec/08-profiling.md) --------------------------------------
// Populated only in a RETROPLUG_PROFILE build; otherwise every field is 0 and `enabled` is false.
// Counts are DELTAS since the last resetAllocStats(); `liveBytesDelta` is the net live-heap change
// over that window (flat ~0 = refcount churn, rising = leak). blockCount/maxBlock* aggregate the
// per-processBlock allocation deltas natively, so the benchmark reads them once per window.
struct DspAllocStats {
    bool          enabled            = false;
    std::uint64_t allocCalls         = 0;
    std::uint64_t reallocCalls       = 0;
    std::uint64_t freeCalls          = 0;
    std::uint64_t allocBytes         = 0;   // cumulative usable bytes allocated in the window
    std::int64_t  liveBytesDelta     = 0;   // net live-heap change over the window
    std::uint64_t peakBytes          = 0;   // all-time peak live bytes (informational)
    std::uint64_t blockCount         = 0;   // processBlock calls in the window
    std::uint64_t maxBlockAllocCalls = 0;   // worst single-block allocation count (the tail)
    std::uint64_t maxBlockAllocBytes = 0;
};

// Result of a self-driven JS_RunGC (cycle-collection) pass. `freedBytes` ~0 proves the acyclic kernel
// accumulates no reference cycles; a large value/time is a cycle leak. enabled=false off-profile.
struct DspGcResult {
    bool         enabled    = false;
    double       ms         = 0.0;
    std::int64_t freedBytes = 0;
};

// One recorded timing span (spec/08-profiling.md Tier B — per-role runtime profile). `t0`/`t1` are
// microseconds relative to the window base set by traceReset(); `label` indexes traceNames(). Native
// pipeline stages use the fixed ids below; the JS kernel interns role kinds from DSP_SPAN_ROLE_BASE up.
struct DspTraceSpan {
    std::uint32_t label = 0;
    double        t0    = 0.0;
    double        t1    = 0.0;
};

// Fixed span-label ids for the native pipeline stages; JS role kinds are interned from ROLE_BASE up.
enum DspSpanLabel : std::uint32_t {
    DSP_SPAN_KERNEL    = 0,   // Engine: the whole DSP-kernel stage (marshal + JS + sink fan-out)
    DSP_SPAN_MARSHAL   = 1,   // DspRuntime: C→JS input marshalling
    DSP_SPAN_JSCALL    = 2,   // DspRuntime: the JS_Call into the kernel's processBlock
    DSP_SPAN_APU       = 3,   // Engine: the SameBoy core/APU render (runBlock)
    DSP_SPAN_PUBLISH   = 4,   // Engine: the state pump to the control plane (per-block frame copy +
                              //   the coarse-interval savestate/SRAM republish — spiky, timer-gated)
    DSP_SPAN_ROLE_BASE = 16,  // JS kernel interns role kinds (mgb, midi-routing, …) from here up
};

// Raw cumulative counters the profiling allocator bumps (the JS_NewRuntime2 opaque). Plain POD, so
// the header needs no quickjs types. Zero + unused in a non-profile build.
struct DspAllocCounters {
    std::uint64_t allocCalls = 0, reallocCalls = 0, freeCalls = 0, allocBytes = 0;
    std::int64_t  liveBytes = 0;
    std::uint64_t peakBytes = 0;
};

// The DSP-side JS runtime: a second, BARE QuickJS context (no txiki) that runs the whole DSP role
// KERNEL (packages/retroplug/src/dspKernel.ts) per block, fed only by bytes. Native is a
// dumb, role-agnostic runner: it loads the kernel as bytecode, pushes the system structure once,
// hands it per-block input, and reads back SYSTEM-ADDRESSED output sinks — a JSValue never crosses.
//
// The kernel bundle defines two globals when evaluated:
//   - setSystems(jsonString)   — the (rarely-changing) system + pipeline structure. Parsed once.
//   - processBlock(input)      — run one block over that structure. `input` is the dynamic data.
// and calls three bound C-function sink thunks as it runs (all system-addressed, so one context
// drives every system):
//   - pushSerialIn(system, frame, byte)          — serial-in sink → serialIn_
//   - emitMidiOut(system, frame, [bytes])        — host MIDI-out sink (→ DAW) → midiOut_
//   - emitCoreMidi(system, frame, [bytes])       — MIDI-IN-to-core sink (→ onMidi) → coreMidi_
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
    struct SerialOut { std::uint32_t system = 0; std::uint8_t byte = 0; };  // raw bytes a core emitted last block (LSDj MI.OUT)
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
    struct CoreMidi  { std::uint32_t system = 0; std::uint32_t frame = 0; std::vector<std::uint8_t> data; };
    struct CoreBytes { std::uint32_t system = 0; std::uint32_t frame = 0; std::vector<std::uint8_t> data; };
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
    // no-op (empty output) when no kernel is loaded. `serialOut` carries the raw bytes each core
    // emitted on its serial port LAST block (LSDj MI.OUT — one-block latency; the caller gathers them
    // after runBlock), fanned to `ctx.serialOut` for the addressed system.
    void processBlock(const std::vector<MidiIn>& midi,
                      const std::vector<ButtonIn>& buttons,
                      const std::vector<KeyIn>& keys,
                      const std::vector<SerialOut>& serialOut,
                      const BlockInfo& block);

    // --- allocation / GC profiling (spec/08-profiling.md); real only in a RETROPLUG_PROFILE build ---
    // allocStats() reports window deltas; resetAllocStats() opens a new window (optionally pinning
    // auto-GC off for determinism); runGc() times a self-driven cycle pass. All no-op / enabled=false
    // when built without RETROPLUG_PROFILE.
    DspAllocStats allocStats() const;
    void          resetAllocStats(bool disableAutoGc);
    DspGcResult   runGc();

    // --- per-role runtime tracing (spec/08-profiling.md Tier B); real only under RETROPLUG_PROFILE ---
    // spanBegin/spanEnd bracket a nested wall-time span; the native pipeline stages call them directly
    // and the JS kernel reaches them via bound thunks (spanBegin/spanEnd/traceName) around each role.
    // traceReset(arm) opens a window (clears buffers, pins the clock base) and flips the kernel's
    // __setTrace so JS only emits when armed — keeping the non-traced path allocation-identical.
    // traceSpans()/traceNames() dump the window for the bench to assemble Chrome trace-event JSON.
    void spanBegin(std::uint32_t label);
    void spanEnd();
    void traceName(std::uint32_t label, const std::string& name);
    void traceReset(bool arm);
    std::vector<DspTraceSpan> traceSpans() const;
    std::vector<std::string>  traceNames() const;

    // --- public for the bound C-function thunks only (the SameBoySystem idiom) ---
    // The DspRuntime is the context opaque, so the sink thunks reach these. All three are cleared at
    // the top of each processBlock; the caller reads them after the call and fans them to cores.
    std::vector<SerialIn>  serialIn_;   // pushSerialIn sink
    std::vector<MidiOut>   midiOut_;    // emitMidiOut sink (host MIDI-out → DAW)
    std::vector<CoreMidi>  coreMidi_;   // emitCoreMidi sink (MIDI-in → the core's onMidi)
    std::vector<CoreBytes> coreBytes_;  // pushCoreBytes sink (raw bytes → the core's device byte-input, no framing)
    std::vector<ButtonOut> buttonOut_;  // pressButton sink

private:
    JSRuntime* rt_     = nullptr;
    JSContext* ctx_    = nullptr;
    bool       loaded_ = false;

    // Profiling state (bumped only under RETROPLUG_PROFILE; a harmless handful of zeroed ints
    // otherwise). `counters_` is the live allocator opaque; `base_` is the resetAllocStats snapshot
    // the window deltas are measured against.
    DspAllocCounters counters_;
    DspAllocCounters base_;
    std::uint64_t    blockCount_         = 0;
    std::uint64_t    maxBlockAllocCalls_ = 0;
    std::uint64_t    maxBlockAllocBytes_ = 0;

    // Per-role runtime tracing (spec/08-profiling.md Tier B; recorded only under RETROPLUG_PROFILE while
    // armed). traceStack_ holds the currently-open spans as {label, t0µs}; spanEnd pops → traceSpans_.
    bool                                          traceArmed_  = false;
    std::uint64_t                                 traceBaseNs_ = 0;  // steady_clock ns at last traceReset
    std::vector<DspTraceSpan>                     traceSpans_;
    std::vector<std::string>                      traceNames_;
    std::vector<std::pair<std::uint32_t, double>> traceStack_;
};

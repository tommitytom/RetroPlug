#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rfl/Bytestring.hpp>

#include "host/rpc/BackendTypes.hpp"
#include "host/script/ScriptHost.hpp"  // DspAllocStats / DspGcResult / DspTraceSpan (no quickjs types)

class Engine;
class QueuedInvoker;
namespace rp::kit { class KitCompiler; }  // fwd: lazy, kept out of the header (enkiTS/r8brain heavy)

// The emulator surface (lifecycle / reads / MIDI / transport / profiling) as a THIN RPC layer over
// (Engine + the one Invoker). No threading branches: every mutation just pushes onto
// the invoker (which flushes inline when quiescent, or hands to the audio thread when it owns the
// Engine); reads come from the snapshot registry, so they need no guard.
class EngineRpcService {
public:
    EngineRpcService(Engine& engine, QueuedInvoker& invoker);
    ~EngineRpcService();   // out-of-line: kitCompiler_ holds a fwd-declared KitCompiler

    // --- emulator lifecycle / reads ---
    // (duplicate, reload and remove live in the TS SystemsStore over the resource calls and setGraph.)
    // Put an immutable byte resource in the engine's ResourceTable (docs/modular-graph-phase-c3-design.md section 4)
    // and answer its id, which a node's settings name as "res:<id>"; or, creating none, why. Its type is "rom",
    // "sram" or "state"; a file named by `path` is read here, so an unreadable or empty one is answered before any
    // document naming it is written.
    RpcPutResourceResult putResource(BackendResourceSpec spec);
    // The owner's drop of a resource putResource made: idempotent (false for an unknown or already dropped id), and it
    // never frees one a plan or a node instance still pins, which lasts until the plan that ran the instance is
    // reclaimed.
    bool                releaseResource(std::uint32_t id);
    // Plan a document TS built (docs/modular-graph-phase-b2-design.md section 2.5).
    RpcSetGraphResult           setGraph(RpcSetGraphRequest request);
    // Load a pack of script node types, or reload one loaded already (Engine::loadScriptPack,
    // docs/modular-graph-phase-c1-design.md section 7), while the audio thread runs blocks or not. Refused, changing
    // nothing, for a memory limit that is no whole number of bytes from 0 to 2^53 - 1 (BackendTypes.hpp).
    RpcLoadScriptPackResult     loadScriptPack(RpcLoadScriptPackRequest request);
    // A live change to a param of a node the last published plan places (Engine::setParam), at the start of the next
    // block: a system node's gain or a NES's expansionVolume, as the store sends an edit of either, with no publish.
    // False for a node or a param that plan does not have, a NaN, a param a wire drives, or a full command ring.
    bool setParam(std::string nodeId, std::string paramId, double value);
    std::optional<rfl::Bytestring> readState(std::uint32_t id);
    std::optional<rfl::Bytestring> readSram(std::uint32_t id);
    std::optional<rfl::Bytestring> readRam(std::uint32_t id);   // work RAM (WRAM), per-block published copy
    // The WRITE counterpart of readRam, in the same coordinates. Queued like pressButton, so it lands
    // between blocks and is safe while the audio thread runs - unlike the debug facet's writeCpu.
    bool            writeRam(std::uint32_t id, std::uint32_t offset, rfl::Bytestring bytes);
    bool screenshot(std::uint32_t id, std::string path);
    RpcFrame getFrame(std::uint32_t id);

    // (Live-core debug/inspection — getApuState/readCpu/breakpoints/trace/profiler/… — moved to
    // DebugRpcService; only the CLI binds that facet.)

    // --- audio render / input drive / transport ---
    bool            pressButton(std::uint32_t id, std::uint32_t button, bool down);
    // Control MIDI for ONE system, bypassing the routing node: a settings knob that must reach the cart
    // it belongs to, however the project routes musical MIDI. Up to 4 bytes (a channel message).
    bool            stageSystemMidi(std::uint32_t id, std::vector<std::uint8_t> bytes);
    rfl::Bytestring renderAudio(double ms);
    // Per-system audio: each live system's interleaved-stereo PCM, in slot order (marshals to
    // Uint8Array[]). Isolates each core so LSDj link-cable sync is provable (a follower's own RMS).
    std::vector<rfl::Bytestring> renderAudioPerSystem(double ms);
    // Per-channel audio for ONE system: each of its channelLayout() streams as its own interleaved-stereo
    // PCM (Game Boy = 4: Pulse 1/Pulse 2/Wave/Noise). Single-system only (empty if systemCount() != 1 or
    // `id` is unknown) - the PerChannel shape keys off the stream, not the slot. Marshals to Uint8Array[].
    std::vector<rfl::Bytestring> renderAudioPerChannel(std::uint32_t id, double ms);
    // Compile an LSDJ sample kit from source audio files → a 16 KB kit bank (harness/tooling only; the
    // plugin never binds this). Resample (r8brain) + effects + 4-bit nibble-pack per sample, fanned across
    // an enkiTS pool by the owned KitCompiler; a per-sample load failure just leaves that slot empty.
    rfl::Bytestring compileKit(KitCompileSpec spec);
    // Compile a risa NES-DPCM sample kit → an 8 KB kit bank (harness/tooling only). Same generic pipeline
    // as compileKit (reuses the lazy kitCompiler_) but drives the DMC codec: r8brain resample to the PAL
    // DPCM rate + 1-bit ±2 delta encode + 64-byte-aligned pack.
    rfl::Bytestring compileDmc(RisaKitCompileSpec spec);
    // The engine's audio sample rate (Hz), so callers can label WAV output correctly.
    double          sampleRate() const;
    // Set the host sample rate (Hz). Baked into each core at construct, so it only takes effect BEFORE any
    // system is built — rejected (false) once a system exists, since there's no resample-on-change today.
    bool            setSampleRate(double sr);
    bool            setTransport(bool running);
    bool            setPpq(double ppq);
    bool            setBpm(double bpm);
    // Host MIDI for the next block rendered, at `frame` inside it (any order, as a plugin stages a block: its events,
    // then each moved automation slot's CC at frame 0). False, nothing staged, for an empty message or a frame of
    // kBlockSize or more. The frame lands in renderAudio's next block when it renders at least kBlockSize frames (its
    // first block is the lesser of kBlockSize and the whole render); a block of `frame` frames or fewer, a renderBlocks
    // one included, is the caller's to avoid. The frame arrives as rpcpp reads any integer, truncated to its low 32 bits
    // (1.5 as 1, NaN as 0), so only the TS driver (audioDriver.ts) can refuse a frame that is no integer in range.
    bool            stageMidiIn(std::vector<std::uint8_t> bytes, std::uint32_t frame);
    // The control-surface twin: staged as `controllerIn` rather than musical MIDI, so a headless test
    // can drive a Launchpad app without a Launchpad. Harness-facet only, like stageMidiIn.
    bool            stageControllerIn(std::vector<std::uint8_t> bytes);
    // Drain the host MIDI out (LSDj's serial-out decoders, a controller's launches), accumulated across renderAudio
    // blocks since the last drain. For headless tests; the plugin drains Engine::midiOut() to the DAW directly.
    std::vector<RpcMidiOut> drainMidiOut();
    // Render a scripted run of host blocks the way PluginDSP::run renders each one: stage the block's MIDI
    // at its frames, drain the command ring, apply its transport, processBlock(frames, lanes, outputs),
    // then take and clear the host MIDI out. Quiescent only. The parity suite's capture call.
    // ppq (a locate) and controller have no PluginDSP::run counterpart (the plugin never calls setPpq, which
    // would double-count; controller traffic is the SDL standalone's path), and the plugin's CC-automation
    // stage and MIDI-out size filter are not modelled.
    RpcRenderBlocksResult   renderBlocks(RpcRenderBlocksRequest req);
    // The target this binary was built for, "<os>-<arch>-<compiler><major>" (linux-x64-gnu13): which
    // parity baseline its float output can be compared against. MSVC reports the full _MSC_VER (msvc1951).
    std::string             buildTarget() const;
    // Messages the graph's full output queues have dropped so far (Engine::droppedMessages), so a test can hold a
    // run to none. Never reset: compare two reads.
    std::uint32_t           droppedMessages() const;

    // --- profiling the audio runtimes (spec/08-profiling.md): the script host's allocation window (every pack's audio
    // runtime, summed), GC pass (a collection of each) and trace window, reached directly through the Engine (the
    // quiescent renderAudio pull path owns it: the benchmark never starts the audio thread). dspResetAllocStats and
    // dspTraceReset return false, dspRunGc enabled false and the trace reads empty when refused (the audio thread
    // running) or on a host that is not a profile build; dspAllocStats never refuses, and is enabled false only
    // off-profile ---
    rp::host::DspAllocStats             dspAllocStats();
    bool                                dspResetAllocStats(bool disableAutoGc);
    rp::host::DspGcResult               dspRunGc();
    bool                                dspTraceReset(bool arm);
    std::vector<rp::host::DspTraceSpan> dspTrace();
    std::vector<std::string>            dspTraceNames();

private:
    bool rendersInto(std::size_t channels) const;
    // Append the block's host MIDI out (Engine::midiOut(), cleared each block) to accumMidiOut_.
    // Called after each processBlock in the render loops so drainMidiOut() sees the whole window.
    void accumulateMidiOut();

    Engine&        engine_;
    QueuedInvoker& invoker_;   // the one mutation path (push; flushes inline when quiescent)

    static constexpr std::uint32_t kBlockSize = 1024;
    std::vector<float>       scratchL_;  // renderAudio pull-path scratch (control thread)
    std::vector<float>       scratchR_;
    std::vector<RpcMidiOut> accumMidiOut_;  // host MIDI out gathered across a render window (drainMidiOut)
    std::unique_ptr<rp::kit::KitCompiler> kitCompiler_;  // lazy: built on first compileKit (enkiTS pool)
};

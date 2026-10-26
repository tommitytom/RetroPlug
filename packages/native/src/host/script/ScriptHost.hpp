#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "graph/rp_node.h"
#include "transport/SpscRing.hpp"

// Forward-declared so quickjs.h stays out of this header (DspRuntime does the same); these match quickjs.h's tags.
struct JSRuntime;
struct JSContext;

namespace rp::host {

// What the audio-side runtime's heap has done since the host was made, cumulative: every block, adoption and
// release that entered JS. A test reads it either side of a stretch of blocks. Written only by the thread that runs
// blocks; any thread may read it, a read on another thread lagging a running audio thread.
struct ScriptAllocations {
    std::uint64_t allocs = 0;    // malloc and calloc calls
    std::uint64_t reallocs = 0;
    std::uint64_t frees = 0;
    std::uint64_t bytes = 0;     // asked for, by allocs and growing reallocs
};

// The script host (docs/modular-graph-phase-b3a-design.md section 2): every TS node one Engine runs, behind the
// same C ABI as a native node. A bundle registers its node definitions (defineNode results, appended to the global
// __rp_scriptNodes: packages/retroplug/src/script/register.ts), and the host registers an rp_node_type for each in
// one package, "rp.script", whose instances are native shims.
//
// Two bare QuickJS runtimes, each loaded with the same bundle and sharing no JS value:
//   - the CONTROL side, entered only by the control thread, answers layout_for and prepare_settings: the
//     definition's own parseSettings checks a settings object, and its layout gives the ports, from the settings
//     alone. It never holds an instance.
//   - the AUDIO side holds every instance and is entered only by the thread that runs blocks (the audio thread, or
//     the control thread while setAudioThreadOwns(false)). A shim's create and activate run on the control thread
//     and only allocate the shim; the JS instance is created on the block thread at the first call the shim gets
//     there, which is the ports_changed the planner sends every fresh instance when its plan is adopted, so before
//     that plan's first process. A shim's destroy (control thread) queues the instance's release, which beginBlock
//     performs at the start of the next block, or does it there and then while no audio thread runs; the record it
//     frees then goes back to the control thread to be deleted (reclaim), so the audio thread frees nothing native.
//
// Every type declares RP_TYPE_STATELESS_PREPARE: its prepare_settings reads the settings it is handed, the
// definition and what the instance was created with, never anything a commit writes. A type that reshapes answers
// RP_PREPARE_RESHAPE for every change its definition accepts; one that does not answers RP_PREPARE_SWAP while the
// layout stays the one it was created with and RP_PREPARE_REBUILD when it moves.
//
// A node's process gets one Block object, the same every block (Phase A's Block, packages/retroplug/src/graph/
// defineNode.ts): messages(port) is a reused Message[] whose payloads are copied into one JS-owned buffer per port,
// control(id) a reused { start, changes }, audio(port) Float32Array views straight over the plan's channel buffers
// (made again only when a buffer or the block's length moves, and detached before what they look at can be freed),
// and send and emit write the plan's output queues directly. Ports are found by id through atoms made when the
// instance, or the layout it reshapes to, is bound, so a warm block allocates nothing native, and a node written
// with indexed loops nothing in JS either. What a block hands out is good for that process call: keep copies.
//
// A throw in process is caught, counted and the block's output up to it stands: the node runs again next block
// (design section 4.5), unless its definition says faultOnThrow, when it faults (RP_FAULT, the executor's latch).
// Returning "fault" faults it too. The first throw's message is kept, and logged by reclaim.
class ScriptHost {
public:
    using Chunk = std::span<const std::uint8_t>;
    // Globals a host binds into one of the contexts before any bundle runs in it (the native tests' probes),
    // `audioSide` saying which; called once for each, on the constructing thread.
    using Binder = void (*)(void* user, JSContext* ctx, bool audioSide);

    explicit ScriptHost(Binder binder = nullptr, void* user = nullptr);
    // Every instance released first: no audio thread may own it (setAudioThreadOwns(false)), and every shim must
    // be gone (the graph that held them destroyed before this host).
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // The production bundle every Engine loads (packages/retroplug/src/script/bundle.ts, embedded at build time).
    static Chunk embeddedBundle();

    // ---- control thread ------------------------------------------------------------------------------
    // Run a bundle (global-code bytecode, in chunks run in order) in both contexts, then read the definitions it
    // registered. Once, before anything plans with the package. False, with a reason, when a chunk fails to load
    // or a definition cannot be read; the package then has no types, so every document naming one is refused
    // (unknown-type).
    bool load(std::span<const Chunk> chunks, std::string* error = nullptr);
    // The script node types, for NodeRegistry::addPackage; valid for the host's life (empty before a load).
    const rp_package* package() const { return &package_; }
    // Set true BEFORE an audio thread starts running blocks, false AFTER it has stopped: false releases every
    // queued instance there and then.
    void setAudioThreadOwns(bool owns);
    // Delete the records the block thread has released, and log the first throw once.
    void reclaim();
    // The first throw's type and message ("" until one), once the block thread has kept it.
    std::string firstThrow() const;

    // ---- the thread that runs blocks ---------------------------------------------------------------------
    // At the top of every block, before the graph runs: release what destroy queued.
    void beginBlock();

    // ---- any thread ------------------------------------------------------------------------------------------
    // Throws caught in a script node (process, and its create, onSettings, portsChanged and activate), so far.
    std::uint32_t     throwCount() const { return throws_.load(std::memory_order_relaxed); }
    ScriptAllocations audioAllocations() const;
    // JS instances created and not yet released.
    std::uint32_t     liveInstances() const { return live_.load(std::memory_order_relaxed); }

    // Defined in ScriptHost.cpp: a script type, and one instance's record.
    struct Type;
    struct Instance;

private:
    struct Glue;  // the ABI tables, the Block object's methods and the audio runtime's allocator
    struct Counters {
        std::atomic<std::uint64_t> allocs{0}, reallocs{0}, frees{0}, bytes{0};
    };

    void releaseJs(Instance& in);   // the block thread, or quiescent
    void retire(Instance* in);      // control thread: a destroyed shim's record
    void releaseQueued();           // quiescent: everything queued, released and deleted
    void freeTypes();

    // counters_ is the audio runtime's allocator opaque, so it is declared (and made) before it.
    Counters                     counters_;
    JSRuntime*                   controlRt_ = nullptr;
    JSContext*                   control_ = nullptr;
    JSRuntime*                   audioRt_ = nullptr;
    JSContext*                   audio_ = nullptr;
    std::uint32_t                blockClass_ = 0;  // the JSClassID of the Block object, in the audio runtime
    std::array<std::uint32_t, 16> atoms_{};        // JSAtoms the block path names properties by, in the audio runtime

    std::vector<std::unique_ptr<Type>> types_;
    std::vector<const rp_node_type*>   typeList_;
    rp_package                         package_{};

    std::atomic<bool>          audioOwns_{false};
    // destroy -> the block thread, and the released records back. A full release ring spills into backlog_ (the
    // control thread's), pushed on at the next destroy or reclaim; the way back, twice as deep, is drained by every
    // destroy and reclaim, and a record it cannot take is counted and leaked rather than freed on the audio thread.
    SpscRing<Instance*, 256>   releases_;
    SpscRing<Instance*, 512>   released_;
    std::vector<Instance*>     backlog_;
    std::atomic<std::uint32_t> leakedRecords_{0};
    std::uint32_t              leaksReported_ = 0;

    std::atomic<std::uint32_t> throws_{0};
    std::atomic<std::uint32_t> live_{0};
    // The first throw, kept by the block thread in a fixed buffer (it allocates nothing native there): claimed
    // 0 -> 1, written, then published 1 -> 2.
    std::atomic<int>           firstThrowState_{0};
    char                       firstThrow_[512] = {};
    bool                       firstThrowLogged_ = false;
};

}  // namespace rp::host

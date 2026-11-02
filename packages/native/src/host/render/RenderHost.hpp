#pragma once

// RenderHost — a self-contained, headless audio-render environment: a BARE QuickJS runtime (no txiki, no
// libuv) + its own Engine + the in-process RPC bridge, running the render-worker bundle
// (packages/retroplug/src/render/worker.ts) as global-code bytecode. It is the "retroplug-cli render"
// pipeline, driven synchronously on the calling thread: the body of an engine job, run on a pool worker
// (host/engine/OfflineJobs.hpp), so a fresh system renders to disk without touching the live audio-thread cores.
// Built, run and destroyed on the one thread. The job's engine runs whole blocks whatever each of the render's calls
// asks for, the frames past a call carried to the next (EngineRpcService::setContinuousBlocks), so a render's bytes do
// not depend on how much each call pulls (docs/modular-graph-phase-d3-design.md section 4).
//
// One-shot: construct, run() once, destroy. run() blocks for the whole render; a cancel the script does not see within
// kCancelGrace interrupts it. Only console + TextEncoder/TextDecoder + the RPC / rendered / cancel / result
// thunks are bound (the render path needs nothing else from the runtime). See ClassIdSpace note: bare QuickJS with
// no fresh native-class registration, so the multi-txiki-runtime class-id hazard does not apply here.

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Match quickjs.h's opaque tags so this header stays QuickJS-free (as ScriptHost.hpp does).
struct JSRuntime;
struct JSContext;

namespace retroplug {

class RenderHost {
public:
    using RenderedFn = std::function<void(double)>;  // audio rendered so far, in milliseconds
    using CancelFn   = std::function<bool()>;         // polled between boot steps and per chunk; true aborts the render
    // One console write the worker made (console.log and the rest, each ending in a newline), `error` for the stderr
    // ones (console.warn / error / trace) and for a script exception's report.
    using ConsoleFn  = std::function<void(bool error, const char* text)>;

    struct Result {
        std::string status;                // "done" | "cancelled" | "error"; empty if the worker never reported
        std::string message;               // error detail (when status == "error")
        std::vector<std::string> outputs;  // WAV paths written (when status == "done")
        bool ok() const { return status == "done"; }
    };

    // How long a cancel waits for the script to see it before the runtime's interrupt handler stops the script. A
    // render checks for a cancel between the steps of its boot and before each chunk, and unwinds through its own
    // catch, which removes the files it opened; a script that does not see one (wedged in a loop, or in a step longer
    // than this) is interrupted the next time it runs JS past the grace: its run ends cancelled, says so on its
    // console, and may leave the `.part` files it was writing, as QuickJS's interrupt is caught by no catch or
    // finally. A core wedged inside one native call is out of its reach. Two seconds is many chunks: a chunk is 100
    // ms of audio, rendered in a fraction of that.
    static constexpr std::chrono::milliseconds kCancelGrace{2000};

    RenderHost();
    ~RenderHost();
    RenderHost(const RenderHost&) = delete;
    RenderHost& operator=(const RenderHost&) = delete;

    // Render one job. `jobJson` is a RenderOpts-shaped spec (at least {"rom":"...","out":"..."}); the worker
    // fills the CLI defaults for anything omitted. onRendered and isCancelled may be empty. onConsole takes every
    // console write the worker makes and every script error's report; an empty one leaves them on the process's stdout
    // and stderr, where a report before run (the constructor's bootstrap failing) goes too. Returns the worker's
    // reported result (status "error" with a message on any internal failure).
    Result run(const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled, ConsoleFn onConsole);

    // run(), evaluating `source` (global code) in place of the render-worker bundle, in the same runtime, thunks and
    // Engine: for the host's own tests, which need a script the bundle is not (one that never polls its cancel).
    Result runScript(const std::string& source, const std::string& jobJson, RenderedFn onRendered,
                     CancelFn isCancelled, ConsoleFn onConsole);

    // Called by the bound JS thunks (which recover the host via the JS context opaque). Public so the
    // free-function thunks in the .cpp can reach them; not part of the external API.
    void onRendered(double ms);
    bool onCancelQuery();
    bool onInterruptPoll();
    void onResult(std::string status, std::string message, std::vector<std::string> outputs);
    void onStdout(const char* text);
    void onStderr(const char* text);

private:
    // The runtime's JS stack, held under the smallest thread a pool worker may be: a plain std::thread, 512 KiB on
    // macOS and 1 MiB on Windows, so 256 KiB leaves the engine, its cores and the RPC bridge, which run on the same
    // stack below the render's JS, the other half (as ScriptHost::kStackBytes). A recursion past it throws a RangeError
    // the worker reports as the render's error, where QuickJS's default of 1 MiB would run off a macOS worker's stack.
    // Measured on Linux x64 (GCC 13), by the least limit each render still finishes under and by a painted thread
    // stack, over the render baseline's LSDj (telemelt to its HFF, picked by name, and by index split) and risa
    // (ecoli_soul to its HFF, with a region role) cases, an smsggdj song and the SGB jingle with a model role: the JS
    // needs 68 KiB (68.5 for smsggdj), about the same for every render, so its peak is in what they share rather than
    // in a song, and 114.5 KiB (116) under ASan. The whole thread, cores included, peaks at 82 to 117 KiB (risa the
    // most), and at 186 KiB under ASan. So 256 KiB is 3.7 times what the JS uses, 2.2 times under ASan. An MSVC Debug
    // build, whose interpreter frames are larger, is unmeasured.
    static constexpr std::size_t kStackBytes = 256 * 1024;

    Result runWith(const std::string* source, const std::string& jobJson, RenderedFn onRendered, CancelFn isCancelled,
                   ConsoleFn onConsole);

    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;

    RenderedFn renderedFn_;
    CancelFn   cancelFn_;
    ConsoleFn  consoleFn_;
    Result     result_;
    // When the interrupt handler first saw the cancel (the epoch while it has not), and whether it then stopped the
    // script.
    std::chrono::steady_clock::time_point cancelSince_{};
    bool                                  interrupted_ = false;
};

} // namespace retroplug

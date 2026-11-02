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
// One-shot: construct, run() once, destroy. run() blocks for the whole render. Only console +
// TextEncoder/TextDecoder + the RPC / rendered / cancel / result thunks are bound (the render path needs
// nothing else from the runtime). See ClassIdSpace note: bare QuickJS with no fresh native-class
// registration, so the multi-txiki-runtime class-id hazard does not apply here.

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
    using CancelFn   = std::function<bool()>;         // polled per chunk; true aborts the render
    // One console write the worker made (console.log and the rest, each ending in a newline), `error` for the stderr
    // ones (console.warn / error / trace) and for a script exception's report.
    using ConsoleFn  = std::function<void(bool error, const char* text)>;

    struct Result {
        std::string status;                // "done" | "cancelled" | "error"; empty if the worker never reported
        std::string message;               // error detail (when status == "error")
        std::vector<std::string> outputs;  // WAV paths written (when status == "done")
        bool ok() const { return status == "done"; }
    };

    RenderHost();
    ~RenderHost();
    RenderHost(const RenderHost&) = delete;
    RenderHost& operator=(const RenderHost&) = delete;

    // Render one job. `jobJson` is a RenderOpts-shaped spec (at least {"rom":"...","out":"..."}); the worker
    // fills the CLI defaults for anything omitted. onRendered / isCancelled / onConsole are optional; with no
    // onConsole the worker's console writes go to the process's stdout and stderr. Returns the worker's reported
    // result (status "error" with a message on any internal failure).
    Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {},
               ConsoleFn onConsole = {});

    // Called by the bound JS thunks (which recover the host via the JS context opaque). Public so the
    // free-function thunks in the .cpp can reach them; not part of the external API.
    void onRendered(double ms);
    bool onCancelQuery();
    void onResult(std::string status, std::string message, std::vector<std::string> outputs);
    void onStdout(const char* text);
    void onStderr(const char* text);

private:
    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;

    RenderedFn renderedFn_;
    CancelFn   cancelFn_;
    ConsoleFn  consoleFn_;
    Result     result_;
};

} // namespace retroplug

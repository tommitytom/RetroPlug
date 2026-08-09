#pragma once

// RenderHost — a self-contained, headless audio-render environment: a BARE QuickJS runtime (no txiki, no
// libuv) + its own Engine + the in-process RPC bridge, running the render-worker bundle
// (packages/retroplug/src/render/worker.ts) as global-code bytecode. It is the "retroplug-cli render"
// pipeline, but embeddable and driven synchronously on the calling thread — designed to run on a dedicated
// worker thread (one per job; see RenderJobRegistry), so the plugin can render a fresh system to disk
// without touching the live audio-thread cores.
//
// One-shot: construct, run() once, destroy. run() blocks for the whole render. Only console +
// TextEncoder/TextDecoder + the RPC / rendered / cancel / result thunks are bound (the render path needs
// nothing else from the runtime). See ClassIdSpace note: bare QuickJS with no fresh native-class
// registration, so the multi-txiki-runtime class-id hazard does not apply here.

#include <functional>
#include <string>
#include <vector>

// Match quickjs.h's opaque tags so this header stays QuickJS-free (mirrors DspRuntime.hpp).
struct JSRuntime;
struct JSContext;

namespace retroplug {

class RenderHost {
public:
    using RenderedFn = std::function<void(double)>;  // audio rendered so far, in milliseconds
    using CancelFn   = std::function<bool()>;         // polled per chunk; true aborts the render

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
    // fills the CLI defaults for anything omitted. onRendered / isCancelled are optional. Returns the
    // worker's reported result (status "error" with a message on any internal failure).
    Result run(const std::string& jobJson, RenderedFn onRendered = {}, CancelFn isCancelled = {});

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
    Result     result_;
};

} // namespace retroplug

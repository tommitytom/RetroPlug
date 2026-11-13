#include "host/SessionPump.hpp"

#include <cstdio>

namespace rp::host {
namespace {

/** Set by the stop takeover below. It is a namespace-level flag rather than a member because the async callback is a
 *  plain function pointer with one argument, the handle, and because there is one session runtime for a process's life
 *  (the same reason the CLI kept its exit state in a TU-local before this). */
bool g_stopRequested = false;

}  // namespace

SessionPump::SessionPump(TjsHostRuntime& host) : host_(&host) {
    // txiki's own callback (src/vm.c) is `uv_stop(&qrt->loop)`; the flag is all this adds. TJS_Stop records the same
    // request itself (TJS_StopRequested), which is what catches one made in the turn the loop is already stopping: the
    // async handle is then never serviced.
    JSContext* ctx = host.context();
    // A host with no context has no runtime to drive, so there is nothing to hook; a pump is harmless then, and the
    // destructor and stopRequested() guard the same way rather than dereferencing null.
    if (!ctx) return;
    TJS_GetRuntime(ctx)->stop.async_cb = [](uv_async_t* handle) {
        g_stopRequested = true;
        uv_stop(handle->loop);
    };
    // The exit hook: what a session's tjs.exit does instead of ending the process here (section 8).
    TJS_SetExitHook(TJS_GetRuntime(ctx), &SessionPump::onExit, this);
    // The refused `tjs.exit` override: a host may still set one so a session's exit code is recorded rather than the
    // process ending inside the call, and txiki freezes `tjs` before an embedder can wrap it, so the set fails with a
    // TypeError (JS_PROP_THROW) that STAYS PENDING on the context for the whole run. It is not the session's error - a
    // pump that took it for one would fail the run on its first turn - so it is cleared here, once. Both hosts deleted
    // their override when they moved onto the hook; this is the belt for one that still sets one.
    if (JS_HasException(ctx)) JS_FreeValue(ctx, JS_GetException(ctx));
}

SessionPump::~SessionPump() {
    // The hook's opaque is this object, and the runtime outlives it (both mains declare the pump after the host and
    // before the runtime's teardown): leave a hook that records nothing rather than a pointer to what is being
    // destroyed. A tjs.exit reached from a provider's late callback then still unwinds instead of calling exit() in the
    // middle of the teardown. A host that shut its runtime down first has no hook to replace.
    if (JSContext* ctx = host_->context()) TJS_SetExitHook(TJS_GetRuntime(ctx), &SessionPump::onExitAbandoned, nullptr);
}

bool SessionPump::onExit(TJSRuntime* qrt, int status, bool errorPending, void* opaque) {
    auto* self = static_cast<SessionPump*>(opaque);
    if (self->exitTaken_) return true;  // the FIRST exit wins: a second may not rewrite the code or the verdict
    self->exitTaken_ = true;
    self->exitCode_  = status;
    if (errorPending) {
        // A sibling callback in this turn threw, and the session asked to exit with that error still pending. txiki is
        // about to replace it with the exit's own uncatchable marker, so it is dumped HERE: the session fails (exit 1)
        // with what was actually thrown, instead of exiting 0 and never printing it.
        tjs_dump_error(TJS_GetJSContext(qrt));
        self->threw_ = true;
    } else if (self->stopRequested()) {
        // A sibling callback threw and another one's try/catch cleared the exception before this exit was asked for, so
        // no exception is pending. txiki's stop request survives where the exception does not, and the takeover's
        // callback has not run yet (the loop is stopping), so this is the earlier request: the session failed, and this
        // says so in the words the CLI's own cleared-error path uses.
        std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
        self->threw_ = true;
    }
    return true;  // taken: tjs.exit unwinds rather than ending the process
}

bool SessionPump::onExitAbandoned(TJSRuntime*, int, bool, void*) { return true; }

bool SessionPump::pumpOnce() {
    JSContext* ctx = host_->context();
    if (!ctx) return threw_;  // a host that shut its runtime down has no session left to report on
    host_->pump();
    if (exitTaken_ && JS_HasException(ctx)) {
        // The exit's uncatchable error IS the unwinding, not a failure: the hook has already decided (threw_, from what
        // was pending when exit was called), so the marker is cleared and the turn reports the clean end.
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    if (threw_) return true;
    if (exitTaken_) return false;  // a clean exit: the loop is stopping and the host returns exitCode()
    if (JS_HasException(ctx)) {
        tjs_dump_error(ctx);
        threw_ = true;
    } else if (stopRequested()) {
        std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
        threw_ = true;
    }
    return threw_;
}

bool SessionPump::stopRequested() const {
    JSContext* ctx = host_->context();
    return g_stopRequested || (ctx && TJS_StopRequested(TJS_GetRuntime(ctx)));
}

}  // namespace rp::host

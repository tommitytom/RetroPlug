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
    // txiki's own callback (src/vm.c) is `uv_stop(&qrt->loop)`; the flag is all this adds.
    TJS_GetRuntime(host.context())->stop.async_cb = [](uv_async_t* handle) {
        g_stopRequested = true;
        uv_stop(handle->loop);
    };
    // The refused `tjs.exit` override: both hosts set their own so a session's exit code is recorded rather than the
    // process ending inside the call, and txiki freezes `tjs` before an embedder can wrap it, so the set fails with a
    // TypeError (JS_PROP_THROW) that STAYS PENDING on the context for the whole run. It is not the session's error - a
    // pump that took it for one would fail the run on its first turn - so it is cleared here, once. From section 8's
    // exit hook on the override is deleted (it never takes) and this is the belt for a host that still sets one.
    if (JS_HasException(host_->context())) JS_FreeValue(host_->context(), JS_GetException(host_->context()));
}

SessionPump::~SessionPump() = default;

bool SessionPump::pumpOnce() {
    JSContext* ctx = host_->context();
    host_->pump();
    if (JS_HasException(ctx)) {
        tjs_dump_error(ctx);
        threw_ = true;
    } else if (g_stopRequested) {
        std::fprintf(stderr, "error: a callback threw, and its error was cleared before it could be reported\n");
        threw_ = true;
    }
    return threw_;
}

bool SessionPump::stopRequested() const { return g_stopRequested; }

}  // namespace rp::host

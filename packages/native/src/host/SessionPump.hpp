#pragma once

// One session runtime's loop, shared by the two hosts that drive txiki themselves: retroplug-cli and retroplug-host
// (docs/known-bugs-batch-3-design.md section 7). txiki turns a throw nothing caught - in a timer or another callback it
// invoked, in a microtask job, in an async module body - into a pending exception and a request to stop ITS loop. A host
// that drives that loop by hand (TjsHostRuntime::pump) has to look for both, or the error is dropped: a batch session
// would say only that it did not complete, and a kept-alive one would pump until its deadline with no reason printed.
//
// The stop request is an async handle on the runtime whose callback stops txiki's loop (src/vm.c's uv__stop). It is
// TAKEN OVER here - the callback keeps that one line and records that it fired - because the request outlives the
// exception it came with: a sibling callback in the same loop turn that throws and catches anything replaces, and so
// clears, the pending exception, and the loop request is then the only witness that something threw. Every request on
// the session runtime is that kind of error path (a callback's throw, a failed job, an unhandled rejection nothing
// cancelled); nothing benign stops the main runtime's loop. TJS_StopRequested asks txiki itself as well, which is the
// form that survives a request made in the turn the loop is already stopping (the handle is then never serviced).
//
// Each host keeps its own LOOP - the CLI's SIGINT handling and unbounded keep-alive, the test host's bounded spin and
// its deadline - and asks this for one turn of it.
//
// THE EXIT (section 8) is the pump's too: txiki freezes `tjs` before an embedder can wrap it, and its tjs.exit called C
// exit() from inside the JS call, so a session that exited normally ended the process with main's frame - the launcher's
// Teardown, the providers, the runtime - never unwinding (a claimed Launchpad never got its close bytes). The pump
// installs txiki's exit HOOK instead: tjs.exit records its status here and throws an uncatchable error, so the session
// unwinds like a thrown one and both hosts' mains return. A host that installs no hook (the plugin, the SDL standalone)
// keeps the old exit.

#include "dpfjs/host/TjsHostRuntime.hpp"  // the shared txiki/QuickJS host (+ tjs.h/quickjs.h)

namespace rp::host {

class SessionPump {
public:
    /** Install the stop takeover and the exit hook on `host`'s runtime, and clear what a refused `tjs.exit` override
     *  left pending. One pump per session runtime, which is one per process in both hosts. Constructed BEFORE the
     *  module is evaluated and AFTER the host's globals are installed. A host whose runtime is already gone gets no
     *  hook and a pump that reports nothing, rather than a dereference of its null context. */
    explicit SessionPump(TjsHostRuntime& host);
    /** Leave the runtime a hook that records nothing, so a `tjs.exit` reached during the teardown does not follow a
     *  pointer to this object. */
    ~SessionPump();
    SessionPump(const SessionPump&)            = delete;
    SessionPump& operator=(const SessionPump&) = delete;

    /** One turn of the loop: run what is ready, then report whether the session ended in an ERROR. An exception is
     *  pending: it is dumped to stderr here. A stop was requested with none pending: reported here, in the launcher's
     *  words, because a sibling callback cleared the error. An exit the hook took is a clean end instead (unless an
     *  error was pending when it was asked for, which is one). Latched: once the session threw, every later turn says
     *  so. */
    bool pumpOnce();

    /** Whether the session has thrown since this pump was made (what a loop's condition tests). */
    bool threw() const { return threw_; }

    /** Whether a session's tjs.exit was TAKEN by the hook: the loop is stopping and main unwinds, so the host's teardown
     *  runs. Read the code with exitCode(). */
    bool exitTaken() const { return exitTaken_; }

    /** The status the session exited with (0 unless tjs.exit was given one). */
    int exitCode() const { return exitCode_; }

    /** Whether txiki has asked its loop to stop since startup. */
    bool stopRequested() const;

private:
    /** txiki's exit hook (TJSExitHook, tjs.h): record the status, dump the error that was already pending so the exit
     *  does not swallow it, and say the exit was taken (which makes tjs.exit unwind instead of ending the process). The
     *  FIRST exit wins: a second one may not rewrite the code or the verdict. */
    static bool onExit(TJSRuntime* qrt, int status, bool errorPending, void* opaque);
    /** What ~SessionPump leaves in the hook's place: it records nothing and still unwinds. Not nullptr, which would put
     *  the stock exit() back in the middle of the hosts' teardown. */
    static bool onExitAbandoned(TJSRuntime* qrt, int status, bool errorPending, void* opaque);

    TjsHostRuntime* host_;
    bool            threw_     = false;
    bool            exitTaken_ = false;
    int             exitCode_  = 0;
};

}  // namespace rp::host

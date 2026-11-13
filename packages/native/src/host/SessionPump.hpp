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
// cancelled); nothing benign stops the main runtime's loop.
//
// Each host keeps its own LOOP - the CLI's SIGINT handling and unbounded keep-alive, the test host's bounded spin and
// its deadline - and asks this for one turn of it.

#include "dpfjs/host/TjsHostRuntime.hpp"  // the shared txiki/QuickJS host (+ tjs.h/quickjs.h)

namespace rp::host {

class SessionPump {
public:
    /** Install the stop takeover on `host`'s runtime and clear what a refused `tjs.exit` override left pending. One
     *  pump per session runtime, which is one per process in both hosts. Constructed BEFORE the module is evaluated and
     *  AFTER the host's globals are installed. */
    explicit SessionPump(TjsHostRuntime& host);
    ~SessionPump();
    SessionPump(const SessionPump&)            = delete;
    SessionPump& operator=(const SessionPump&) = delete;

    /** One turn of the loop: run what is ready, then report whether the session ended in an ERROR. An exception is
     *  pending: it is dumped to stderr here. A stop was requested with none pending: reported here, in the launcher's
     *  words, because a sibling callback cleared the error. Latched: once the session threw, every later turn says so. */
    bool pumpOnce();

    /** Whether the session has thrown since this pump was made (what a loop's condition tests). */
    bool threw() const { return threw_; }

    /** Whether txiki has asked its loop to stop since startup. */
    bool stopRequested() const;

private:
    TjsHostRuntime* host_;
    bool            threw_ = false;
};

}  // namespace rp::host

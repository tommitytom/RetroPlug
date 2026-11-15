#pragma once

// The plugin lifecycle's handoff policy: what a buffer-size or sample-rate change does when a host runs it on a thread
// of its own, and how the engine gets the quiescent moment the change needs.
//
// DPF's JACK host runs deactivate(), the change and activate() from JACK's notification callback
// (DistrhoPluginJACK.cpp's jackBufferSizeCallback / jackSampleRateCallback -> DistrhoPluginInternal.hpp's
// PluginExporter::setBufferSize / setSampleRate), and VST2 runs the same sequence from whichever thread its host
// dispatches the block-size and sample-rate opcodes on. NONE of the engine work those callbacks used to do is safe
// there: the ownership flip and the drains race the editor's per-frame reclaim and its edits on the control thread,
// and updateLatency / updateParameterMap call into the QuickJS context that thread pumps every frame. PipeWire changes
// its quantum whenever another client asks for a different latency, so this is routine on a PipeWire desktop, not the
// rare manual case (spec/02-native-host.md, the plugin lifecycle's thread rule).
//
// The policy splits the work by what the change actually needs, and never touches the engine on a foreign thread:
//
//  - **A size the plan already holds.** The Engine's plan starts at Engine::kDefaultMaxBlockFrames (8192) and never
//    shrinks, and nothing on the control plane can grow it while the audio thread owns the engine
//    (GraphHost::reactivate refuses). So a foreign buffer-size change within that maximum needs nothing at all: the
//    engine never leaves the audio thread, so there is no ownership flip, no drain, no reclaim and no JS, and the next
//    run() drains the ring as usual. That is every PipeWire quantum change.
//  - **A rate change, or a size above the plan.** Nothing can apply it while the audio thread runs the engine, so
//    run() PARKS: it writes silence, holds its block's MIDI in a fixed buffer (a note-off is not lost), and touches
//    nothing. The control thread's idle tick - the one hook both hosts call every frame, the editor's
//    reclaimReleased - then does the whole quiescent handoff: take the engine back, drain, reclaim, apply the rate and
//    the size, hand it back, report latency and re-label the CC map.
//
// WHAT OWNS THE ENGINE, which the two tiers above both turn on. There are two facts, and the policy tracks them
// separately: `engineGiven_` (the audio thread may run it) and the state (whether a block may drive it). A block drives
// the engine only when the state is Running, and the state is only Running when the engine is given, so the two cannot
// disagree in that direction. The direction that matters is the other one, and it is what a lifecycle call may not get
// wrong: a format can move the engine between them on a foreign thread.
//
//  - **A foreign activate when the engine is the CONTROL thread's** (a control-thread deactivate before it, or the
//    plugin's very first activate reaching the audio thread through DPF's run(), which activates when fIsActive is
//    false, DistrhoPluginInternal.hpp's PluginExporter::run; VST3's setProcessing from the processing thread and VST2's
//    SUSPEND(1) reach it too). Giving the engine to a thread that is already running, without the quiescent handoff,
//    would put two threads in the command and release rings. So it REQUESTS the give: the park runs on the next block
//    and the tick gives from the control thread.
//  - **A foreign deactivate that nothing follows** (a VST2 SUSPEND(0) off the host's main thread). DPF promises no
//    run() after a deactivate, so the engine would sit audio-owned with nobody draining: the ring fills and every edit
//    answers busy. The foreign call parks, and once the deactivate has stood for kDeactivateGrace the tick TAKES the
//    engine and keeps it (state Suspended: no give, no resume), so an edit applies inline while the plugin is
//    suspended. The grace period exists because a format's deactivate and activate are two calls, not one: JACK's and
//    VST2's pairs take the mutex separately, a tick may land between them, and the pair needs nothing at all if it is
//    allowed to finish. A paired activate cancels the request, so the routine JACK callback costs nothing.
//  - **A control-thread deactivate** takes the engine and keeps it too, as every format's did before this policy: the
//    state goes Suspended, and an idle tick must not hand it back to a thread that is not running. The activate that
//    follows gives it again.
//
// The handshake is a release/acquire flag. run() reads the state with acquire and stores Parked (release) only on
// ENTRY to a later block, so a block that stored Parked has already declined to touch the engine and the tick, which
// waits for Parked, cannot overlap one that did. It therefore does not rely on JACK's promise that process does not run
// during the callback.
//
// One mutex serialises the foreign lifecycle calls against the tick. The audio thread reaches it too, through the
// activates DPF makes on it (run()'s auto-activate, VST3's setProcessing from the processing thread, an AU reset on the
// render thread), and it never WAITS for it: an activate or a deactivate from a thread that is not the policy's control
// thread tries the lock, and one that cannot have it records itself in an atomic word that the next entry owning the
// lock applies - the tick, or the call that got the lock next, which applies it before its own, so a deferred call is
// always ordered before the direct one that follows it. A block is therefore never held up by the engine work - or the
// JS of updateControlPlane - that the control thread is doing. The tick itself is the control thread's alone: a host
// that calls getState from another thread (VST2's effGetChunk can) applies nothing there, and the park waits for a call
// that belongs to the thread that owns the engine. The thread that
// owns the engine is the one that constructed the policy, so a host that MOVES its main thread would read as foreign
// everywhere and never apply a park: DPF's formats all construct and idle on the main thread, which is the premise this
// rests on, and the one thing the policy can do for a host that does not is say the park is overdue.
//
// A host with no idle tick at all (a VST2 host that changes the rate off its main thread with the editor closed) is
// covered by getState and setState, which ask the policy to apply a pending park; their tick is the one that never runs
// the grace-suspend, because a suspend made from a state call is undone by nothing (see stateTick). The editor's close
// runs one idle tick, so it applies a park a block has already seen, or settles a foreign deactivate whose grace has
// run out on it (the tick that may suspend), and nothing else besides the overdue report: a park whose first block has
// not come (and is not a foreign deactivate's) is Requested, and the tick may not take an engine a block may be in, and
// a Suspended engine is left suspended, because a grace-suspend is undone by nothing but an activate. What remains
// uncovered is a foreign activate with no editor open after it and no state call either: that plugin parks, silent,
// until one comes, or the editor reopens. A park nobody applies is reported once after a second, from whichever
// control-thread entry comes next (there is nothing else that could say it).
//
// Nothing here touches DPF, so the policy is unit-tested without it (test/plugin/LifecycleHandoff.test.cpp) and
// PluginDSP only implements HandoffHost over its engine and forwards its four lifecycle overrides.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace rp::plugin {

// The engine work a lifecycle change needs done, and what the policy may assume about the engine's plan. Every method
// runs on the CONTROL thread, in the tick that applies a park (or in the lifecycle call itself when that call already
// came from the control thread).
class HandoffHost {
public:
    virtual ~HandoffHost() = default;

    // Take the engine from the audio thread: the ownership flip and the command ring drained into the engine.
    // Quiescent only (no run() in flight, and none starting).
    virtual void takeEngine() = 0;

    // Free the plans the audio thread handed back; returns how many. Quiescent or not.
    virtual std::size_t reclaimReleases() = 0;

    // Hand the engine to the audio thread: from here every control-plane edit is push-only, drained by run().
    virtual void giveEngine() = 0;

    // Grow the plan so the host's blocks fit. A shrink is the Engine's to ignore, never this policy's to ask for.
    virtual void setMaxBlockFrames(std::uint32_t frames) = 0;

    // Re-rate the live cores. Quiescent only (Engine::setSampleRate).
    virtual void setSampleRate(double rate) = 0;

    // Report the project's latency to the host and re-label the CC map: both call into the shared script context, so
    // control thread only.
    virtual void updateControlPlane() = 0;

    // A park this host never got round to applying, said once per park. Nothing to say in a unit test's fake.
    virtual void parkOverdue(double /*seconds*/) {}

    // MIDI a parked block could not hold (LifecycleHandoff's fixed buffer), said on the CONTROL thread once the block
    // that parked is done with: the audio thread only counted it, and a log line is not its to write.
    virtual void midiDropped(std::uint64_t /*count*/) {}
};

// The MIDI a parked block held, so a note-off the host sent while parked still reaches the cart. Fixed capacity,
// written by the audio thread only, and it never allocates: past the capacity a message is dropped and counted.
class HeldMidi {
public:
    static constexpr int kCapacity = 256;
    // DPF's inline MIDI data: a message longer than this (a SysEx) is kept in dataExt, and one of those is not held.
    static constexpr std::uint32_t kMaxBytes = 4;

    struct Event {
        std::uint8_t size = 0;
        std::uint8_t data[kMaxBytes] = {};
    };

    // Hold one message; false when it did not fit or was not a held size (both counted as dropped).
    bool push(const std::uint8_t* bytes, std::uint32_t size) {
        if (bytes == nullptr || size == 0 || size > kMaxBytes || count_ >= kCapacity) {
            ++dropped_;
            return false;
        }
        Event& e = events_[count_++];
        e.size = static_cast<std::uint8_t>(size);
        std::memcpy(e.data, bytes, size);
        return true;
    }

    int count() const { return count_; }
    const Event& at(int i) const { return events_[i]; }
    void clear() { count_ = 0; }
    std::uint64_t dropped() const { return dropped_; }

private:
    Event          events_[kCapacity];
    int            count_   = 0;
    std::uint64_t  dropped_ = 0;
};

// The policy. Construct it on the thread DPF builds the plugin on, which is the control thread it also runs the first
// activate() on.
class LifecycleHandoff {
public:
    // Running: no block is parked and the audio thread may drive the engine.
    // Requested: a park has been asked for and no block has seen it yet.
    // Parked: a block has stored that it declined to drive the engine, which is the moment the tick may take it.
    // Suspended: the engine is the control thread's and must stay so until an activate. A format's deactivate, either
    // thread, leaves this state (see the header): a block parks, and an idle tick does nothing about it.
    enum class State { Running, Requested, Parked, Suspended };

    LifecycleHandoff(HandoffHost& host, std::thread::id controlThread, std::uint32_t planMaxBlockFrames)
        : host_(host), controlThread_(controlThread), planFrames_(planMaxBlockFrames) {}

    // --- the DPF overrides, on whichever thread the format calls them ----------------------------------------------
    //
    // activate and deactivate, the two DPF can call ON the audio thread, never wait for the mutex: a call that cannot
    // have it records itself for the tick (recordDeferred) and returns. A block's deadline is not a place to wait for
    // the control thread to finish a take, a reclaim or the JS under it. Every entry that owns the lock applies a
    // recorded call BEFORE its own (applyDeferredLocked), so a deferred call keeps its place in front of the call that
    // got the lock a moment after it.

    // DPF calls this on the control thread before an activate (and a VST2 host may not, when it suspends).
    void deactivate() {
        std::unique_lock<std::mutex> lock = lockForCall();
        if (!lock.owns_lock()) {
            recordDeferred(kDeferDeactivate, 0);
            return;
        }
        applyDeferredLocked();  // a call recorded before this one is ordered before it
        deactivated_ = true;    // either thread: until an activate, nothing may give the engine back
        if (onControlThread()) {
            // Quiescent by the format's contract, and the engine stays this thread's until the activate that follows:
            // today's behaviour, and what makes an edit apply inline while the plugin is suspended.
            if (engineGiven_) {
                host_.takeEngine();
                host_.reclaimReleases();
                engineGiven_ = false;
            }
            state_.store(State::Suspended, std::memory_order_release);
            return;
        }
        // Foreign: DPF promises no run() after this until an activate, so the engine can be taken back here - but not
        // at once, because the activate half of the format's pair may be a moment behind and needs nothing at all.
        deactivatedAt_ = Clock::now();
        requestParkLocked();
    }

    // `frames` is the host's buffer size at this activation (DPF's getBufferSize), the plan's first input.
    void activate(std::uint32_t frames) {
        std::unique_lock<std::mutex> lock = lockForCall();
        if (!lock.owns_lock()) {
            recordDeferred(kDeferActivate, frames);
            return;
        }
        applyDeferredLocked();  // as in deactivate: a pair keeps the order its format sent it in
        deactivated_ = false;   // an activate pairs a foreign deactivate, if there was one: nothing to take back
        if (!onControlThread()) {
            activateForeignLocked(frames);
            return;
        }
        // On the control thread this is quiescent whatever happened before it: DPF runs deactivate first, and a
        // format that skips it still promised no run() before activate.
        if (engineGiven_) {
            host_.takeEngine();
            host_.reclaimReleases();
        }
        planFrames_ = std::max(planFrames_, frames);
        host_.setMaxBlockFrames(frames);
        applyPendingLocked();
        host_.giveEngine();
        engineGiven_ = true;
        // Running before the two calls below, so a block that starts while they run drives an engine that is already
        // handed over and applied (nothing but the state gates a block). Both are the control plane's - a latency
        // report that may call back into the host, a CC map that re-labels parameters - so the policy's mutex is let
        // go first: the engine work above is what a foreign lifecycle call has to wait for, and this is not it.
        resumeLocked();
        lock.unlock();
        reportDroppedLocked();
        host_.updateControlPlane();
    }

    void bufferSizeChanged(std::uint32_t frames) {
        std::lock_guard<std::mutex> lock(lifecycle_);
        applyDeferredLocked();  // a call recorded before this one is ordered before it
        // DPF reaches this only while the plugin is inactive, so a control-thread call that finds the engine GIVEN is a
        // call during a FOREIGN deactivate nothing has followed yet: growing the plan there would be a change to an
        // engine a block may be in the middle of, and the graph refuses one while the audio thread owns it
        // (GraphHost::reactivate). Recorded instead, and the park this requests hands it to the take that applies it.
        if (onControlThread() && !engineGiven_) {
            // Quiescent, so the plan can grow here; a size a deferred activate recorded before this call is the same
            // grow and is taken with it, rather than left for a take that need not come.
            const std::uint32_t wanted = hasPendingSize_ ? std::max(frames, pendingFrames_) : frames;
            hasPendingSize_ = false;
            pendingFrames_  = 0;
            planFrames_     = std::max(planFrames_, wanted);
            host_.setMaxBlockFrames(wanted);
            return;
        }
        // Tier 1: the plan already holds a block this long, so nothing has to move. The next run() gets the block and
        // the ring drains as usual; the plan is untouched because the engine never left the audio thread.
        if (frames <= planFrames_) return;
        hasPendingSize_ = true;
        pendingFrames_  = frames;
        requestParkLocked();
    }

    void sampleRateChanged(double rate) {
        std::lock_guard<std::mutex> lock(lifecycle_);
        applyDeferredLocked();  // a call recorded before this one is ordered before it
        // As in bufferSizeChanged: a control-thread change with the engine given is a change during a foreign
        // deactivate's grace. Engine::setSampleRate re-rates the live cores, which needs the quiescent take, so the
        // rate waits for it - the activate that follows applies it, or a tick does.
        if (onControlThread() && !engineGiven_) {
            hasPendingRate_ = false;
            host_.setSampleRate(rate);
            return;
        }
        hasPendingRate_ = true;
        pendingRate_    = rate;
        requestParkLocked();
    }

    // --- the audio thread ----------------------------------------------------------------------------------------

    // True when run() may drive the engine for this block. False while a park is requested, applied or suspended: the
    // caller writes silence and holds its MIDI, and touches nothing. Stores Parked on entry, so a tick that sees Parked
    // knows this block and every block before it have finished with the engine; the exchange only ever moves
    // Requested -> Parked, so it cannot overwrite a Running a control-thread activate has just stored.
    bool beginBlock() {
        State state = state_.load(std::memory_order_acquire);
        if (state == State::Running) return true;
        if (state == State::Requested) {
            State expected = State::Requested;
            state_.compare_exchange_strong(expected, State::Parked, std::memory_order_release,
                                           std::memory_order_acquire);
        }
        return false;
    }

    // Hold one message for the resumed block. Audio thread, parked blocks only. A message the fixed buffer cannot keep
    // is counted here and said by the control thread's next tick (see HandoffHost::midiDropped).
    void holdMidi(const std::uint8_t* bytes, std::uint32_t size) {
        if (!held_.push(bytes, size)) heldDropped_.fetch_add(1, std::memory_order_relaxed);
    }

    const HeldMidi& held() const { return held_; }

    // Visit the MIDI a park held, as frame 0 of the block that resumes (a note-off is not lost), and clear it. Called
    // by the first block beginBlock() lets through, so on the audio thread.
    template <typename Push>
    void replayHeld(Push&& push) {
        if (held_.count() == 0) return;
        for (int i = 0; i < held_.count(); ++i) push(held_.at(i).data, held_.at(i).size);
        held_.clear();
    }

    // --- the control thread's idle ----------------------------------------------------------------------------------

    // The editor's per-frame tick: reclaim what the audio thread released, and apply the handoff a park has left
    // pending. Returns how many plans it freed, for the idle reclaim probe. The CONTROL thread's alone: every engine
    // call it makes, and the JS in the report, belongs to the thread that owns the engine, so a call from another
    // thread applies nothing and leaves the park pending for this one. It does say an overdue park, from any thread: a
    // host that moved its main thread has no other tick, and a park nobody applies is worth a log line wherever it is
    // noticed.
    std::size_t idleTick() { return tick(true); }

    // The state seams' tick (getState and setState), for a host with no idle tick at all. It applies a park that is
    // WAITING - a rate, a size, the give - and never runs the grace-suspend: a host that saves while it is paused would
    // otherwise come back to a plugin that parks every block until its NEXT save, since DPF reports a rate or a size
    // only while the plugin is inactive. Only a tick that keeps coming may make a park that only another tick can undo.
    std::size_t stateTick() { return tick(false); }

    bool parked() const { return state_.load(std::memory_order_acquire) != State::Running; }

    State state() const { return state_.load(std::memory_order_acquire); }

private:
    using Clock = std::chrono::steady_clock;

    // How long a foreign deactivate may stand before the tick takes the engine back and keeps it. Long enough for the
    // activate half of a JACK or VST2 pair, which are two calls and not one, and short enough that a suspended plugin
    // gets its inline edits back well inside a UI frame's worth of time.
    static constexpr std::chrono::milliseconds kDeactivateGrace{250};

    bool onControlThread() const { return std::this_thread::get_id() == controlThread_; }

    // The lock activate and deactivate take: this thread's own calls may WAIT for it (they own the engine, and DPF
    // brackets them, so there is nothing foreign to wait for but another of the host's own callbacks), and a foreign
    // one may not (it can be the audio thread itself), so it tries instead and defers.
    std::unique_lock<std::mutex> lockForCall() {
        if (onControlThread()) return std::unique_lock<std::mutex>(lifecycle_);
        return std::unique_lock<std::mutex>(lifecycle_, std::try_to_lock);
    }

    // The two ticks, which differ only in whether they may suspend (see idleTick and stateTick).
    std::size_t tick(bool allowSuspend) {
        if (!onControlThread()) {
            std::lock_guard<std::mutex> lock(lifecycle_);
            noteOverdueLocked();
            return 0;
        }
        std::unique_lock<std::mutex> lock(lifecycle_);
        applyDeferredLocked();  // a lifecycle call the audio thread could not make, before this tick's own work
        noteOverdueLocked();
        const State state = state_.load(std::memory_order_acquire);
        if (state == State::Running) {
            // A drop a parked block counted is said by the first control-thread entry, not only by a handoff: a park a
            // format's activate pairs again resumes with no handoff to report it.
            const std::size_t freed = host_.reclaimReleases();
            reportDroppedLocked();
            return freed;
        }
        if (state == State::Suspended) return 0;  // the engine is this thread's already, and stays so until an activate
        if (deactivated_) {
            if (!allowSuspend) return 0;  // left as it is, for the activate that follows or a tick that keeps coming
            // The format says no run() is coming, but its activate may still: give it kDeactivateGrace to arrive,
            // and then take the engine back and keep it (see the header).
            if (state == State::Requested && Clock::now() - deactivatedAt_ < kDeactivateGrace) return 0;
            return suspendLocked();
        }
        // A park for a rate or a size, with the engine still the audio thread's: a block must have seen the park
        // before the tick touches the engine.
        if (state != State::Parked) return 0;
        const std::size_t freed = handoffLocked();
        lock.unlock();  // as in activate: the report and the JS are the control plane's, not the policy's
        reportDroppedLocked();
        host_.updateControlPlane();
        return freed;
    }

    // An activate or deactivate a thread could not apply: it never waits for the mutex (the control thread's take, its
    // reclaim or its JS may hold it), so it records the call in one atomic word instead, and the next entry that owns
    // the lock applies it - a tick, or the call that got the lock a moment later, which applies it BEFORE its own work
    // so a format's pair keeps its order. One word, so recording is a single store and the LATEST request wins, which
    // is what a pair means: a deactivate then an activate collapses to the activate, exactly as the pair nets out.
    enum : std::uint64_t { kDeferDeactivate = 1, kDeferActivate = 2 };

    void recordDeferred(std::uint64_t kind, std::uint32_t frames) {
        deferred_.store((kind << 32) | frames, std::memory_order_release);
    }

    void applyDeferredLocked() {
        const std::uint64_t request = deferred_.exchange(0, std::memory_order_acquire);
        if (request == 0) return;
        // Applied with a FOREIGN call's meaning, whichever thread recorded it: it could not see the engine, so it may
        // not take it or treat it as quiescent. A deactivate starts its grace now, and an activate gives, grows the
        // plan and resumes exactly as the foreign branch of activate does.
        deactivated_ = (request >> 32) == kDeferDeactivate;
        if (deactivated_) {
            deactivatedAt_ = Clock::now();
            requestParkLocked();
            return;
        }
        activateForeignLocked(static_cast<std::uint32_t>(request & 0xFFFF'FFFFu));
    }

    // The activate of a thread that may not touch the engine: the control thread's take is what moves one, and this
    // asks for it. The caller has cleared deactivated_ already.
    void activateForeignLocked(std::uint32_t frames) {
        // A block longer than the plan has to grow it, and nothing may do that while the audio thread runs the engine.
        // DPF usually announces it through bufferSizeChanged first, but not always: setBufferSize returns early on an
        // unchanged size, so a host above the plan can reach only this call. Recorded before the engine check: DPF's
        // run() auto-activates a host's first block on the audio thread, engine not yet given.
        const bool overPlan = frames > planFrames_;
        if (overPlan) {
            hasPendingSize_ = true;
            pendingFrames_  = std::max(pendingFrames_, frames);
        }
        if (!engineGiven_ || overPlan) {
            // The engine is the control thread's (a deactivate before this, or this host's first activate): giving it
            // to a thread that is already running without the quiescent handoff would put two threads in the rings. Ask
            // for the give; the next block parks and the tick makes it (and grows the plan first).
            requestParkLocked();
            return;
        }
        // Audio-owned, so the engine never moved: the format's pair needs nothing, unless a size or a rate really is
        // pending, and then the park is already requested.
        if (!hasPendingSize_ && !hasPendingRate_) resumeLocked();
    }

    void requestParkLocked() {
        const State state = state_.load(std::memory_order_relaxed);
        if (state == State::Parked) return;  // already parked: the tick applies what it finds, whenever it comes
        if (state == State::Running) overdueReported_ = false;
        parkedAt_ = Clock::now();
        state_.store(State::Requested, std::memory_order_release);
    }

    void resumeLocked() {
        state_.store(State::Running, std::memory_order_release);
        overdueReported_ = false;
    }

    // The quiescent handoff, on the control thread. Only ever reached from Parked (the audio thread has finished with
    // the engine: it stored Parked with release, this read it with acquire).
    std::size_t handoffLocked() {
        std::size_t freed = 0;
        if (engineGiven_) {
            host_.takeEngine();
            freed = host_.reclaimReleases();
        }
        applyPendingLocked();
        host_.giveEngine();
        engineGiven_ = true;
        resumeLocked();
        return freed;
    }

    // The engine's the control thread's from here (a foreign deactivate nothing followed): take it if it is still the
    // audio thread's, reclaim what that thread handed back, and KEEP it. No give, no resume: a hand-off to a thread
    // that is not running would fill the ring with nobody draining, and the activate that comes later asks for it.
    std::size_t suspendLocked() {
        std::size_t freed = 0;
        if (engineGiven_) {
            host_.takeEngine();
            freed = host_.reclaimReleases();
            engineGiven_ = false;
        }
        state_.store(State::Suspended, std::memory_order_release);
        return freed;
    }

    void applyPendingLocked() {
        if (hasPendingRate_) {
            host_.setSampleRate(pendingRate_);
            hasPendingRate_ = false;
        }
        if (hasPendingSize_) {
            planFrames_ = std::max(planFrames_, pendingFrames_);
            host_.setMaxBlockFrames(pendingFrames_);
            hasPendingSize_ = false;
            pendingFrames_  = 0;
        }
    }

    // A message a parked block could not hold, said on this thread: the audio thread only counted it.
    void reportDroppedLocked() {
        if (heldDropped_.load(std::memory_order_relaxed) == 0) return;
        const std::uint64_t dropped = heldDropped_.exchange(0, std::memory_order_relaxed);
        host_.midiDropped(dropped);
    }

    // A park nobody applied within a second: a host with no idle tick and no state save. Said once, so a stuck plugin
    // cannot flood the log, and only from a later control-thread entry - nothing else could say it.
    void noteOverdueLocked() {
        // Only a park that is WAITING is overdue: a suspended plugin is exactly where a deactivate left it, and its
        // engine is the control thread's, so nothing is stuck.
        const State state = state_.load(std::memory_order_relaxed);
        if (overdueReported_ || (state != State::Requested && state != State::Parked)) return;
        const double seconds = std::chrono::duration<double>(Clock::now() - parkedAt_).count();
        if (seconds < 1.0) return;
        overdueReported_ = true;
        host_.parkOverdue(seconds);
    }

    HandoffHost&               host_;
    const std::thread::id      controlThread_;
    std::mutex                 lifecycle_;

    // Written on the control thread under lifecycle_ and by run() (Requested -> Parked only), read on the audio thread
    // without the mutex: an atomic, so a park that lands mid-block is seen by the next one and never torn. Suspended to
    // begin with, because nothing has given the engine to an audio thread yet: a block that ran before an activate
    // would be driving an engine nobody handed over. The first control-thread activate gives it, as it does from
    // anywhere else.
    std::atomic<State>         state_{State::Suspended};

    // Control thread under lifecycle_.
    std::uint32_t  planFrames_;
    bool           engineGiven_      = false;  // the audio thread may run the engine
    bool           deactivated_      = false;  // the format deactivated and no activate has come yet
    bool           hasPendingSize_   = false;
    std::uint32_t  pendingFrames_    = 0;
    bool           hasPendingRate_   = false;
    double         pendingRate_      = 0.0;
    Clock::time_point deactivatedAt_{};
    Clock::time_point parkedAt_{};
    bool           overdueReported_  = false;
    // Counted by parked blocks on the audio thread (several may run after the first one's CAS, and only that first
    // store is what the tick waits for), read and cleared by the control thread: a count is a count, so relaxed.
    std::atomic<std::uint64_t> heldDropped_{0};

    // Audio thread throughout: the parked blocks write it, and the first resumed block reads and clears it. Only the
    // FIRST parked block's write is ordered by an atomic: its CAS to Parked is the release the tick's acquire pairs
    // with. The later ones ride the host's own serialisation of its process calls - a VST3 or CLAP host may move blocks
    // between pool threads, but it never runs two at once, and the resumed block is after them in that order.
    HeldMidi       held_;

    // Written by an audio-thread activate or deactivate that could not have the mutex, read and cleared by the tick: an
    // atomic word (kind in the high half, the activate's frames in the low one), 0 for none.
    std::atomic<std::uint64_t> deferred_{0};
};

} // namespace rp::plugin

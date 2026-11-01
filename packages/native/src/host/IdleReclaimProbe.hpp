#pragma once

// The harness hook behind RETROPLUG_IDLE_RECLAIM_TEST: it holds a HOST to the idle tick it owes its engine
// (HostServices::reclaimOnIdle), which nothing outside the host's frame loop can see.
//
// The engine-graph suite holds what the tick does to the plans the block thread hands back; what it cannot reach is the
// two call sites - the plugin's editor idle and the SDL standalone's frame loop - because a host's frame loop is not a
// library a test can pump. So each host's own smoke arms this: it publishes a plan, publishes a replacement a few ticks
// later, and reports the first tick that freed the replaced one. A host whose loop stops calling the tick frees
// nothing, reports nothing, and goes red in `tools/run-sdl-smoke.sh` (SDL) or `tools/run-standalone.sh` (the plugin's
// editor). Inert, and costing one branch per frame, without the variable.

#include <cstddef>

#include "host/engine/Engine.hpp"

namespace retroplug {

/** A published plan is replaced by the next publish; the block thread hands the one it stopped running back, and the
 *  idle tick frees it. The probe drives that swap and reports what the tick did with it:
 *
 *    tick() < armAt>          the host's own frames; the probe does nothing
 *    tick() == armAt          publishes a stereo plan of its own
 *    tick() == armAt + 6      publishes the one that replaces it (the block thread hands the first back)
 *    then, the first tick that frees a plan is the pass: that idle tick reached the engine
 *
 *  Its two documents are the engine's own empty graph, renamed, so either plans on any host whatever the control plane
 * has published: no core, no resource, nothing but the host's stereo pair. It prints one line either way - the pass, or
 * the refusal that stopped it, or the give-up when no tick freed anything - and ok() reports the pass for a test. */
class IdleReclaimProbe {
public:
    IdleReclaimProbe() = default;

    // Arms the probe on `engine`, publishing its first plan at the idle tick numbered `armAt` (1 is the first one).
    void arm(Engine& engine, unsigned armAt);
    // The same, at the tick RETROPLUG_IDLE_RECLAIM_TEST names, which is how a host's smoke arms it. Inert when the
    // variable is unset or empty.
    void armFromEnv(Engine& engine);

    bool active() const { return engine_ != nullptr && step_ != Step::Passed && step_ != Step::Failed; }
    // Did an idle tick free the plan the swap replaced? False until one does.
    bool ok() const { return step_ == Step::Passed; }

    // One idle tick, called right after it: `freed` is what it reclaimed (HostServices::reclaimOnIdle's return).
    void tick(std::size_t freed);

private:
    enum class Step { Inert, Wait, First, Watch, Passed, Failed };

    static constexpr unsigned kReplaceAfter = 6;   // ticks between the plan and the one that replaces it
    static constexpr unsigned kGiveUpAfter  = 90;  // ticks past arming with nothing freed: the tick is not called

    void publish(const char* document);

    Engine*  engine_ = nullptr;
    Step     step_   = Step::Inert;
    unsigned armAt_  = 0;
    unsigned ticks_  = 0;
};

}  // namespace retroplug

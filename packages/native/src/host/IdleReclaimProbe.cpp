#include "host/IdleReclaimProbe.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace retroplug {

namespace {

// The engine's own empty graph (`kEmptyDocument`, Engine.cpp), and the same plan under another node id: both write the
// host's stereo pair and place nothing else, so either plans on any host whatever the control plane has published, and
// the second is a different document from the first (a swap, not a repeat of the plan already running).
constexpr const char* kFirstPlan =
    R"({"nodes":[{"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],"wires":[]})";
constexpr const char* kReplacement =
    R"({"nodes":[{"id":"probe-out","type":"rp.host.audio-out","settings":{"buses":[1,1]}}],"wires":[]})";

}  // namespace

void IdleReclaimProbe::arm(Engine& engine, unsigned armAt) {
    engine_ = &engine;
    armAt_  = armAt;
    step_   = Step::Wait;
}

void IdleReclaimProbe::armFromEnv(Engine& engine) {
    const char* at = std::getenv("RETROPLUG_IDLE_RECLAIM_TEST");
    if (at == nullptr || *at == '\0') return;
    arm(engine, static_cast<unsigned>(std::strtoul(at, nullptr, 10)));
}

void IdleReclaimProbe::publish(const char* document) {
    Engine::GraphRequest request;
    request.document = document;
    const Engine::GraphResult result = engine_->setGraph(request);
    if (result.ok) return;
    const std::string why = result.errors.empty() ? std::string("refused")
                                                  : result.errors.front().code + ": " + result.errors.front().message;
    std::fprintf(stderr, "[idle-reclaim] self-test FAILED: the probe's plan did not publish (%s)\n", why.c_str());
    step_ = Step::Failed;
}

void IdleReclaimProbe::tick(std::size_t freed) {
    switch (step_) {
        case Step::Inert:
        case Step::Passed:
        case Step::Failed:
            return;
        case Step::Wait:
            if (++ticks_ < armAt_) return;
            publish(kFirstPlan);
            step_ = Step::First;
            return;
        case Step::First:
            ++ticks_;
            if (ticks_ < armAt_ + kReplaceAfter) return;
            publish(kReplacement);
            step_ = Step::Watch;
            return;
        case Step::Watch:
            ++ticks_;
            // The idle tick this frame ran freed what the swap handed back: the tick reaches the engine in this host.
            // (Anything else that reclaims - a publish's own reclaim - would have freed it first, and this sees 0.)
            if (freed >= 1) {
                std::fprintf(stderr, "[idle-reclaim] self-test: tick %u freed %zu replaced plan(s)\n", ticks_, freed);
                step_ = Step::Passed;
                return;
            }
            if (ticks_ >= armAt_ + kGiveUpAfter) {
                std::fprintf(stderr,
                             "[idle-reclaim] self-test FAILED: no idle tick freed the replaced plan in %u ticks (the "
                             "host's frame loop does not call HostServices::reclaimOnIdle)\n",
                             ticks_ - armAt_);
                step_ = Step::Failed;
            }
            return;
    }
}

}  // namespace retroplug

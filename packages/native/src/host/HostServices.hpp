#pragma once

#include <cstddef>

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "host/rpc/HostRpcService.hpp"

/** The backend service graph every host stands up: one Engine (which knows no console: a system node carries the
 *  build function its core is built through), the ONE invoker, and the concern services over them.
 *
 *  Seven hosts composed this by hand - the plugin, the SDL standalone, the CLI, the Node addon, the test
 *  host, the render host and the UI test harness - and node/binding.cpp's comment already said its member
 *  order "mirrors cli/main.cpp's composition block", which is a duplication note in the code.
 *
 *  Deliberately codec-agnostic: no QuickJS, no N-API. That is what lets the Node addon share it, and it
 *  is the reason this is a plain struct rather than something that also owns a transport and a server.
 *  Member ORDER is load-bearing - the services hold references to the engine, so it must be constructed
 *  first and destroyed last.
 *
 *  It stops at the three services ALL seven hosts hold. DebugRpcService and AudioDriverRpcService are
 *  declared by the full-surface hosts themselves, and that is not tidiness: ~AudioDriverRpcService clears
 *  the invoker's audioThreadOwns bit and calls freePending() + reclaimReleased(). Merely OWNING one
 *  changes what happens at teardown, so handing it to the plugin and the SDL standalone - which
 *  deliberately mount neither facet - would alter their shutdown, in a DAW, on a path no test covers.
 *  A struct that is a superset of what a host needs is not free when its members have destructors. */
struct HostServices {
    Engine                engine;
    QueuedInvoker         invoker{engine};
    HostRpcService        host;
    EngineRpcService      engineSvc{engine, invoker};

    HostServices() = default;

    // The idle tick a host gives its engine between audio callbacks: free the plans the block thread has handed back
    // since the last one, with the nodes only they held (a cart node and the notes it holds on the cart, a core, the
    // resources they pin), on this thread, which is the one setGraph runs on. Every host with a frame loop calls it
    // each frame - the plugin's editor idle and the SDL standalone's loop, through this one seam - because without one
    // a replaced plan waits for the next publish. Returns how many plans it freed; with nothing handed back it
    // allocates nothing and takes no lock the block thread takes (Engine::reclaim).
    std::size_t reclaimOnIdle() { return invoker.reclaimReleased(); }

    HostServices(const HostServices&)            = delete;
    HostServices& operator=(const HostServices&) = delete;
};

#pragma once

// The rp.devtest.plan package: node types made to test a host that runs plans, the device check's image above all
// (packages/graph-runtime/device-check), whose plan needs a signal that is not silence, a coupled group and a way to
// read what reached the end, and which no portable rp.core or rp.dsp type gives it. No product that ships lists the
// package; the device check, the plan exporter's tool and the device-mode test do.
//
//   rp.devtest.source    a deterministic stereo signal, messages and control changes from a seed (xorshift32), so a
//                        plan has something to carry on every kind of port.
//   rp.devtest.digest    an FNV-1a digest of everything that reaches its audio, events and control inputs, which a host
//                        reads through get_interface(RP_DEVTEST_DIGEST_INTERFACE) as an rp_devtest_digest.
//   rp.devtest.lockstep  a coupled node (rp.step/1 and rp.link/1): each step advances it a few frames, mixing in what
//                        its peer last produced, so the order the round robin ran in is in its output.
//
// Nothing here names an SDK type but the entry's rp_package: the digest's view is a plain struct outside namespace rp,
// so a host built apart from the nodes (the device-mode test's two builds in one binary) reads it alike.

#include <cstdint>

#include "graph/rp_node.h"

// What a digest node has seen, as it counted it.
#define RP_DEVTEST_DIGEST_INTERFACE "rp.devtest.digest/1"

struct rp_devtest_digest {
    std::uint64_t hash = 1469598103934665603ull;  // FNV-1a 64 over every block's inputs
    std::uint32_t blocks = 0;
    bool          heard = false;  // anything but silence and empty queues reached it
};

namespace rp::devtest {

// How many rp.devtest.plan nodes are alive in this build: a host's unwinding is seen leaving none.
int live() noexcept;

// Counts itself alive for live(), as a member of each of the package's nodes: a test's own node may hold one too, to
// be counted with them.
struct Live {
    Live() noexcept;
    ~Live();
    Live(const Live&) = delete;
    Live& operator=(const Live&) = delete;
};

const rp_package* planPackage();

}  // namespace rp::devtest

// The package's entry, named in packages/devtest/package.json.
extern "C" const rp_package* rp_devtest_plan_package(void);

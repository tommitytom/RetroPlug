// retroplug-render-host-test — a thin harness that drives one RenderHost render from a JSON job spec, so a
// UI/background render can be proven byte-identical to `retroplug-cli render` (the same shared render
// library, run on a bare-QuickJS host instead of the txiki CLI process). Not shipped; built by name for the
// Phase-2 parity check (see packages/retroplug/scripts / the render parity test).
//
//   retroplug-render-host-test '{"rom":"resources/roms/mGB.gb","out":"/tmp/x.wav","durationMs":1200}'
//
// Exits 0 on a "done" result, 1 otherwise (printing status + message + the written output paths to stderr).

#include <cstdio>
#include <string>

#include "host/render/RenderHost.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json>\n");
        return 2;
    }

    retroplug::RenderHost host;
    double lastProgress = -1.0;
    retroplug::RenderHost::Result r = host.run(
        argv[1],
        [&lastProgress](double f) { lastProgress = f; }, // last-progress witness (proves onProgress fires)
        [] { return false; });                            // never cancel

    std::fprintf(stderr, "render-host-test: status=%s progress=%.3f message=%s outputs=%zu\n",
                 r.status.c_str(), lastProgress, r.message.c_str(), r.outputs.size());
    for (const std::string& p : r.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
    return r.ok() ? 0 : 1;
}

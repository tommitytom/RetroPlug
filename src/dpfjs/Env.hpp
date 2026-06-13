#pragma once

#include <cstdlib>
#include <string>

// Configurable env-var prefix for the dpf.js framework. Generic framework
// features (screenshot capture, dev bundle override) read `<PREFIX><suffix>`;
// the consumer injects its prefix at build time via -DDPFJS_ENV_PREFIX (e.g.
// RetroPlug passes "RETROPLUG_"). Defaults to "DPFJS_" for a bare framework.
//
// Consumer-domain env vars (RetroPlug's project/rom autoload, debug overlay,
// lifecycle trace) are NOT framework concerns — they keep their literal names.
#ifndef DPFJS_ENV_PREFIX
#define DPFJS_ENV_PREFIX "DPFJS_"
#endif

namespace dpfjs {

// getenv for a framework env var named `<DPFJS_ENV_PREFIX><suffix>`. Returns
// nullptr if unset. The returned pointer is owned by the environment (stable).
inline const char* getenvWithPrefix(const char* suffix) {
    const std::string name = std::string(DPFJS_ENV_PREFIX) + suffix;
    return std::getenv(name.c_str());
}

} // namespace dpfjs

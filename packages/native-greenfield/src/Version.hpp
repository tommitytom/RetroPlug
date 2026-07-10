#pragma once

// Single source of truth for the greenfield RetroPlug version. Consumed by:
//   - PluginDSP::getVersion() — DPF's numeric d_version()
//   - the menu chrome title             — via the version() RPC (HostRpcService)
// Bump these three numbers to release a new version; everything else follows.
#define RETROPLUG_GF_VERSION_MAJOR 0
#define RETROPLUG_GF_VERSION_MINOR 6
#define RETROPLUG_GF_VERSION_MICRO 2

// Stringized "MAJOR.MINOR.MICRO" (e.g. "0.6.2"). Two-step expansion so the macro *values* are
// stringized, not their names.
#define RETROPLUG_GF_VERSION_STR_(x) #x
#define RETROPLUG_GF_VERSION_STR(x)  RETROPLUG_GF_VERSION_STR_(x)
#define RETROPLUG_GF_VERSION_STRING                          \
    RETROPLUG_GF_VERSION_STR(RETROPLUG_GF_VERSION_MAJOR) "." \
    RETROPLUG_GF_VERSION_STR(RETROPLUG_GF_VERSION_MINOR) "." \
    RETROPLUG_GF_VERSION_STR(RETROPLUG_GF_VERSION_MICRO)

#pragma once

// Single source of truth for the RetroPlug version. Consumed by:
//   - PluginDSP::getVersion()       — DPF's numeric d_version()
//   - the standalone window title   — PluginUI (DGL window title)
//   - the menu chrome title         — via the getVersion() RPC
// Bump these three numbers to release a new version; everything else follows.
#define RETROPLUG_VERSION_MAJOR 0
#define RETROPLUG_VERSION_MINOR 6
#define RETROPLUG_VERSION_MICRO 2

// Stringized "MAJOR.MINOR.MICRO" (e.g. "0.6.2"). Two-step expansion so the
// macro *values* are stringized, not their names.
#define RETROPLUG_VERSION_STR_(x) #x
#define RETROPLUG_VERSION_STR(x)  RETROPLUG_VERSION_STR_(x)
#define RETROPLUG_VERSION_STRING                       \
    RETROPLUG_VERSION_STR(RETROPLUG_VERSION_MAJOR) "." \
    RETROPLUG_VERSION_STR(RETROPLUG_VERSION_MINOR) "." \
    RETROPLUG_VERSION_STR(RETROPLUG_VERSION_MICRO)

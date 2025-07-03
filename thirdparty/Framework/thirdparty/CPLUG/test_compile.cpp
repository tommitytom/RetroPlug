#define HOTRELOAD_BUILD_COMMAND ""
#define HOTRELOAD_LIB_PATH ""
#define HOTRELOAD_WATCH_DIR ""

#include "example/config.h"
#include "src/cplug_clap.c"
#include "src/cplug_vst3.c"
#ifndef __APPLE__
#include "src/cplug_standalone_win.c"

#include "example/example.c"
#endif
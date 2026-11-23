// One module of a shared module set, as retroplug-modc's `table` writes it. The generated C includes this header and
// SharedModules.hpp includes it for C++, so the table and its reader cannot disagree about the layout.
#ifndef RP_JS_MODULE_H
#define RP_JS_MODULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rp_js_module {
    const char *name;
    const uint8_t *bytecode;
    uint32_t size;
} rp_js_module;

#ifdef __cplusplus
}
#endif

#endif

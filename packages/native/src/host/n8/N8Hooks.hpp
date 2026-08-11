#pragma once

struct JSContext;  // quickjs (fwd-declared to keep this header JS-include-free)

namespace retroplug {

class N8Host;

// Bind the four N8 config hooks (__rp_getN8Config / __rp_setN8Port / __rp_connectN8 / __rp_setN8Lookahead)
// on globalThis, routed to `host` via each C-function's QuickJS func-data (which carries the N8Host*), so
// multiple plugin instances on separate contexts never cross-route. Both the SDL standalone and the DAW
// plugin call this on their control-plane JS context; the TS UI (n8Devices.ts) reads the hooks. Idempotent
// per context (binds once). `host` must outlive the context.
void bindN8Hooks(JSContext* ctx, N8Host& host);

}  // namespace retroplug

#pragma once

struct JSContext;  // quickjs (fwd-declared to keep this header JS-include-free)

namespace retroplug {

class LaunchpadHost;

// Bind the four Launchpad config hooks (__rp_getLaunchpadConfig / __rp_setLaunchpadPorts /
// __rp_connectLaunchpad / __rp_setLaunchpadFarewell) on globalThis, routed to `host` via each C-function's
// QuickJS func-data (which carries the LaunchpadHost*), so multiple instances on separate contexts never
// cross-route. Standalone-only today - the DAW plugin's MIDI seam still caps messages at 4 bytes, which no
// SysEx fits through. The TS UI (launchpadDevices.ts) reads the hooks. Idempotent per context (binds once).
// `host` must outlive the context.
void bindLaunchpadHooks(JSContext* ctx, LaunchpadHost& host);

}  // namespace retroplug

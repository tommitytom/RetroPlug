// useNativeEvent — subscribe to a native (lvgljs) event channel for a component's lifetime.
//
// The lvgljs `on`/`off` bus is how native pushes events into the UI (`frame`, `key`, `mouse`,
// `gamepad-*`, config/project change notifications, file-browser results). The legacy UI wires it with
// ~7 near-identical inline on/off effects; this collapses them.
//
// The latest `handler` is kept in a ref and invoked through a stable listener, so passing an inline
// closure does NOT re-subscribe every render — the on/off pair runs once per (channel) mount. This is
// the stable-wrapper pattern the legacy Menu uses to avoid churning its bus subscription.

import { useEffect, useRef } from "react";
import { on, off } from "lvgljs";

export function useNativeEvent(channel: string, handler: (...args: unknown[]) => void): void {
  const ref = useRef(handler);
  ref.current = handler;

  useEffect(() => {
    const listener = (...args: unknown[]) => ref.current(...args);
    on(channel, listener);
    return () => off(channel, listener);
  }, [channel]);
}

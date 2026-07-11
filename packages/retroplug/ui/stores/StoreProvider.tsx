// StoreProvider: exposes the greenfield store graph to the React tree and multiplexes its single-observer
// callbacks out to many React subscribers.
//
// In a real host (plugin / standalone) the control-plane bundle has ALREADY composed the one graph the
// DSP plays and getState serializes, and published it on globalThis[Symbol.for("plugin")]. We REUSE that
// graph — never compose a second, divergent one — and register our per-mount `notify` fan-out through the
// control plane's `setUiNotify` so store changes re-render us; the editor closing (unmount) detaches it,
// leaving the graph (and the loaded project) intact for the next open. Only a host with no control plane
// (the ui-test harness) falls back to composing its own graph here.

import { useRef, useEffect, type ReactNode } from "react";
import { composeAppStores, type StoreChannel, type AppStores } from "../../src/appStores";
import { StoreContext, type StoreContextValue } from "./useStores";

type PluginNamespace = {
  stores?: AppStores;
  setUiNotify?: (fn: ((channel: StoreChannel) => void) | null) => void;
};

function pluginNamespace(): PluginNamespace | undefined {
  return (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as PluginNamespace | undefined;
}

export function StoreProvider({ children }: { children: ReactNode }) {
  const ref = useRef<StoreContextValue | null>(null);
  const notifyRef = useRef<((channel: StoreChannel) => void) | null>(null);
  const ns = pluginNamespace();

  if (!ref.current) {
    const listeners = new Map<StoreChannel, Set<() => void>>();
    const notify = (channel: StoreChannel) => {
      const set = listeners.get(channel);
      if (set) for (const cb of set) cb();
    };
    notifyRef.current = notify;

    const subscribe = (channel: StoreChannel, cb: () => void): (() => void) => {
      let set = listeners.get(channel);
      if (!set) {
        set = new Set();
        listeners.set(channel, set);
      }
      set.add(cb);
      return () => {
        set!.delete(cb);
      };
    };

    // Reuse the control plane's published graph when present; otherwise (ui-test harness) own one.
    const stores = ns?.stores ?? composeAppStores({ notify });
    ref.current = { stores, subscribe };
  }

  // Route the shared graph's change notifications to this mount's fan-out, and stop on unmount so a
  // closed editor's torn-down subscribers are never called. A no-op when we composed our own graph
  // (composeAppStores wired `notify` directly and there's no control plane to register with).
  useEffect(() => {
    if (!ns?.setUiNotify || !notifyRef.current) return;
    ns.setUiNotify(notifyRef.current);
    return () => ns.setUiNotify?.(null);
  }, [ns]);

  return <StoreContext.Provider value={ref.current}>{children}</StoreContext.Provider>;
}

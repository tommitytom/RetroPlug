// StoreProvider: builds the greenfield store graph once and multiplexes its single-observer callbacks
// out to many React subscribers.
//
// The graph is built lazily on first render (useRef), so createRealBackend() resolves the __rpcSend
// bridge the host binds before the UI mounts. composeAppStores receives a single `notify(channel)` that
// fans out to per-channel listener Sets; the store hooks (useStores.ts) subscribe through the context's
// `subscribe`, never touching a store's own callback.

import { useRef, type ReactNode } from "react";
import { composeAppStores, type StoreChannel } from "../../src/appStores";
import { StoreContext, type StoreContextValue } from "./useStores";

export function StoreProvider({ children }: { children: ReactNode }) {
  const ref = useRef<StoreContextValue | null>(null);

  if (!ref.current) {
    const listeners = new Map<StoreChannel, Set<() => void>>();
    const notify = (channel: StoreChannel) => {
      const set = listeners.get(channel);
      if (set) for (const cb of set) cb();
    };

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

    ref.current = { stores: composeAppStores({ notify }), subscribe };
  }

  return <StoreContext.Provider value={ref.current}>{children}</StoreContext.Provider>;
}

// The notify plumbing behind the standalone-only menu modules (audioDraft, midiDevices, transport,
// n8Devices, launchpadDevices). Each owns state that lives in the NATIVE host rather than in a store, so
// a change there has no React path back to the menu and has to be pushed. All five had grown a
// character-identical copy of the same three pieces.
//
// A note on what went with them. Every copy also kept a `version` counter and exported an `xVersion()`
// reading it, each documented as "a stable snapshot for App's forced re-render". Nothing ever imported
// one - App bumps a `useState` counter instead - so in five places a counter was incremented and never
// read. They are gone, counter included.
//
// No React here on purpose: these modules are reachable from the pure-TS tests, whose tsconfig does not
// resolve `react`. The React side is ui/hooks/useRerender.ts (`useRerenderOn(subscribeX, ...)`, what App
// uses), or `useSyncExternalStore(x.subscribe, ...)` as ui/screens/grid/lsdjDebug.ts does.

export interface Subscribable {
  /** Notify every listener. Call after mutating the module's own state. */
  emit(): void;
  /** Subscribe; the returned function unsubscribes. */
  subscribe(fn: () => void): () => void;
}

export function createSubscribable(): Subscribable {
  const listeners = new Set<() => void>();
  return {
    emit(): void {
      for (const l of listeners) l();
    },
    subscribe(fn: () => void): () => void {
      listeners.add(fn);
      return () => void listeners.delete(fn);
    },
  };
}

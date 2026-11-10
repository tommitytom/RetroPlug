// The notify plumbing behind the menu modules whose state lives in the NATIVE host rather than in a store (the audio
// draft, the MIDI devices, the transport, and the device packs' n8 and Launchpad menus). A change there has no React
// path back to the menu, so it is pushed: a module emits, a subscriber repaints. A module keeps one subscribable and no
// version counter; a poll that needs to know a version reads the one it tracks itself.
//
// No React here on purpose: the module is reachable from the pure-TypeScript tests, whose tsconfig does not resolve
// `react`. The React side is the shell's useRerender hook (`useRerenderOn(subscribeX, ...)`, what the app uses), or
// `useSyncExternalStore(x.subscribe, ...)` as the LSDj debug readout does.
import type { Signal } from "../contribution";

export interface Subscribable {
  /** Notify every listener. Call after mutating the module's own state. */
  emit(): void;
  /** Subscribe; the returned function unsubscribes. */
  subscribe(fn: () => void): () => void;
}

/** A value its owner can set and the shell can read and watch: the writable counterpart of the contribution contract's
 *  `Readable` / `Signal`, and the one implementation of a view's `visible` or a package's debug flag. */
export function createSignal<T>(initial: T): Signal<T> {
  let value = initial;
  const listeners = new Set<() => void>();
  return {
    get: () => value,
    set(next: T): void {
      if (Object.is(next, value)) return;
      value = next;
      for (const l of listeners) l();
    },
    subscribe(fn: () => void): () => void {
      listeners.add(fn);
      return () => void listeners.delete(fn);
    },
  };
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

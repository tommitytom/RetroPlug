// The React seam over the greenfield store graph.
//
// The stores are framework-agnostic plain classes with single-observer callbacks and getters that return
// a FRESH array/object each call (view() even does per-entry fileExists RPCs). Two consequences shape
// this layer:
//   - Single observer → the StoreProvider is a multiplexer: it installs each store's one callback and
//     fans out to per-channel listener Sets. Hooks subscribe to the provider's fan-out, never a store.
//   - Fresh snapshots → a naive useSyncExternalStore(getSnapshot = () => store.view()) would see a new
//     reference every render and tear/loop. useStoreSnapshot caches the value and recomputes read() ONLY
//     after a notify, so the reference is stable between changes (and the expensive read runs once per
//     change, not per render).

import { createContext, useCallback, useContext, useRef, useSyncExternalStore } from "react";
import type { AppStores, StoreChannel } from "../../src/appStores";

export interface StoreContextValue {
  stores: AppStores;
  /** Register `cb` for a channel; returns an unsubscribe. */
  subscribe: (channel: StoreChannel, cb: () => void) => () => void;
}

export const StoreContext = createContext<StoreContextValue | null>(null);

function useStoreContext(): StoreContextValue {
  const ctx = useContext(StoreContext);
  if (!ctx) throw new Error("useStores must be used within a <StoreProvider>");
  return ctx;
}

/** The store graph, for imperative actions (mutators). Reading reactively goes through the hooks below —
 *  but calling a mutator directly (e.g. project.setZoom(n)) is fine: the store observers now cover
 *  settings/dirty, so the change fans out and the relevant hooks re-render with no action wrapper. */
export function useStores(): AppStores {
  return useStoreContext().stores;
}

/** Subscribe to a store channel and read a cached snapshot that only changes when the channel fires.
 *  The cache gives useSyncExternalStore the stable identity it needs; `read` runs lazily on first use
 *  and after each notify. */
export function useStoreSnapshot<T>(channel: StoreChannel, read: () => T): T {
  const { subscribe } = useStoreContext();
  const cache = useRef<{ value: T; valid: boolean }>({ value: undefined as unknown as T, valid: false });

  const sub = useCallback(
    (onStoreChange: () => void) =>
      subscribe(channel, () => {
        cache.current.valid = false; // invalidate; recompute lazily in getSnapshot
        onStoreChange();
      }),
    [channel, subscribe],
  );

  const getSnapshot = () => {
    if (!cache.current.valid) {
      cache.current.value = read();
      cache.current.valid = true;
    }
    return cache.current.value;
  };

  return useSyncExternalStore(sub, getSnapshot);
}

// --- public hooks (one per store view) ---------------------------------------------------------------

/** The systems list (id / platform / core / paths / focus / settings / roles), re-read on any structural
 *  edit, load, or new. */
export function useSystems() {
  const { project } = useStores();
  return useStoreSnapshot("systems", () => project.systems.view());
}

/** The project settings (layout / midiRouting / audioRouting / zoom). */
export function useProjectSettings() {
  const { project } = useStores();
  return useStoreSnapshot("project", () => project.settings());
}

/** Whether the project has unsaved changes. */
export function useIsDirty(): boolean {
  const { project } = useStores();
  return useStoreSnapshot("project", () => project.isDirty());
}

/** The recent-projects list, with live missing flags + labels. */
export function useRecent() {
  const { recent } = useStores();
  return useStoreSnapshot("recent", () => recent.view());
}

/** The machine-global user config (default zoom, active binding profiles, sram auto-save). */
export function useUserConfig() {
  const { userConfig } = useStores();
  return useStoreSnapshot("userConfig", () => userConfig.config());
}

/** The resolved active bindings (keyboard + gamepad maps). */
export function useBindings() {
  const { bindings } = useStores();
  return useStoreSnapshot("bindings", () => bindings.resolvedBindings());
}

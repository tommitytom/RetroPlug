// Where a pack was refused, as this module set hears of it (docs/downloadable-packs-m3-design.md section 5): the native
// table's refusals (__rp_packRefusals: an API level, no library for this platform, discovery, and the TS loaders'
// reports), this set's TS loader's, and this context's Engine's (every setGraph answer's refusedPacks). What a
// placeholder's card and a missing role's row read, and Settings > Packs (M4) after them. Imports nothing, so the
// loader, the graph and the needs can all read it.

/** A refusal as the native table answers it. */
export interface NativeRefusal {
  readonly id: string;
  readonly reason: string;
  /** The refused folder's version, when it is a `<id>/<version>` folder. */
  readonly version?: string;
  /** Refused for its API level. */
  readonly needsUpdate?: boolean;
}

interface RefusalHooks {
  __rp_packRefusals?: () => string;
}

let native: Map<string, NativeRefusal> | null = null;
const ts = new Map<string, string>();
const engine = new Map<string, string>();

/** The native table's refusals by pack id (the last for an id wins), read once and again after a live load. */
export function nativeRefusals(): ReadonlyMap<string, NativeRefusal> {
  if (!native) {
    native = new Map();
    const hook = (globalThis as RefusalHooks).__rp_packRefusals;
    if (hook) {
      try {
        for (const r of JSON.parse(hook()) as NativeRefusal[]) native.set(r.id, r);
      } catch (e) {
        console.warn(`[packs] the host's refusal list did not read: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
  }
  return native;
}

/** Read the native refusals again at the next ask (a live load may have added one). */
export function forgetNativeRefusals(): void {
  native = null;
}

/** The TS loader's refusal of `id` (src/packs/loadPacks.ts calls this from its refuse). */
export function noteTsRefusal(id: string, reason: string): void {
  ts.set(id, reason);
}

export function tsRefusals(): ReadonlyMap<string, string> {
  return ts;
}

/** An Engine's refusals as a setGraph answer lists them. Recorded only: a publish is under way, so nothing is told. */
export function noteEngineRefusals(list: readonly { readonly id: string; readonly reason: string }[]): void {
  for (const { id, reason } of list) engine.set(id, reason);
}

export function engineRefusals(): ReadonlyMap<string, string> {
  return engine;
}

// The in-app (React/LVGL) file browser's request channel + pattern matching. openFileBrowser() publishes a
// request here; the App renders a browser overlay (a MenuTree) and calls resolveFileBrowser() with the pick
// (or null on cancel), which settles the promise. One browse at a time — a second request while one is open
// resolves null immediately (same contract as the old single native-dialog slot). Host-agnostic: the same
// browser renders in the SDL standalone AND the DPF plugin, backed by the cross-host listDir RPC.

import type { FileBrowserOpts } from "./backend";

export interface FileBrowserRequest {
  opts: FileBrowserOpts;
  resolve: (path: string | null) => void;
}

let pending: FileBrowserRequest | null = null;
const listeners = new Set<() => void>();
function emit(): void {
  for (const l of listeners) l();
}

/** Open the in-app browser for `opts`, resolving to the chosen absolute path (or null on cancel). */
export function requestFileBrowser(opts: FileBrowserOpts): Promise<string | null> {
  if (pending) return Promise.resolve(null); // one at a time
  return new Promise<string | null>((resolve) => {
    pending = { opts, resolve };
    emit();
  });
}

/** The overlay to render, or null. Read by App each render. */
export function getFileBrowserRequest(): FileBrowserRequest | null {
  return pending;
}

/** Settle the current browse with a pick (absolute path) or null (cancel), and clear the overlay. */
export function resolveFileBrowser(path: string | null): void {
  const p = pending;
  pending = null;
  emit();
  p?.resolve(path ?? null);
}

/** App subscribes so a request/resolve re-renders the overlay. Returns an unsubscribe. */
export function subscribeFileBrowser(fn: () => void): () => void {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}

// Remember the directory across browses (so reopening resumes where you left off). The current directory of
// an open browser IS this value; navigate() updates it. Seeded from `fallback` (the config dir) on first use.
let lastDir: string | null = null;
export function getLastBrowseDir(fallback: string): string {
  return lastDir ?? fallback;
}
export function setLastBrowseDir(dir: string): void {
  lastDir = dir;
}

// --- glob matching for the browser's pattern filter (e.g. "*.gb", "*.rplg.zip", "*.ss?") --------------
export function globToRegExp(glob: string): RegExp {
  const body = glob
    .replace(/[.+^${}()|[\]\\]/g, "\\$&") // escape regex specials (NOT * or ?)
    .replace(/\*/g, ".*")
    .replace(/\?/g, ".");
  return new RegExp("^" + body + "$", "i"); // case-insensitive, like the native fnmatch(FNM_CASEFOLD)
}

/** Whether `name` matches any of `patterns` (empty patterns = match all). */
export function matchesPatterns(name: string, patterns: string[]): boolean {
  if (patterns.length === 0) return true;
  return patterns.some((p) => globToRegExp(p).test(name));
}

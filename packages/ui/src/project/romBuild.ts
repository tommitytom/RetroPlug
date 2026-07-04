// Plugin-side ROM add/load orchestration — the TS home of what used to be
// PluginRpcService::loadRomFromPath / addRomFromPath / loadMgb + the
// onFileBrowserSelected ROM dispatch. The native side now only content-detects a
// browser selection and, for a real ROM, emits "rom-path-selected"; this module
// decides load-vs-add, does the sibling-`.rplg` deferral, and drives the one
// native primitive (constructSystem). Byte IO + emulator construction stay
// native — no ROM bytes cross the bridge, just the path.
//
// The `.sav` -> ROM pairing (a picked save with no explicit ROM) is still handled
// natively (deferred to a follow-up); native only hands us plain ROM paths.

import { fileExists, constructSystem, openRomBrowser } from "./projectHost";
import { startLoad, type LoadResult } from "./loadProject";

type Mode = "load" | "add";

// A ROM open is a two-step dance: the menu sets the intended mode + opens the
// native browser, then the selection returns asynchronously via
// "rom-path-selected". The mode is held here between the two (mirrors
// loadProject.ts's `pending` latch). A path arriving with no pending mode (the
// RETROPLUG_AUTOLOAD_ROM startup path) defaults to "load".
let pendingMode: Mode | null = null;

// Replace a path's final extension (or append when there is none): `<rom>.gb` ->
// `<rom>.rplg`. Lexical, matching the native replace_extension the deferral used.
function replaceExt(path: string, ext: string): string {
  const slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  const dot = path.lastIndexOf(".");
  return (dot > slash ? path.slice(0, dot) : path) + ext;
}

// Menu entry points. Each stashes the mode, then opens the native ROM dialog;
// the selection comes back through onRomPathSelected.
export function startLoadRom(): void {
  pendingMode = "load";
  openRomBrowser("replace");
}
export function startAddRom(): void {
  pendingMode = "add";
  openRomBrowser("add");
}

// Load the binary-baked mGB (no file, no dialog): builds directly.
export function loadMgb(): void {
  constructSystem("", "mgb", "load");
}

// Handle a "rom-path-selected" event: build the ROM, or — for a "load" beside an
// existing `<rom>.rplg` — open that project instead of a bare single-system one
// (the old native sibling-`.rplg` deferral). Returns the deferral's LoadResult
// (so the UI can surface missing-files / incompatible), or an empty result when
// it just built a system (the DSP's config-changed drives the tile refresh).
export function onRomPathSelected(path: string): LoadResult {
  const mode: Mode = pendingMode ?? "load";
  pendingMode = null;

  if (mode === "load") {
    const rplg = replaceExt(path, ".rplg");
    if (fileExists(rplg)) return startLoad(rplg);
  }

  constructSystem(path, "", mode);
  return { missing: [], incompatible: false };
}

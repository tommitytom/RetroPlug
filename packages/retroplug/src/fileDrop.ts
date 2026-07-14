// Drag-and-drop routing: decide what a set of dropped file paths should DO, given the live instance
// count and which tile (if any) the drop landed on. Pure and store-free — no mutation, no async, no
// window geometry — so the whole decision table is unit-testable against the mock backend. App owns the
// two things this can't know: mapping the drop coordinate to a target tile (hitTestTile) and applying
// the resulting action through the guarded modals / the systems store.
//
// The rules mirror the menu semantics:
//   - a `.rplg` / `.rplg.zip` project file always loads as a project (it replaces everything anyway);
//   - a ROM on the start screen or a single instance loads as a NEW project ("Load…");
//   - a ROM in a multi-instance project cold-boot REPLACES the target instance (the tile under the
//     cursor, else the focused one) — the menu's "Replace Instance";
//   - a bare `.sav` dropped onto a tile in a multi-instance project loads into THAT instance
//     ("Load SRAM"); anywhere else it's treated as a load, paired with its sibling ROM (or a browser).

import type { HostBackend } from "./backend";
import { classifyKind } from "./fileSelection";

/** A project file (thin `.rplg` or the export `.rplg.zip`) — classified by extension, like ProjectStore. */
function isProjectPath(path: string): boolean {
  return /\.rplg(\.zip)?$/i.test(path);
}

/** What App should do with a drop. `loadProject`/`loadRom` route through the guarded modals (an
 *  unsaved-changes prompt, just like the menu's Load); `replace`/`loadSram` hit the systems store
 *  directly (like the un-guarded "Replace Instance"/"Load SRAM"); `pairSav` needs an async ROM pick. */
export type DropAction =
  | { type: "loadProject"; path: string }
  | { type: "loadRom"; romPath: string; explicitSav?: string }
  | { type: "replace"; id: number; romPath: string; explicitSav?: string }
  | { type: "loadSram"; id: number; sav: string }
  | { type: "pairSav"; sav: string }
  | { type: "ignore"; reason: string };

export interface DropContext {
  /** Live instance count (0 = start screen, 1 = single, >1 = multi). */
  count: number;
  /** The instance to act on: the tile the drop landed on, or the focused instance as a fallback. */
  targetId: number | null;
  /** True iff `targetId` is an actual tile the drop hit (not the focus fallback) — gates whether a
   *  bare `.sav` loads into that instance vs. is treated as a project load. */
  onTile: boolean;
  /** Resolve a `.sav`'s sibling ROM, or null — `SystemsStore.resolveSiblingRom`. Injected to keep this
   *  module store-free. */
  siblingRom: (savPath: string) => string | null;
}

/** Decide the action for `paths`. A single ROM + single `.sav` are paired; otherwise the first ROM (or,
 *  with no ROM, the first `.sav`) is acted on and any extras are ignored. */
export function resolveDropAction(backend: HostBackend, ctx: DropContext, paths: string[]): DropAction {
  const files = paths.filter((p) => p.length > 0);
  if (files.length === 0) return { type: "ignore", reason: "no files" };

  // A project file wins outright — loading a project rebuilds the whole session regardless of count.
  const project = files.find(isProjectPath);
  if (project) return { type: "loadProject", path: project };

  const roms = files.filter((p) => classifyKind(backend, p) === "rom");
  const savs = files.filter((p) => classifyKind(backend, p) === "sav");
  const rom = roms[0];
  const sav = savs[0];

  if (rom) {
    // Pair a lone ROM with a lone dropped `.sav`; a messier multi-drop just loads the first ROM.
    const explicitSav = roms.length === 1 && savs.length === 1 ? sav : undefined;
    const multi = ctx.count > 1 && ctx.targetId != null;
    return multi
      ? { type: "replace", id: ctx.targetId as number, romPath: rom, ...(explicitSav ? { explicitSav } : {}) }
      : { type: "loadRom", romPath: rom, ...(explicitSav ? { explicitSav } : {}) };
  }

  if (sav) {
    // Onto a specific tile in a multi-instance project → load that save into the instance.
    if (ctx.count > 1 && ctx.onTile && ctx.targetId != null) return { type: "loadSram", id: ctx.targetId, sav };
    // Anywhere else → treat as a load: pair the sibling ROM if there is one, else ask for one.
    const sibling = ctx.siblingRom(sav);
    return sibling ? { type: "loadRom", romPath: sibling, explicitSav: sav } : { type: "pairSav", sav };
  }

  return { type: "ignore", reason: "unsupported file" };
}

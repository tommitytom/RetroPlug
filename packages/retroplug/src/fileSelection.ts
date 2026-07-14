// FileSelection: turn a user's file pick into the right systems op. Opens the OS
// browser, classifies what comes back (ROM by content / .sav by extension / other),
// resolves the .sav→ROM pairing (its 2nd, ROM-only browser), then routes:
//   - resolveLoad: RESOLVE-ONLY (no mutation) for the project-level "Load…" — decide
//     between a sibling <rom>.rplg project and a fresh ROM, letting the caller apply
//     the guarded reset+load.
//   - browseAdd: append a new instance.
//   - browseReplace(id): swap one instance in place (fresh boot, sav from disk).
//   - browseSwap(id): swap one instance's ROM in place, KEEPING its live SRAM.
//
// The dialog is the one intrinsically-async op (it waits on human input over DPF's
// non-blocking browser), so the whole flow is plain async: each method resolves to its
// FINAL outcome after every dialog settles. The 2nd browser is just another `await`
// inside the same Promise — no pending-mode latch, no out-of-band event correlation.

import type { HostBackend } from "./backend";
import { extensionLower } from "./pathUtil";
import { siblingRplgPath } from "./savPaths";
import { classifyRom, type SystemsStore } from "./systemsStore";

export const ROM_OR_SAV_PATTERNS = ["*.gb", "*.gbc", "*.gba", "*.nes", "*.sav"];
export const ROM_PATTERNS = ["*.gb", "*.gbc", "*.gba", "*.nes"];

/** What a picked file is: a ROM (any known format, by content), a `.sav` (by
 *  extension, since it isn't a ROM), or something else. */
export type FileKind = "rom" | "sav" | "other";

export function classifyKind(backend: HostBackend, path: string): FileKind {
  if (classifyRom(backend, path) !== "unknown") return "rom";
  if (extensionLower(path) === ".sav") return "sav";
  return "other";
}

/** The result of an add/replace selection, once every dialog it triggered has settled. */
export type SelectionOutcome =
  | { kind: "added"; system: number } // an add appended one
  | { kind: "replaced"; system: number } // a replace swapped one in place
  | { kind: "swapped"; system: number } // a swap-ROM changed one in place, keeping its SRAM
  | { kind: "error"; path: string } // unreadable / not a ROM / bad pair target
  | { kind: "cancelled" }; // a dialog closed with no pick

/** What a "Load…" pick resolves to (RESOLVE-ONLY — the caller applies it, guarded). */
export type ResolvedLoad =
  | { kind: "project"; path: string } // a sibling <rom>.rplg → load that project
  | { kind: "rom"; romPath: string; explicitSav?: string } // a fresh ROM → new project from it
  | { kind: "error"; path: string } // unreadable / not a ROM / bad pair target
  | { kind: "cancelled" }; // a dialog closed with no pick

// A picked path resolved to a concrete ROM (+ paired sav), or a terminal state — the
// shared classify-and-pair step, done WITHOUT mutating the store.
type Pick =
  | { kind: "rom"; rom: string; explicitSav?: string }
  | { kind: "error"; path: string }
  | { kind: "cancelled" };

export class FileSelection {
  constructor(private readonly backend: HostBackend, private readonly systems: SystemsStore) {}

  /** Project-level "Load…": browse a ROM/sav (pairing an unpaired `.sav` via the 2nd browser), then decide
   *  between the sibling `<rom>.rplg` project and a fresh ROM. Pure of store mutation — the caller applies
   *  the outcome behind the unsaved-changes guard. */
  async resolveLoad(): Promise<ResolvedLoad> {
    const pick = await this.pickRom();
    if (pick.kind !== "rom") return pick; // cancelled / error
    // With no paired save, a sibling `<rom>.rplg` means "open that project" rather than "new project from ROM".
    if (!pick.explicitSav) {
      const rplg = siblingRplgPath(pick.rom);
      if (this.backend.fileExists(rplg)) return { kind: "project", path: rplg };
    }
    return { kind: "rom", romPath: pick.rom, explicitSav: pick.explicitSav };
  }

  /** "Add Instance": browse a ROM/sav and append a new instance (with `.sav` pairing). When `parentId` is
   *  given (the instance the Add was launched from), the new instance inherits its link group — promoting the
   *  parent to group 1 if it was ungrouped, so the pair is linked. */
  async browseAdd(parentId?: number): Promise<SelectionOutcome> {
    const pick = await this.pickRom();
    if (pick.kind !== "rom") return pick;
    const opts = pick.explicitSav ? { explicitSav: pick.explicitSav } : undefined;
    const id = this.systems.addSystem(pick.rom, opts);
    if (id === null) return { kind: "error", path: pick.rom };
    if (parentId != null) this.systems.inheritLinkGroup(id, parentId);
    return { kind: "added", system: id };
  }

  /** "Replace Instance": browse a ROM/sav and swap system `id` in place (with `.sav` pairing). Stays in the
   *  current project — no sibling-project defer, no project adoption. */
  async browseReplace(id: number): Promise<SelectionOutcome> {
    const pick = await this.pickRom();
    if (pick.kind !== "rom") return pick;
    const opts = pick.explicitSav ? { explicitSav: pick.explicitSav } : undefined;
    const newId = this.systems.replaceSystem(id, pick.rom, opts);
    return newId === null ? { kind: "error", path: pick.rom } : { kind: "replaced", system: newId };
  }

  /** "Swap ROM (Preserve SRAM)": browse a ROM (ROM-only — a `.sav` pick would contradict "keep the current
   *  save") and swap system `id`'s ROM in place, carrying its live battery SRAM into the new cart. Stays in
   *  the current project. Distinct from browseReplace, which cold-boots the picked ROM with its own on-disk sav. */
  async browseSwap(id: number): Promise<SelectionOutcome> {
    const path = await this.backend.openFileBrowser({ title: "Swap ROM (keep SRAM)", patterns: ROM_PATTERNS });
    if (path === null) return { kind: "cancelled" };
    if (classifyKind(this.backend, path) !== "rom") return { kind: "error", path };
    const newId = this.systems.swapRom(id, path);
    return newId === null ? { kind: "error", path } : { kind: "swapped", system: newId };
  }

  // Open the ROM-or-sav browser and resolve the pick to a concrete ROM (+ paired sav), running the 2nd
  // ROM-only browser for an unpaired `.sav`. No store mutation — the shared front half of every op.
  private async pickRom(): Promise<Pick> {
    const path = await this.backend.openFileBrowser({ title: "Open ROM or .sav", patterns: ROM_OR_SAV_PATTERNS });
    if (path === null) return { kind: "cancelled" };
    if (classifyKind(this.backend, path) === "sav") return this.pairSav(path);
    if (classifyKind(this.backend, path) === "rom") return { kind: "rom", rom: path };
    return { kind: "error", path }; // "other" — fails cleanly
  }

  // A picked save: pair with its sibling ROM if there is one, else open the 2nd (ROM-only) browser and
  // pair with what the user points at.
  private async pairSav(sav: string): Promise<Pick> {
    const sibling = this.systems.resolveSiblingRom(sav);
    if (sibling) return { kind: "rom", rom: sibling, explicitSav: sav };
    const rom = await this.backend.openFileBrowser({ title: "Select the ROM for this save", patterns: ROM_PATTERNS });
    if (rom === null) return { kind: "cancelled" };
    if (classifyKind(this.backend, rom) !== "rom") return { kind: "error", path: rom };
    return { kind: "rom", rom, explicitSav: sav };
  }
}

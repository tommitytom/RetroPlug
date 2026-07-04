// FileSelection: turn a user's file pick into the right systems op. Opens the OS
// browser, classifies what comes back (ROM by content / .sav by extension / other),
// and routes it — load vs add per the caller's mode, and the .sav→ROM pairing with
// its 2nd (ROM-only) browser.
//
// The dialog is the one intrinsically-async op (it waits on human input over DPF's
// non-blocking browser), so the whole flow is plain async: browse() resolves to the
// FINAL outcome after every dialog settles. The 2nd browser is just another `await`
// inside the same Promise — no pending-mode latch, no out-of-band event correlation.
// Port of PluginRpcService's handleOpenRomSelection / onFileBrowserSelected /
// openRomBrowser, with classification + pairing done TS-side over the systems store.

import type { Backend } from "./backend";
import { extensionLower } from "./pathUtil";
import { classifyRom, type SystemsStore } from "./systemsStore";

export const ROM_OR_SAV_PATTERNS = ["*.gb", "*.gbc", "*.gba", "*.nes", "*.sav"];
export const ROM_PATTERNS = ["*.gb", "*.gbc", "*.gba", "*.nes"];

/** What a picked file is: a ROM (any known format, by content), a `.sav` (by
 *  extension, since it isn't a ROM), or something else. */
export type FileKind = "rom" | "sav" | "other";

export function classifyKind(backend: Backend, path: string): FileKind {
  if (classifyRom(backend, path) !== "unknown") return "rom";
  if (extensionLower(path) === ".sav") return "sav";
  return "other";
}

/** The result of a selection, once every dialog it triggered has settled. */
export type SelectionOutcome =
  | { kind: "loaded"; system: number } // a load replaced/adopted a system
  | { kind: "added"; system: number } // an add appended one
  | { kind: "deferred"; project: string } // sibling <rom>.rplg → the Project domain loads it
  | { kind: "error"; path: string } // unreadable / not a ROM / bad pair target
  | { kind: "cancelled" }; // a dialog closed with no pick

type Mode = "load" | "add";

export class FileSelection {
  constructor(private readonly backend: Backend, private readonly systems: SystemsStore) {}

  /** Open the ROM-or-sav browser and route the pick. Resolves to the final outcome. */
  async browse(mode: Mode): Promise<SelectionOutcome> {
    const path = await this.backend.openFileBrowser({
      title: "Open ROM or .sav",
      patterns: ROM_OR_SAV_PATTERNS,
    });
    return this.route(path, mode);
  }

  /** Route a KNOWN path with no dialog (autoload / recent-files / drag-drop). May
   *  still open a pairing browser when it's an unpaired `.sav`. */
  selectPath(path: string, mode: Mode): Promise<SelectionOutcome> {
    return this.route(path, mode);
  }

  private async route(path: string | null, mode: Mode): Promise<SelectionOutcome> {
    if (path === null) return { kind: "cancelled" };
    if (classifyKind(this.backend, path) === "sav") return this.beginPair(path, mode);
    return this.applyRom(path, mode); // rom, or "other" (which fails cleanly)
  }

  // A picked save: pair with its sibling ROM if there is one, else open the 2nd
  // (ROM-only) browser and pair with what the user points at.
  private async beginPair(sav: string, mode: Mode): Promise<SelectionOutcome> {
    const sibling = this.systems.resolveSiblingRom(sav);
    if (sibling) return this.applyRom(sibling, mode, sav);
    const rom = await this.backend.openFileBrowser({
      title: "Select the ROM for this save",
      patterns: ROM_PATTERNS,
    });
    if (rom === null) return { kind: "cancelled" };
    if (classifyKind(this.backend, rom) !== "rom") return { kind: "error", path: rom };
    return this.applyRom(rom, mode, sav);
  }

  // Drive the systems store: add appends, load replaces the focused tile (or defers to
  // a sibling project). `explicitSav` is a paired save.
  private applyRom(rom: string, mode: Mode, explicitSav?: string): SelectionOutcome {
    const opts = explicitSav ? { explicitSav } : undefined;
    if (mode === "add") {
      const id = this.systems.addSystem(rom, opts);
      return id === null ? { kind: "error", path: rom } : { kind: "added", system: id };
    }
    const r = this.systems.loadRom(rom, opts);
    if (r === null) return { kind: "error", path: rom };
    if ("deferredProject" in r) return { kind: "deferred", project: r.deferredProject };
    return { kind: "loaded", system: r.system };
  }
}

// SystemsStore: the live systems list as the app owns it. TS is the source of truth
// for order, ids-as-handles, focus, and dirty; the Backend is a narrow emulator-
// lifecycle service (build / clone / reload / drop) that takes CONCRETE paths TS has
// already resolved. All the orchestration lives here: classify a ROM (TS-side, from
// its header), disambiguate a sav suffix against the live list, resolve a paired-sav
// override, pick a sibling ROM, and apply the load/add/replace + focus rules.
//
// Mirrors RecentStore's shape (constructor(backend, onChange), view(), mutators,
// no-op-guarded change signal). Reproduces PluginRpcService's constructSystem /
// duplicateSystem / reload orchestration and the DSP list handlers
// (PluginDSP.cpp:406-458), with every path derived by the pure kernels.

import type { Backend } from "./backend";
import { detectRomFormat, ROM_SNIFF_LEN, type RomFormat } from "./romFormat";
import { resolveSavPath, siblingSavPath, siblingRplgPath, nextFreeSavSuffix } from "./savPaths";
import {
  type SystemEntry,
  type SystemKind,
  findById,
  appendEntry,
  removeById,
  replaceById,
  isSuffixOwned,
  resolveSavOverride,
  pickSiblingRom,
  nextFocusAfterRemove,
} from "./systemsList";

/** Classify a ROM from its header only — the one place ROM bytes enter TS, and just
 *  the first `ROM_SNIFF_LEN` of them. Native never classifies. */
export function classifyRom(backend: Backend, romPath: string): RomFormat {
  return detectRomFormat(backend.readFilePrefix(romPath, ROM_SNIFF_LEN) ?? new Uint8Array());
}

/** A system as the UI sees it: thin identity + live `focused`/`missing` flags. */
export interface SystemView {
  id: number;
  kind: SystemKind;
  romPath: string;
  savPath: string; // the override ("" = derived from suffix)
  savSuffix: number;
  embedded: boolean;
  focused: boolean;
  missing: boolean;
}

/** loadRom outcome: defer to a sibling project, a built system, or a failure. */
export type LoadResult = { deferredProject: string } | { system: number } | null;

export class SystemsStore {
  private entries: SystemEntry[] = [];
  private focusedId = 0;
  private dirty = false;

  constructor(private readonly backend: Backend, private readonly onChange: () => void = () => {}) {}

  /** Snapshot for the UI, with live focus + missing flags. */
  view(): SystemView[] {
    return this.entries.map((e) => ({
      id: e.id,
      kind: e.kind,
      romPath: e.romPath,
      savPath: e.savPath,
      savSuffix: e.savSuffix,
      embedded: e.embeddedRom !== "",
      focused: e.id === this.focusedId,
      missing: e.romPath !== "" && e.embeddedRom === "" && !this.backend.fileExists(e.romPath),
    }));
  }

  systems(): SystemEntry[] {
    return this.entries.slice();
  }
  focused(): number {
    return this.focusedId;
  }
  isDirty(): boolean {
    return this.dirty;
  }

  /** Append a new instance of `romPath`, disambiguating its sav suffix against the
   *  live list. Returns the new id, or null when the ROM is unknown/unreadable. */
  addSystem(romPath: string, opts?: { explicitSav?: string }): number | null {
    const suffix = this.freeSuffix(romPath);
    const built = this.construct(romPath, "", suffix, opts?.explicitSav);
    if (!built) return null;
    const wasEmpty = this.entries.length === 0;
    this.entries = appendEntry(this.entries, built.entry);
    if (wasEmpty) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Load `romPath`, replacing the focused tile (or adopting into an empty project).
   *  With no paired save, defers to a sibling `<rom>.rplg` when one exists. */
  loadRom(romPath: string, opts?: { explicitSav?: string }): LoadResult {
    if (!opts?.explicitSav) {
      const rplg = siblingRplgPath(romPath);
      if (this.backend.fileExists(rplg)) return { deferredProject: rplg };
    }
    const id = this.loadInPlace(romPath, "", opts?.explicitSav);
    return id === null ? null : { system: id };
  }

  /** Load the binary-baked mGB (no file, no dialog), replacing the focused tile. */
  loadMgb(): number | null {
    return this.loadInPlace("", "mgb", undefined);
  }

  /** Swap a specific system for `romPath`, in place. */
  replaceSystem(id: number, romPath: string, opts?: { explicitSav?: string }): number | null {
    if (!findById(this.entries, id)) return null;
    const built = this.construct(romPath, "", 0, opts?.explicitSav, id);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, built.entry);
    if (this.focusedId === id) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Clone a system's LIVE state into a fresh appended instance with its own suffix. */
  duplicateSystem(id: number): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const suffix = this.freeSuffix(src.romPath);
    const savPath = resolveSavPath(src.romPath, suffix, "");
    const newId = this.backend.duplicateSystem(id, savPath);
    if (newId === null) return null;
    this.entries = appendEntry(this.entries, {
      id: newId,
      kind: src.kind,
      romPath: src.romPath,
      savPath: "",
      savSuffix: suffix,
      embeddedRom: src.embeddedRom,
    });
    return this.committed(newId);
  }

  /** Drop a system; refocus the front if it was focused. False when absent. */
  removeSystem(id: number): boolean {
    if (!findById(this.entries, id)) return false;
    this.backend.removeSystem(id);
    this.entries = removeById(this.entries, id);
    this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
    this.markDirty();
    return true;
  }

  /** Rebuild a system's ROM from disk (native carries its live SRAM + paths),
   *  swapping in place with a new id and preserving identity + focus. */
  reloadSystem(id: number): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const newId = this.backend.reloadSystem(id);
    if (newId === null) return null;
    this.entries = replaceById(this.entries, id, { ...src, id: newId });
    if (this.focusedId === id) this.focusedId = newId;
    return this.committed(newId);
  }

  /** The sibling ROM for a picked `.sav`, or null — the pairing helper. */
  resolveSiblingRom(savPath: string): string | null {
    return pickSiblingRom(
      savPath,
      (p) => this.backend.fileExists(p),
      (p) => classifyRom(this.backend, p),
    );
  }

  // --- project-load rebuild seam ------------------------------------------
  // clear()/adopt() are QUIET: no dirty/onChange, since a load isn't a user edit
  // (the ProjectStore sets dirty explicitly around load/new).

  /** Tear down every system + reset the list/focus (for `new` + before a load). */
  clear(): void {
    for (const e of this.entries) this.backend.removeSystem(e.id);
    this.entries = [];
    this.focusedId = 0;
  }

  /** Reconstruct one system from a serialized config entry, preserving its EXACT
   *  savSuffix + savPath-override (no free-suffix reassignment). Appends; focuses the
   *  first. Returns the new id, or null when the ROM won't classify. */
  adopt(config: {
    romPath?: string;
    savPath?: string;
    savSuffix?: number;
    embeddedRom?: string;
  }): number | null {
    const embeddedRom = config.embeddedRom ?? "";
    const romPath = config.romPath ?? "";
    const savSuffix = config.savSuffix ?? 0;
    const override = config.savPath ?? "";
    let kind: SystemKind;
    if (embeddedRom) {
      kind = "sameboy";
    } else {
      const fmt = classifyRom(this.backend, romPath);
      if (fmt === "unknown") return null;
      kind = fmt;
    }
    const savPath = embeddedRom ? null : resolveSavPath(romPath, savSuffix, override);
    const id = this.backend.constructSystem({ romPath, embeddedRom, savPath, statePath: null });
    if (id === null) return null;
    const wasEmpty = this.entries.length === 0;
    this.entries = appendEntry(this.entries, { id, kind, romPath, savPath: override, savSuffix, embeddedRom });
    if (wasEmpty) this.focusedId = id;
    return id;
  }

  // --- internals ----------------------------------------------------------

  // Replace the focused tile (or adopt into an empty project), focusing the result.
  private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
    const empty = this.entries.length === 0;
    const target = empty ? undefined : this.effectiveFocus();
    const built = this.construct(romPath, embeddedRom, 0, explicitSav, target);
    if (!built) return null;
    this.entries = empty
      ? appendEntry(this.entries, built.entry)
      : replaceById(this.entries, target as number, built.entry);
    this.focusedId = built.id;
    return this.committed(built.id);
  }

  // Classify (unless embedded), resolve the concrete savPath, and build via native.
  // Returns the new id + the thin entry to record, or null when rejected/failed.
  private construct(
    romPath: string,
    embeddedRom: string,
    suffix: number,
    explicitSav: string | undefined,
    replaceId?: number,
  ): { id: number; entry: SystemEntry } | null {
    let kind: SystemKind;
    if (embeddedRom) {
      kind = "sameboy"; // embedded ROMs are always Game Boy
    } else {
      const fmt = classifyRom(this.backend, romPath);
      if (fmt === "unknown") return null;
      kind = fmt;
    }
    const override = resolveSavOverride(romPath, suffix, explicitSav ?? "", (p) =>
      this.backend.canonicalize(p),
    );
    const savPath = embeddedRom ? null : resolveSavPath(romPath, suffix, override);
    const id = this.backend.constructSystem({ romPath, embeddedRom, savPath, statePath: null, replaceId });
    if (id === null) return null;
    return { id, entry: { id, kind, romPath, savPath: override, savSuffix: suffix, embeddedRom } };
  }

  // The free suffix for a new instance of `romPath`: live-list ownership + on-disk.
  private freeSuffix(romPath: string): number {
    return nextFreeSavSuffix(
      romPath,
      (n) => isSuffixOwned(this.entries, romPath, n),
      (n) => this.backend.fileExists(siblingSavPath(romPath, n)),
    );
  }

  // The focused id, falling back to the front when focus is stale (DSP LoadRom rule).
  private effectiveFocus(): number {
    return findById(this.entries, this.focusedId) ? this.focusedId : this.entries[0].id;
  }

  private markDirty(): void {
    this.dirty = true;
    this.onChange();
  }

  // Finish a successful mutation: mark dirty + notify, return the id.
  private committed(id: number): number {
    this.markDirty();
    return id;
  }
}

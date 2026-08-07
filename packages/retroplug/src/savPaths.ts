// Sibling-file derivation for a ROM: its battery `.sav`, its project `.rplg`, the
// resolved save path (suffix sibling vs explicit override), the free-suffix scan
// used when adding a duplicate instance, and the ordered ROM candidates probed
// when pairing a picked `.sav`. Ports of packages/native SramAutoSave.hpp,
// assignSavSuffix, and findSiblingRom's candidate enumeration.
//
// The two "kernels" here stay pure so they're unit-testable without a Backend:
// `nextFreeSavSuffix` takes ownership/existence predicates, and
// `siblingRomCandidates` returns the ordered path list — the systems domain wires
// the live-systems ownership + Backend reads on top.

import { dirname, stem, joinPath, replaceExtension, replaceFilename, extensionLower } from "./pathUtil";

/** Battery-save file extensions, most-preferred first. `.sav` is the default write target across
 *  GB/NES/GBA; `.srm` is the alternative some NES/risa saves ship with, so it's accepted when pairing a
 *  ROM to its battery and when loading SRAM (but new saves are still written as `.sav`). */
export const SAV_EXTS = [".sav", ".srm"] as const;

/** File-dialog globs for a battery save (`.sav` / `.srm`). */
export const SAV_PATTERNS: string[] = SAV_EXTS.map((ext) => `*${ext}`);

/** True when `path`'s extension is a recognised battery-save extension (`.sav` / `.srm`). */
export function isSavPath(path: string): boolean {
  return (SAV_EXTS as readonly string[]).includes(extensionLower(path));
}

/** `<rom>` + `ext` for suffix 0/1, or `<rom-stem>-<N><ext>` for suffix ≥ 2. Empty
 *  romPath → empty. `ext` includes the dot (e.g. `".sav"`, `".ss0"`). */
export function siblingPath(romPath: string, suffix: number, ext: string): string {
  if (!romPath) return "";
  if (suffix >= 2) return replaceFilename(romPath, `${stem(romPath)}-${suffix}${ext}`);
  return replaceExtension(romPath, ext);
}

/** The battery-save sibling: `<rom>.sav` (suffix 0/1) or `<rom>-N.sav` (≥ 2). */
export function siblingSavPath(romPath: string, suffix = 0): string {
  return siblingPath(romPath, suffix, ".sav");
}

/** The battery-save sibling CANDIDATES, most-preferred first: `<rom>.sav` then `<rom>.srm` (+ suffix),
 *  for pairing a ROM to an existing on-disk battery. The caller keeps the first that exists. */
export function siblingSavCandidates(romPath: string, suffix = 0): string[] {
  return SAV_EXTS.map((ext) => siblingPath(romPath, suffix, ext));
}

/** The auto-written project sibling `<rom>.rplg` (suffix-independent). */
export function siblingRplgPath(romPath: string): string {
  if (!romPath) return "";
  return replaceExtension(romPath, ".rplg");
}

/** The file battery I/O actually uses: the explicit `override` when set, else the
 *  suffix-derived sibling. */
export function resolveSavPath(romPath: string, suffix: number, override: string): string {
  return override || siblingSavPath(romPath, suffix);
}

/** Pick a free `.sav` suffix for a new instance of `romPath`. Slot 0 is reclaimed
 *  whenever no live system owns it (its on-disk file is deliberately NOT checked —
 *  reusing `<rom>.sav` is the normal single-instance case). Otherwise scan from 2
 *  (never 1) for the first slot that is neither owned by a live system nor already
 *  a file on disk, so an orphaned `<rom>-N.sav` from a removed instance is never
 *  clobbered. `isOwned(n)` / `existsOnDisk(n)` are the two predicates the systems
 *  domain supplies. */
export function nextFreeSavSuffix(
  romPath: string,
  isOwned: (suffix: number) => boolean,
  existsOnDisk: (suffix: number) => boolean,
): number {
  if (!romPath) return 0;
  if (!isOwned(0)) return 0;
  let n = 2;
  while (isOwned(n) || existsOnDisk(n)) n++;
  return n;
}

const ROM_EXTS = [".gb", ".gbc", ".gba", ".nes", ".sms", ".gg"];

/** The ordered ROM paths to probe when pairing a picked `.sav`, mirroring
 *  findSiblingRom: the save's own stem first, then — only when the stem ends in
 *  `-<digits>` (a duplicate slot) — the base stem; each crossed with `.gb/.gbc/
 *  .gba/.nes/.sms/.gg` (stem-outer, ext-inner). The caller keeps the first
 *  candidate that exists AND content-validates as a real ROM. */
export function siblingRomCandidates(savPath: string): string[] {
  const dir = dirname(savPath);
  const s = stem(savPath);
  const stems = [s];
  const dash = s.lastIndexOf("-");
  const tail = dash >= 0 ? s.slice(dash + 1) : "";
  if (dash >= 0 && tail.length > 0 && /^[0-9]+$/.test(tail)) stems.push(s.slice(0, dash));

  const out: string[] = [];
  for (const st of stems) {
    for (const ext of ROM_EXTS) out.push(joinPath(dir, st + ext));
  }
  return out;
}

// Pure systems-list logic: immutable ordering transforms over the live systems +
// the decision kernels the store composes. No Backend, no IO — so every rule (list
// ordering, sav-suffix ownership, the paired-sav override test, sibling-ROM pairing,
// focus fallback) is unit-testable in isolation. The store (systemsStore.ts) wires
// these to the Backend + the pure paths kernels on top.
//
// Ports: list ordering from the DSP handlers (PluginDSP.cpp:406-458); the
// assignSavSuffix ownership predicate; buildSystemFromPath's weakly_canonical
// "is this pick my sibling?" override test; findSiblingRom's candidate loop.

import { siblingSavPath, siblingRomCandidates } from "./savPaths";
import type { RomFormat } from "./romFormat";
import type { CoreSettings } from "./systemSettings";
import type { RoleInstance } from "./systemRoles";

/** A supported emulator backend (the non-"unknown" RomFormat values). */
export type SystemKind = "sameboy" | "nes" | "gba";

/** The per-system record TS owns. `savSuffix` + `savPath` (an override, `""` when the
 *  natural sibling is used) are the persistent identity. `settings` are the universal
 *  per-system knobs; `roles` are the generic per-system roles (backend "system" role +
 *  optional feature roles) — everything backend/feature-specific lives here, not in
 *  fixed fields. */
export interface SystemEntry {
  id: number;
  kind: SystemKind;
  romPath: string;
  savPath: string; // override; "" = derive from romPath + savSuffix
  savSuffix: number;
  embeddedRom: string; // "" unless a binary-baked ROM (e.g. "mgb")
  settings: CoreSettings;
  roles: RoleInstance[];
}

/** The live system with `id`, or undefined. */
export function findById(list: SystemEntry[], id: number): SystemEntry | undefined {
  return list.find((e) => e.id === id);
}

/** Append `entry` (add semantics). */
export function appendEntry(list: SystemEntry[], entry: SystemEntry): SystemEntry[] {
  return [...list, entry];
}

/** Drop the entry with `id`; survivors keep their ids and relative order. */
export function removeById(list: SystemEntry[], id: number): SystemEntry[] {
  return list.filter((e) => e.id !== id);
}

/** Swap the entry with `id` for `next`, preserving its slot index (load/replace/
 *  reload semantics). Returns the same array reference when `id` isn't present. */
export function replaceById(list: SystemEntry[], id: number, next: SystemEntry): SystemEntry[] {
  const idx = list.findIndex((e) => e.id === id);
  if (idx < 0) return list;
  const out = list.slice();
  out[idx] = next;
  return out;
}

/** True when a live system for `romPath` already holds `suffix` — the `isOwned`
 *  predicate for nextFreeSavSuffix. Exact string match on romPath (as native does). */
export function isSuffixOwned(list: SystemEntry[], romPath: string, suffix: number): boolean {
  return list.some((e) => e.romPath === romPath && e.savSuffix === suffix);
}

/** Decide whether a picked `.sav` becomes a persisted savPath override. It does NOT
 *  when it's just this instance's natural sibling — equal (after canonicalize) to the
 *  suffix-N sibling OR the plain suffix-0 sibling; then the suffix mechanism owns the
 *  file and `""` is returned. A genuinely different file returns the raw pick. Mirrors
 *  buildSystemFromPath's weakly_canonical test. */
export function resolveSavOverride(
  romPath: string,
  suffix: number,
  pickedSav: string,
  canonicalize: (p: string) => string,
): string {
  if (!pickedSav) return "";
  const picked = canonicalize(pickedSav);
  const sibN = canonicalize(siblingSavPath(romPath, suffix));
  const sib0 = canonicalize(siblingSavPath(romPath, 0));
  return picked !== sibN && picked !== sib0 ? pickedSav : "";
}

/** The sibling ROM for a picked `.sav`: the first `siblingRomCandidates` path that
 *  both `exists` and `classify`es as a real ROM (not "unknown"), else null. The pure
 *  core of findSiblingRom; the store injects `exists`/`classify` from the Backend. */
export function pickSiblingRom(
  savPath: string,
  exists: (path: string) => boolean,
  classify: (path: string) => RomFormat,
): string | null {
  for (const cand of siblingRomCandidates(savPath)) {
    if (exists(cand) && classify(cand) !== "unknown") return cand;
  }
  return null;
}

/** The focus after removing `removedId`: unchanged when a non-focused system went
 *  away, else the new front of `remaining` (or 0 when the list is now empty). */
export function nextFocusAfterRemove(
  remaining: SystemEntry[],
  removedId: number,
  focusedId: number,
): number {
  if (focusedId !== removedId) return focusedId;
  return remaining.length ? remaining[0].id : 0;
}

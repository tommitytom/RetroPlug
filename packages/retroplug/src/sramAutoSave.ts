// The loose-`.sav` auto-save (mirror) policy — a port of native's system/SramAutoSave.hpp
// write logic. Flush a system's battery RAM to its sibling <rom>.sav the way most Game
// Boy emulators do: write when the SRAM changed since the last check, seeding (not
// rewriting) an identical sibling that was just loaded. Gated on the user's `sramAutoSave`
// preference (Off / OnProjectSave / Continuous).
//
// This is pure decision logic over the live SRAM byte read: it reads SRAM via the
// existing backend.readSram(id) pump and resolves the target with resolveSavPath — no new
// native primitive. The pairing (path/suffix/override resolution) already lives in
// savPaths.ts; this is only the write policy.

import type { ControlPlaneBackend } from "./backend";
import type { SystemsStore } from "./systemsStore";
import type { UserConfigStore } from "./userConfigStore";
import { resolveSavPath } from "./savPaths";
import { decodeSong, encodeSong } from "./lsdj";

function fnvInto(h: number, bytes: Uint8Array): number {
  for (let i = 0; i < bytes.length; i++) {
    h ^= bytes[i];
    h = Math.imul(h, 0x01000193);
  }
  return h;
}

/** A stable FNV-1a (32-bit) hash of `bytes`. Used only for in-process dedup (never
 *  persisted), so it need not match native's SampleCache::hashBytes — it just has to
 *  hash live SRAM and on-disk `.sav` bytes with the same function. */
export function hashBytes(bytes: Uint8Array): number {
  return fnvInto(0x811c9dc5, bytes) >>> 0;
}

/** The MEANINGFUL-change signature of a full 128 KiB LSDj `.sav`, or `null` if `bytes`
 *  isn't one (the caller then falls back to a whole-SRAM hash). LSDj rewrites its working
 *  RAM every frame — the work/total-time clocks tick constantly — so a whole-SRAM hash reports
 *  a fresh sav as "dirty" the instant it boots. Those volatile bytes are NOT modeled by the
 *  codec, so a canonical re-encode `encodeSong(decodeSong(workingSong))` normalises them away;
 *  the header + stored-project archive (0x8000..end) is static (it changes only on an explicit
 *  LSDj SAVE) and is hashed raw. The signature is dedup-only, never persisted. */
export function lsdjSramSignature(bytes: Uint8Array): number | null {
  // A full LSDj image is 128 KiB with the 'jk' SRAM-init magic at 0x813E.
  if (bytes.length < 0x20000 || bytes[0x813e] !== 0x6a || bytes[0x813f] !== 0x6b) return null;
  let canonical: Uint8Array;
  try {
    canonical = encodeSong(decodeSong(bytes.subarray(0, 0x8000)));
  } catch {
    return null;
  }
  let h = fnvInto(0x811c9dc5, canonical);
  h = fnvInto(h, bytes.subarray(0x8000));
  return h >>> 0;
}

/** Change-detection signature for a system's SRAM: the LSDj semantic signature when `bytes`
 *  is an LSDj sav (normalising its per-frame clock churn), else a whole-SRAM hash. Used only
 *  for the dirty/dedup decision — the bytes actually written to disk are always the live SRAM. */
export function sramSignature(bytes: Uint8Array): number {
  return lsdjSramSignature(bytes) ?? hashBytes(bytes);
}

/** The per-system auto-save decision (port of autoSaveSramToSibling's core):
 *   - `lastHash === h`                          → unchanged, no write
 *   - `lastHash === null` and the on-disk file already hashes to `h` → seed, no write
 *   - otherwise                                 → write
 *  Always returns the current hash for the caller to adopt (after a successful write, or
 *  as the seed/unchanged value). */
export function decideAutoSave(
  savBytes: Uint8Array,
  lastHash: number | null,
  onDiskBytes: Uint8Array | null,
): { write: boolean; hash: number } {
  const hash = sramSignature(savBytes);
  if (lastHash !== null && lastHash === hash) return { write: false, hash };
  if (lastHash === null && onDiskBytes !== null && sramSignature(onDiskBytes) === hash) return { write: false, hash };
  return { write: true, hash };
}

/** The per-system fields the unsaved-SRAM check needs (SystemEntry / SystemView satisfy it). */
export interface SramTarget {
  id: number;
  romPath: string;
  savSuffix: number;
  savPath: string; // the paired-save override ("" = the suffix sibling)
}

/** One system whose live battery is unsaved: which system, the `.sav` a save would write, and whether
 *  that file doesn't exist yet (vs existing but differing). The detail the unsaved-changes prompt lists. */
export interface DirtySram {
  id: number;
  savPath: string;
  isNew: boolean;
}

/** Every system whose LIVE battery differs MEANINGFULLY from its `.sav` on disk - the "unsaved SRAM"
 *  signal, with the target path + whether it's a new file. Uses sramSignature, which normalises LSDj's
 *  per-frame working-RAM churn (the ticking clock) away so a just-booted LSDj cart isn't reported dirty;
 *  non-LSDj batteries use a whole-SRAM hash. Embedded ROMs (no romPath) and empty batteries are never
 *  dirty; a missing `.sav` with a non-empty battery is. */
export function dirtySramTargets(backend: ControlPlaneBackend, systems: SramTarget[]): DirtySram[] {
  const out: DirtySram[] = [];
  for (const s of systems) {
    if (!s.romPath) continue;
    const savPath = resolveSavPath(s.romPath, s.savSuffix, s.savPath);
    if (!savPath) continue;
    const live = backend.readSram(s.id);
    if (!live || live.length === 0) continue;
    const disk = backend.readFile(savPath);
    if (!disk) out.push({ id: s.id, savPath, isNew: true }); // no .sav yet, but the battery has content
    else if (sramSignature(live) !== sramSignature(disk)) out.push({ id: s.id, savPath, isNew: false });
  }
  return out;
}

/** How many systems have a live battery that differs from its on-disk `.sav`. */
export function sramDirtyCount(backend: ControlPlaneBackend, systems: SramTarget[]): number {
  return dirtySramTargets(backend, systems).length;
}

/** Write every dirty system's live battery to its sibling `.sav` (UNGATED — an explicit "save on close",
 *  unlike the auto-save mirror which respects the Off preference). Returns the number written. */
export function flushDirtySram(backend: ControlPlaneBackend, systems: SramTarget[]): number {
  let n = 0;
  for (const t of dirtySramTargets(backend, systems)) {
    const live = backend.readSram(t.id);
    if (live && backend.writeFile(t.savPath, live)) n++;
  }
  return n;
}

export class SramAutoSaver {
  // Persistent per-system hash of the last-written SRAM, used by pump() so the Continuous
  // idle-tick only writes on change. flushOnSave() uses a fresh (null) hash instead.
  private hashes = new Map<number, number>();
  // Raw whole-battery hash of what each system looked like last tick — the cheap gate in front of the
  // semantic signature. sramSignature on an LSDj cart is a full encodeSong(decodeSong(...)) round-trip over
  // 32 KB, and the Continuous pump asks per system every couple of seconds; measured on a live cart the
  // battery does not move at all during playback, so this hash answers "nothing to do" nearly every time.
  // When the raw bytes DO move we fall through to the semantic signature, which still decides whether the
  // change is meaningful. Pump-only; flushOnSave always does the full comparison.
  private rawHashes = new Map<number, number>();

  constructor(
    private readonly backend: ControlPlaneBackend,
    private readonly systems: SystemsStore,
    private readonly userConfig: UserConfigStore,
  ) {}

  /** Flush every system's battery RAM to its resolved sibling `.sav` at a save/quit
   *  moment (port of flushSramMirror): a no-op when the preference is Off; otherwise each
   *  system is seeded-or-written against its on-disk file with a fresh hash. Returns the
   *  number of systems actually written. */
  flushOnSave(): number {
    if (this.userConfig.sramAutoSave() === "Off") return 0;
    let written = 0;
    for (const sys of this.systems.systems()) {
      if (this.flushSystem(sys.id, sys.romPath, sys.savSuffix, sys.savPath, false)) written++;
    }
    return written;
  }

  /** The Continuous idle-tick: writes each system's changed SRAM using its persistent
   *  hash (no write when unchanged). A no-op unless the preference is Continuous — Off /
   *  OnProjectSave leave the loose `.sav` to flushOnSave. The caller throttles the
   *  cadence. Returns the number of systems written this tick. */
  pump(): number {
    if (this.userConfig.sramAutoSave() !== "Continuous") return 0;
    this.pruneDeadHashes();
    let written = 0;
    for (const sys of this.systems.systems()) {
      if (this.flushSystem(sys.id, sys.romPath, sys.savSuffix, sys.savPath, true)) written++;
    }
    return written;
  }

  // Resolve, read, decide, and (maybe) write one system's SRAM. `persistent` selects the
  // pump's cross-tick hash vs flushOnSave's fresh one. Returns whether it wrote.
  private flushSystem(id: number, romPath: string, savSuffix: number, savOverride: string, persistent: boolean): boolean {
    if (!romPath) return false; // embedded ROM: no sibling to mirror
    const savPath = resolveSavPath(romPath, savSuffix, savOverride);
    if (!savPath) return false;
    const savBytes = this.backend.readSram(id);
    if (!savBytes || savBytes.length === 0) return false;

    // Cheap gate (pump only): if not one byte of the battery moved since last tick, nothing can need
    // writing, and we skip the semantic signature's codec round-trip entirely. Only valid once this system
    // has been seen before - a first observation still has to consult the file on disk.
    if (persistent && this.hashes.has(id)) {
      const raw = hashBytes(savBytes);
      if (this.rawHashes.get(id) === raw) return false;
      this.rawHashes.set(id, raw);
    }

    const lastHash = persistent ? this.hashes.get(id) ?? null : null;
    // The on-disk file is only needed for the first-observation seed check.
    const onDisk = lastHash === null ? this.backend.readFile(savPath) : null;
    const decision = decideAutoSave(savBytes, lastHash, onDisk);

    if (decision.write && !this.backend.writeFile(savPath, savBytes)) return false; // retry next time
    if (persistent) {
      this.hashes.set(id, decision.hash);
      this.rawHashes.set(id, hashBytes(savBytes)); // seed/refresh the cheap gate for the next tick
    }
    return decision.write;
  }

  // Drop persistent hashes for systems that no longer exist (ids are monotonic, so this
  // only sheds removed/reloaded ones). A cold boot (reload / loadSram / a Songs-menu edit) allocates a NEW
  // id, so its state is shed here and the next tick re-seeds from the file on disk rather than writing a
  // stale snapshot back over it.
  private pruneDeadHashes(): void {
    const live = new Set(this.systems.systems().map((s) => s.id));
    for (const id of this.hashes.keys()) if (!live.has(id)) this.hashes.delete(id);
    for (const id of this.rawHashes.keys()) if (!live.has(id)) this.rawHashes.delete(id);
  }
}

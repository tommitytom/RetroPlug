// The pure core of `.lsdprj` import: given a `.lsdprj` file, the effective ROM (base + current overrides),
// the current override list, and the live SRAM, plan the new SAV image (song injected byte-level) + the
// updated override list (missing kits assigned to free slots, linking the `.lsdprj` by path + ordinal). The
// menu wraps this with IO (read files, write the .sav, setRoleConfig, loadSram). Kept pure + separate so
// it's unit-testable and the byte-level kit handling (6-bit indices the Song model can't hold) stays here.
import { decodeLsdprj, usedKitIndices, remapSongKits, injectSong, freeSong, freeSongSlot, loadSongToWorking } from "./lsdjSav";
import { LsdjRom, KIT_COUNT, KIT_NAME_OFFSET, KIT_NAME_SIZE } from "./lsdj/rom";
import type { LsdjAssetOverride } from "./lsdjAssetsRole";

/** The 6-char kit name stored in a raw 16 KB kit bank (in-bank offset 0x52). */
export function kitBankName(bank: Uint8Array): string {
  let s = "";
  for (let i = 0; i < KIT_NAME_SIZE; i++) {
    const c = bank[KIT_NAME_OFFSET + i];
    if (c === 0) break;
    s += String.fromCharCode(c);
  }
  return s.trim();
}

const banksEqual = (a: Uint8Array, b: Uint8Array): boolean => a.length === b.length && a.every((v, i) => v === b[i]);

export interface LsdprjImportPlan {
  savBytes: Uint8Array; // the live SRAM with the imported song injected
  overrides: LsdjAssetOverride[]; // current overrides + any new kit overrides (link the .lsdprj by path)
  songSlot: number; // the slot the song landed in
  addedKits: number; // how many new kit overrides were recorded
}

export interface LsdprjImportArgs {
  file: Uint8Array; // the .lsdprj bytes
  path: string; // its on-disk path (stored in new kit overrides)
  effectiveRom: Uint8Array; // base ROM + current overrides applied (for kit dedupe + free-slot finding)
  overrides: LsdjAssetOverride[]; // current lsdj-assets overrides
  liveSram: Uint8Array; // the target SAV
  targetSlot?: number; // Replace → this slot (freed first); Add → first free slot
}

/** Plan a `.lsdprj` import (see module header). Returns null on any failure (malformed file, out of kit or
 *  song slots, song won't fit) — the caller then writes nothing. */
export function planLsdprjImport(args: LsdprjImportArgs): LsdprjImportPlan | null {
  let prj;
  try {
    prj = decodeLsdprj(args.file);
  } catch {
    return null;
  }

  const eff = LsdjRom.fromBytes(args.effectiveRom);
  if (!eff.isLsdj) return null;

  // Assign each kit bank: reuse an identical existing kit, else the first free slot (record an override
  // linking the .lsdprj by path + ordinal). Occupy the slot in `eff` so later banks see it.
  const used = usedKitIndices(prj.songBytes);
  const map = new Map<number, number>();
  const overrides = [...args.overrides];
  let addedKits = 0;
  const n = Math.min(prj.kitBanks.length, used.length);
  for (let i = 0; i < n; i++) {
    const bank = prj.kitBanks[i];
    let slot = -1;
    for (let k = 0; k < KIT_COUNT; k++) {
      if (eff.kit(k).valid && banksEqual(eff.exportKitFile(k), bank)) {
        slot = k;
        break;
      }
    }
    if (slot < 0) {
      for (let k = 0; k < KIT_COUNT; k++) {
        if (!eff.kit(k).valid) {
          slot = k;
          break;
        }
      }
      if (slot < 0) return null; // out of kit slots
      eff.importKitFile(slot, bank);
      overrides.push({ type: "kit", slot, name: kitBankName(bank) || `kit${i}`, path: args.path, lsdprjKit: i });
      addedKits++;
    }
    map.set(used[i], slot);
  }
  remapSongKits(prj.songBytes, map);

  const songSlot = args.targetSlot ?? freeSongSlot(args.liveSram);
  if (songSlot < 0) return null; // out of song slots
  const base = args.targetSlot != null ? freeSong(args.liveSram, args.targetSlot) : args.liveSram;
  const injected = injectSong(base, songSlot, prj.name, prj.version, prj.songBytes);
  if (!injected) return null; // out of blocks
  // Load the imported song into working memory so the reboot shows it.
  const savBytes = loadSongToWorking(injected, songSlot) ?? injected;

  return { savBytes, overrides, songSlot, addedKits };
}

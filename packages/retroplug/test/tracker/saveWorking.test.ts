// "Save to catalog" - the third button on the Load confirm, and the only one that PRESERVES the work.
// The property that matters: after saving, the working song must read CLEAN (it is now committed), and
// every other saved song must be untouched. If saving didn't actually clear the dirty flag, the user
// would save, be prompted again, and reasonably conclude the save did nothing.
import { test, expect } from "../../testing/harness";
import { lsdjSongCatalog, risaSongCatalog } from "../../src/tracker";
import { savBytes as lsdjSav } from "../lsdj/fixtures";
import { savBytes as risaSav } from "../risa/fixtures";
import { loadSongToWorking, decompressSlot, savSongName } from "../../src/lsdj/codec/sav";
import { saveWorkingToCatalog, canSaveWorkingToCatalog } from "../../src/lsdjSongOps";
import { saveWorkingToCatalog as risaSaveWorking, loadSongToWorkingInSav, workingSongSlot } from "../../src/risaSongOps";
import { sameBytes } from "../_bytes";
import { normalizeSaveContainer } from "../../src/risaSav";

test("lsdj: saving a LINKED working song updates its own slot and clears the dirty flag", () => {
  const sav = lsdjSav("all");
  const slot = lsdjSongCatalog.list(sav)[0].index;
  const name = savSongName(sav, slot);
  const other = lsdjSongCatalog.list(sav)[1];
  const otherBefore = decompressSlot(sav, other.index)!;

  // Load, then edit - the "worked on it for an hour" state.
  const edited = loadSongToWorking(sav, slot)!;
  edited[0x100] ^= 0xff;
  expect(lsdjSongCatalog.workingSongDirty!(edited)).toBe(true);

  const saved = saveWorkingToCatalog(edited)!;
  expect(saved != null).toBeTruthy();
  // Now committed: the prompt must not fire again.
  expect(lsdjSongCatalog.workingSongDirty!(saved)).toBe(false);
  // It updated its OWN slot (no duplicate), keeping the name.
  expect(lsdjSongCatalog.list(saved).length).toBe(lsdjSongCatalog.list(sav).length);
  expect(savSongName(saved, slot)).toBe(name);
  // The stored slot now holds the edit.
  expect(sameBytes(decompressSlot(saved, slot)!, saved.subarray(0, 0x8000))).toBe(true);
  // Every other song is byte-for-byte untouched.
  expect(sameBytes(decompressSlot(saved, other.index)!, otherBefore)).toBe(true);
});

test("lsdj: saving an UNLINKED working song claims a free slot under the given name", () => {
  const sav = lsdjSav("all");
  const before = lsdjSongCatalog.list(sav).length;
  const unlinked = loadSongToWorking(sav, lsdjSongCatalog.list(sav)[0].index)!;
  unlinked[0x100] ^= 0xff; // edited, so it no longer matches ANY slot...
  unlinked[0x8140] = 0xff; // ...and names none either: committed nowhere
  expect(lsdjSongCatalog.workingSongDirty!(unlinked)).toBe(true);
  expect(canSaveWorkingToCatalog(unlinked)).toBe(true);

  const saved = saveWorkingToCatalog(unlinked, "NEWSONG")!;
  expect(saved != null).toBeTruthy();
  expect(lsdjSongCatalog.workingSongDirty!(saved)).toBe(false);
  expect(lsdjSongCatalog.list(saved).length).toBe(before + 1);
  expect(lsdjSongCatalog.list(saved).some((s) => s.name === "NEWSONG")).toBe(true);
});

test("lsdj: a full catalog reports it can't take an unlinked song rather than failing silently", () => {
  // 32 occupied slots: every alloc-table entry owned, so freeSongSlot finds nothing.
  const sav = lsdjSav("all").slice();
  for (let i = 0; i < 191; i++) sav[0x8100 + i] = i % 32;
  const unlinked = sav.slice();
  unlinked[0x8140] = 0xff;
  expect(canSaveWorkingToCatalog(unlinked)).toBe(false);
  expect(saveWorkingToCatalog(unlinked, "NOPE")).toBe(null);
});

test("risa: Load links the working song to the slot it came from (the cart's own Load does the same)", () => {
  const sav = risaSav("v2_blumarbl");
  const first = risaSongCatalog.list(sav)[0];
  const loaded = loadSongToWorkingInSav(sav, first.index)!;
  // Without this the working song is orphaned from the slot it IS, and a save appends a byte-identical
  // duplicate - the state the whole working-song row used to be gated on.
  expect(workingSongSlot(loaded)).toBe(first.index);
  expect(risaSongCatalog.workingSongDirty!(loaded)).toBe(false); // nothing to lose, so no row and no prompt
});

test("risa: saving a LINKED working song updates in place instead of appending a duplicate", () => {
  const sav = risaSav("v2_blumarbl");
  const first = risaSongCatalog.list(sav)[0];
  const linked = loadSongToWorkingInSav(sav, first.index)!; // Load links it
  linked[0x40] ^= 0xff; // edited for an hour: linked, but no longer what the slot holds
  expect(risaSongCatalog.workingSongDirty!(linked)).toBe(true);

  const count = risaSongCatalog.list(linked).length;
  const saved = risaSaveWorking(linked);
  // In place: same slot count, still linked to the same slot, and clean.
  expect(risaSongCatalog.list(saved).length).toBe(count);
  expect(workingSongSlot(saved)).toBe(first.index);
  expect(risaSongCatalog.workingSongDirty!(saved)).toBe(false);
});

test("risa: saving an UNLINKED working song appends a new slot and links it", () => {
  const sav = risaSav("v2_blumarbl");
  const loaded = loadSongToWorkingInSav(sav, risaSongCatalog.list(sav)[0].index)!;
  loaded[0x2000 + 0x1e94] = 0xff; // arrived unlinked (an imported .sav, or a pre-stamp battery)...
  loaded[0x40] ^= 0xff; // ...and edited, so it matches no slot: genuinely uncommitted work
  expect(risaSongCatalog.workingSongDirty!(loaded)).toBe(true);

  const count = risaSongCatalog.list(loaded).length;
  const saved = risaSaveWorking(loaded);
  expect(risaSongCatalog.list(saved).length).toBe(count + 1);
  expect(workingSongSlot(saved)).toBe(count); // linked to the slot it landed in
  expect(risaSongCatalog.workingSongDirty!(saved)).toBe(false);
});

test("risa: saving an UNLINKED working song that a slot ALREADY holds adopts that slot, never duplicates", () => {
  const sav = risaSav("v2_blumarbl");
  const first = risaSongCatalog.list(sav)[0];
  const orphan = loadSongToWorkingInSav(sav, first.index)!;
  orphan[0x2000 + 0x1e94] = 0xff; // unlinked, but the content IS slot 0 - appending would clone it
  const count = risaSongCatalog.list(orphan).length;

  const saved = risaSaveWorking(orphan);
  expect(risaSongCatalog.list(saved).length).toBe(count); // no new song
  expect(workingSongSlot(saved)).toBe(first.index); // adopted the slot that already held it
  expect(risaSongCatalog.workingSongDirty!(saved)).toBe(false);
});

test("lsdj: an unlinked save with no name declines rather than creating a blank-named slot", () => {
  const sav = lsdjSav("all");
  const unlinked = loadSongToWorking(sav, lsdjSongCatalog.list(sav)[0].index)!;
  unlinked[0x100] ^= 0xff;
  unlinked[0x8140] = 0xff;
  // A song saved under a blank name is barely findable in the list again, so refuse instead.
  expect(saveWorkingToCatalog(unlinked)).toBe(null);
  expect(saveWorkingToCatalog(unlinked, "")).toBe(null);
  // A LINKED song still needs no name - it inherits its slot's.
  const linked = loadSongToWorking(sav, lsdjSongCatalog.list(sav)[0].index)!;
  linked[0x100] ^= 0xff;
  expect(saveWorkingToCatalog(linked) != null).toBe(true);
});

test("risa: a legacy-layout catalog is never updated in place (append or nothing)", () => {
  // A legacy catalog lives at 0x6000 and does not reserve bank 1, so the 'current entry' offset there holds
  // arbitrary working-song bytes. Treating that as a record index would overwrite a legacy record at a
  // meaningless position - the in-place branch must decline and leave every existing record intact.
  const legacy = normalizeSaveContainer(risaSav("multi_legacy")).save;
  legacy[0x2000 + 0x1e94] = 2; // a plausible-looking "current entry" that means nothing here
  const before = risaSongCatalog.list(legacy).map((s) => s.name);

  let after: string[];
  try {
    after = risaSongCatalog.list(risaSaveWorking(legacy)).map((s) => s.name);
  } catch {
    after = before; // declining outright is also acceptable - what matters is no in-place clobber
  }
  // Every original song survives, in order. An append may add one on the end; nothing may be overwritten.
  expect(after.slice(0, before.length)).toEqual(before);
});

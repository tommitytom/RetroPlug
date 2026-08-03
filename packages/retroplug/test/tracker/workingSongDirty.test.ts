// The confirm-on-Load gate: `workingSongDirty` must never cry wolf. A prompt that fires when nothing
// would be lost teaches users to dismiss it, which is worse than no prompt at all - so the headline
// property here is NO FALSE POSITIVES: immediately after loading a song into working memory, the working
// song IS that slot, and the catalog must say clean. The mirror property (a real edit is caught) guards
// the other direction, where the cost is silent data loss.
//
// Live-cart stability (the clock ticking under a running core) is the twin of this file in
// test-native/lsdj-working-dirty.test.ts.
import { test, expect } from "../../testing/harness";
import { lsdjSongCatalog, risaSongCatalog } from "../../src/tracker";
import { savBytes as lsdjSav } from "../lsdj/fixtures";
import { savBytes as risaSav } from "../risa/fixtures";
import { loadSongToWorking } from "../../src/lsdj/codec/sav";
import { loadSongToWorkingInSav, workingSongSlot } from "../../src/risaSongOps";
import { normalizeSaveContainer } from "../../src/risaSav";
import { BANK_DATA, WRAM_BANK_SIZE, SAVE_CURRENT_ENTRY_OFFSET } from "../../src/risa/codec/constants";

const LSDJ_KEYS = ["all", "happy_birthday", "lsdj499", "lsdj620", "lsdj668", "lsdj671", "lsdj690", "lsdj732", "lsdj790", "lsdj798", "lsdj834", "lsdj888"];

test("lsdj: a freshly loaded song is NEVER reported dirty (no false positives, whole corpus)", () => {
  let checked = 0;
  for (const key of LSDJ_KEYS) {
    const sav = lsdjSav(key);
    for (const s of lsdjSongCatalog.list(sav)) {
      const loaded = loadSongToWorking(sav, s.index);
      if (!loaded) continue; // empty slot
      expect(lsdjSongCatalog.workingSongDirty!(loaded)).toBe(false);
      checked++;
    }
  }
  console.log(`[working-dirty] lsdj: ${checked} slot loads across ${LSDJ_KEYS.length} savs, all clean`);
  expect(checked > 0).toBe(true);
});

test("lsdj: an edit to working memory IS caught, and an unlinked working song is always dirty", () => {
  const sav = lsdjSav("all");
  const slot = lsdjSongCatalog.list(sav)[0].index;
  const loaded = loadSongToWorking(sav, slot)!;
  expect(lsdjSongCatalog.workingSongDirty!(loaded)).toBe(false);

  // Poke a byte of the working song (0x0000..0x8000) - the song no longer matches its slot.
  const edited = loaded.slice();
  edited[0x100] ^= 0xff;
  expect(lsdjSongCatalog.workingSongDirty!(edited)).toBe(true);

  // Unlinked but the CONTENT still lives in a slot: not lost work, so no warning. "Is it committed
  // anywhere" beats "does it name a slot" - the latter would fire after loads that lose nothing.
  const unlinkedButSaved = loaded.slice();
  unlinkedButSaved[0x8140] = 0xff;
  expect(lsdjSongCatalog.workingSongDirty!(unlinkedButSaved)).toBe(false);

  // Unlinked AND edited: committed nowhere, so it really would be lost.
  const unlinkedAndEdited = edited.slice();
  unlinkedAndEdited[0x8140] = 0xff;
  expect(lsdjSongCatalog.workingSongDirty!(unlinkedAndEdited)).toBe(true);
});

test("risa: a freshly loaded song is NEVER reported dirty (no false positives)", () => {
  let checked = 0;
  for (const key of ["v2_blumarbl", "legacy_4xtreme", "multi_legacy"]) {
    const sav = risaSav(key);
    for (const s of risaSongCatalog.list(sav)) {
      const loaded = loadSongToWorkingInSav(sav, s.index);
      if (!loaded) continue; // legacy layout / malformed record - load declines, nothing to assert
      // Clean BOTH as loaded (risa's load leaves 'current entry' unstamped, so this is the unlinked-but-
      // committed path) and once linked - a Load must never warn about the song it just put there.
      expect(risaSongCatalog.workingSongDirty!(loaded)).toBe(false);
      const linked = loaded.slice();
      linked[BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET] = s.index;
      expect(risaSongCatalog.workingSongDirty!(linked)).toBe(false);
      checked++;
    }
  }
  console.log(`[working-dirty] risa: ${checked} slot loads, all clean`);
  expect(checked > 0).toBe(true);
});

test("risa: an edit is caught whether or not the working song names a slot", () => {
  const sav = risaSav("v2_blumarbl");
  const first = risaSongCatalog.list(sav)[0];
  const loaded = loadSongToWorkingInSav(sav, first.index)!;

  // risa's load leaves 'current entry' at 0xff, so this is the unlinked path - and it must still be clean,
  // because the content is exactly what slot `first.index` holds.
  expect(workingSongSlot(loaded)).toBe(-1);
  expect(risaSongCatalog.workingSongDirty!(loaded)).toBe(false);

  const linked = loaded.slice();
  linked[BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET] = first.index;
  expect(risaSongCatalog.workingSongDirty!(linked)).toBe(false);

  // Poke a song byte in bank 0 (chains) - a real edit the firmware would see. Caught on both paths: the
  // linked one compares against its own slot, the unlinked one finds no slot that matches.
  const editedLinked = normalizeSaveContainer(linked).save;
  editedLinked[0x40] ^= 0xff;
  expect(risaSongCatalog.workingSongDirty!(editedLinked)).toBe(true);

  const editedUnlinked = normalizeSaveContainer(loaded).save;
  editedUnlinked[0x40] ^= 0xff;
  expect(risaSongCatalog.workingSongDirty!(editedUnlinked)).toBe(true);
});

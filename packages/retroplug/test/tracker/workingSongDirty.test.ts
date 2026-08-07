// The confirm-on-Load gate: `workingSongDirty` must never cry wolf. A prompt that fires when nothing
// would be lost teaches users to dismiss it, which is worse than no prompt at all - so the headline
// property here is NO FALSE POSITIVES: immediately after loading a song into working memory, the working
// song IS that slot, and the catalog must say clean. The mirror property (a real edit is caught) guards
// the other direction, where the cost is silent data loss.
//
// Live-cart stability (the clock ticking under a running core) is the twin of this file in
// test-native/tracker-working-dirty.test.ts.
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

  // Unlinked is dirty for LSDj, WITHOUT scanning the catalog for a matching slot - unlike risa's twin,
  // which needs that scan because its load leaves the working song unlinked. LSDj's load always stamps
  // activeProjectIndex, so unlinked means a fresh sav or a deleted active slot: the content really is gone.
  // The scan cost 74 ms on a full 32-song sav on a per-menu-build path, to suppress a prompt in a case
  // LSDj does not produce. Both a pristine and an edited working song answer dirty here.
  const unlinkedPristine = loaded.slice();
  unlinkedPristine[0x8140] = 0xff;
  expect(lsdjSongCatalog.workingSongDirty!(unlinkedPristine)).toBe(true);

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

// --- robustness: this runs on EVERY menu build, so it must never throw --------------------------------
// decompressProject (LSDj) and parseCatalog (risa) both throw on malformed input, and a battery can be
// corrupt, hand-edited, or simply not the console we think it is. A throw here would take the whole menu
// down; the safe degraded answer is "dirty", which warns rather than silently discarding.

test("lsdj: a corrupt or undersized battery answers instead of throwing", () => {
  const good = loadSongToWorking(lsdjSav("all"), 0)!;

  // Block data that decompressProject THROWS on (verified: "decompress overflowed 0x8000 bytes"). Both
  // paths must land on DIRTY, because a slot that cannot be read cannot vouch for the working song.
  // Asserting the DIRECTION is the point: "fail toward warning" is the contract, and a weaker check like
  // `typeof x === "boolean"` holds just as well when a decode failure silently reports clean - which would
  // discard the song with no prompt, the one outcome that costs the user work.
  const linkedCorrupt = good.slice(); // activeProjectIndex still names slot 0
  linkedCorrupt.fill(0, 0x8200); // wipe the block area
  expect(lsdjSongCatalog.workingSongDirty!(linkedCorrupt)).toBe(true);

  const unlinkedCorrupt = linkedCorrupt.slice();
  unlinkedCorrupt[0x8140] = 0xff; // now the SCAN runs, and every slot it tries throws
  expect(lsdjSongCatalog.workingSongDirty!(unlinkedCorrupt)).toBe(true);

  // An active pointer naming a slot that owns nothing.
  const danglingActive = good.slice();
  danglingActive[0x8140] = 31; // no blocks are tagged 31 in this fixture
  expect(lsdjSongCatalog.workingSongDirty!(danglingActive)).toBe(true);

  // Too small to be a sav at all: no signal, so no warning.
  expect(lsdjSongCatalog.workingSongDirty!(new Uint8Array(16))).toBe(false);
  expect(lsdjSongCatalog.workingSongDirty!(new Uint8Array(0))).toBe(false);
});

test("risa: an unrecognized container or legacy layout answers instead of throwing", () => {
  // Not a risa container size at all, and a 64 KB buffer carrying no RSAV catalog: both stop at the
  // layout gate, so there is no catalog to reason about and nothing to warn about.
  expect(risaSongCatalog.workingSongDirty!(new Uint8Array(123))).toBe(false);
  expect(risaSongCatalog.workingSongDirty!(new Uint8Array(0x10000))).toBe(false);
  // Legacy layout overlaps the working-song banks, so there is nothing meaningful to compare.
  expect(risaSongCatalog.workingSongDirty!(risaSav("legacy_4xtreme"))).toBe(false);
});

test("risa: a catalog that fails to PARSE degrades to dirty (fail toward warning)", () => {
  // Reaches the unlinked scan and throws inside it, which the earlier layout-gate cases never do: valid
  // RSAV magic + version so chooseCatalogLayout accepts it, an unlinked working song so the scan runs, and
  // a record walk that parseCatalog rejects (a count claiming more records than `used` can hold).
  const save = normalizeSaveContainer(risaSav("v2_blumarbl")).save;
  save[BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET] = 0xff;
  save[0x8000 + 5] = 0xff; // count = 255, far beyond the used-byte region
  expect(risaSongCatalog.workingSongDirty!(save)).toBe(true);
});

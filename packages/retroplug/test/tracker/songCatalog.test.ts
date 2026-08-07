// The tracker song-catalog layer: resolution by marker role + the risa adapter's list/workingName over a
// real battery fixture. The LSDj/risa workingName round-trips against a booted core are in test-native.
import { test, expect } from "../../testing/harness";
import { resolveSongCatalog, lsdjSongCatalog, risaSongCatalog } from "../../src/tracker";
import { loadSongToWorkingInSav } from "../../src/risaSongOps";
import { normalizeSaveContainer } from "../../src/risaSav";
import { BANK_DATA, WRAM_BANK_SIZE, SAVE_CURRENT_ENTRY_OFFSET } from "../../src/risa/codec/constants";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import { savBytes } from "../risa/fixtures";

test("resolveSongCatalog selects the catalog by marker role (undefined for a non-tracker system)", () => {
  expect(resolveSongCatalog([{ kind: "lsdj-sync", config: {} }])).toBe(lsdjSongCatalog);
  expect(resolveSongCatalog([{ kind: "risa", config: {} }, { kind: "risa-assets", config: {} }])).toBe(risaSongCatalog);
  expect(resolveSongCatalog([{ kind: "mesen", config: {} }])).toBe(undefined);
  expect(lsdjSongCatalog.markerRole).toBe("lsdj-sync");
  expect(risaSongCatalog.markerRole).toBe("risa");
  expect(risaSongCatalog.reorder != null).toBe(true); // risa is positional
  expect(lsdjSongCatalog.reorder != null).toBe(true); // LSDj reorders by swapping saved slots
});

test("lsdj catalog lists projects + reads the active (working) song name", () => {
  const song = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };
  const sav = savFrom({ activeProjectIndex: 0, projects: [{ name: "HELLO", version: 0, song }] } as SavInput);
  expect(lsdjSongCatalog.list(sav).some((s) => s.index === 0 && s.name === "HELLO")).toBe(true);
  expect(lsdjSongCatalog.workingName(sav)).toBe("HELLO");
});

test("risa catalog lists the RSAV catalog + reads the working-song name after a Load", () => {
  const battery = normalizeSaveContainer(savBytes("v2_blumarbl")).save;
  expect(risaSongCatalog.list(battery).map((s) => s.name)).toEqual(["BLUMARBL"]);
  // Promote song 0 into working memory → workingName reads the N8T working-song name back.
  const loaded = loadSongToWorkingInSav(battery, 0)!;
  expect(loaded).toBeTruthy();
  expect(risaSongCatalog.workingName(loaded)).toBe("BLUMARBL");
});

test("risaSongCatalog.workingSong reports name + LINK state, and leaves the show/hide call to workingSongDirty", () => {
  const curOff = BANK_DATA * WRAM_BANK_SIZE + SAVE_CURRENT_ENTRY_OFFSET;
  const saved = normalizeSaveContainer(savBytes("v2_blumarbl")).save; // curEntry 0 → linked to slot 0
  expect(risaSongCatalog.workingSong!(saved)).toEqual({ name: "BLUMARBL", linked: true });

  // Unlinking changes the LINK state it reports - and nothing else. It does not claim the song is worth a
  // row: on a battery whose working song IS slot 0 (what a Load leaves behind), the content is still
  // committed, and workingSongDirty - the row's actual gate - says so.
  const unlinked = loadSongToWorkingInSav(saved, 0)!;
  unlinked[curOff] = 0xff;
  expect(risaSongCatalog.workingSong!(unlinked)).toEqual({ name: "BLUMARBL", linked: false });
  expect(risaSongCatalog.workingSongDirty!(unlinked)).toBe(false); // committed to slot 0 → no row, no prompt

  // LSDj deliberately does not implement it (its working song is a copy of a saved slot, and an unlinked one
  // has no name to show - LSDj keeps names on the stored project, not in the song).
  expect(lsdjSongCatalog.workingSong).toBe(undefined);
});

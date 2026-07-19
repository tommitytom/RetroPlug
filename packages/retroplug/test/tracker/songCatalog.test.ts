// The tracker song-catalog layer: resolution by marker role + the risa adapter's list/workingName over a
// real battery fixture. The LSDj/risa workingName round-trips against a booted core are in test-native.
import { test, expect } from "../../testing/harness";
import { resolveSongCatalog, lsdjSongCatalog, risaSongCatalog } from "../../src/tracker";
import { loadSongToWorkingInSav } from "../../src/risaSongOps";
import { normalizeSaveContainer } from "../../src/risaSav";
import { savFrom, type SavInput } from "../../src/lsdjSav";
import { savBytes } from "../risa/fixtures";

test("resolveSongCatalog selects the catalog by marker role (undefined for a non-tracker system)", () => {
  expect(resolveSongCatalog([{ kind: "lsdj-sync", config: {} }])).toBe(lsdjSongCatalog);
  expect(resolveSongCatalog([{ kind: "risa", config: {} }, { kind: "risa-assets", config: {} }])).toBe(risaSongCatalog);
  expect(resolveSongCatalog([{ kind: "mesen", config: {} }])).toBe(undefined);
  expect(lsdjSongCatalog.markerRole).toBe("lsdj-sync");
  expect(risaSongCatalog.markerRole).toBe("risa");
  expect(risaSongCatalog.reorder != null).toBe(true); // risa is positional
  expect(lsdjSongCatalog.reorder).toBe(undefined); // LSDj slots are fixed-index
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

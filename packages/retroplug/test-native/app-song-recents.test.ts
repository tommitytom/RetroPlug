// Song rows in the Recent list, proven against a REAL SameBoy core. The mock-tier tests cover the list
// logic; what only a real core can show is the loop the feature actually rides: a song load writes the
// battery + cold-boots the cart, the core PUBLISHES that battery to the snapshot registry, and
// recordCurrentSong reads the published bytes back and records the row. That read-back is exactly what
// catches a song the user loads on LSDj's own FILE screen (identical battery bytes - LSDj's load writes
// the same active-project byte), which nothing else in the app can see.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { buildAppRegistry } from "../src/appHost";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { loadSongByName, lsdjSongCatalog } from "../src/tracker";
import { savFrom, type SavInput } from "../src/lsdjSav";

declare const __RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";

const SONG = { formatVersion: 22, rows: [{ chains: [0] }], chains: [{ phrases: [0] }], phrases: [{ notes: [1], instruments: [0] }], instruments: [{ type: "pulse" as const }] };

test("a song change records its own recents row, read back from the real core's battery", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP app-song-recents: LSDj ROM not found at ${LSDJ}`);
    return; // resource-less environment - the devcontainer has it
  }

  // A private copy of the cart with a two-song battery beside it, so the test owns both files.
  const rom = __CONFIG_DIR__ + "/song-recents.gb";
  const sav = __CONFIG_DIR__ + "/song-recents.sav";
  expect(be.writeFile(rom, be.readFile(LSDJ)!)).toBeTruthy();
  const battery = savFrom({
    activeProjectIndex: 0, // GRUB is the working song
    projects: [
      { name: "GRUB", version: 0, song: SONG },
      { name: "INTRO", version: 0, song: SONG },
    ],
  } as SavInput);
  expect(be.writeFile(sav, battery)).toBeTruthy();

  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, buildAppRegistry());
  expect(typeof project.systems.addSystem(rom)).toBe("number");
  const projPath = __CONFIG_DIR__ + "/song-recents.rplg";
  expect(project.save(projPath)).toBeTruthy();

  const songs = () => recent.view().map((v) => v.song);
  expect(songs()).toEqual(["GRUB"]); // the save recorded the cart's working song
  expect(project.recordCurrentSong()).toBeFalsy(); // unchanged → no second row, no write

  // Load the other song the way the Recent list does. The core reboots from the written battery and
  // publishes it, so the poll now sees INTRO - the same state LSDj's own FILE-screen load produces.
  const sys = project.systems.systems()[0];
  expect(loadSongByName(be, project.systems, sys, "INTRO")).toBeTruthy();
  expect(lsdjSongCatalog.workingName(project.systems.readSram(project.systems.systems()[0].id)!)).toBe("INTRO");

  expect(project.recordCurrentSong()).toBeTruthy();
  expect(songs()).toEqual(["INTRO", "GRUB"]); // a row each, newest first

  // Back to GRUB: its row moves up rather than duplicating.
  expect(loadSongByName(be, project.systems, project.systems.systems()[0], "GRUB")).toBeTruthy();
  expect(project.recordCurrentSong()).toBeTruthy();
  expect(songs()).toEqual(["GRUB", "INTRO"]);
  expect(recent.view().every((v) => v.path === be.canonicalize(projPath))).toBeTruthy(); // one project, two rows
});

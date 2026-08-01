// Validates the ported legacy offset table (plan follow-up) on a real OLD-ERA core: LSDj v6.9.0 uses the
// 232-block that my detector never verified (it only ran on v8+, which use 0xC0E0). Boots the real
// v6.9.0 ROM + its own sav, identifies it, resolves the ported layout, and confirms the ported SONG
// cursor offset (0xC377/0xC378) is LIVE-correct: pressing DOWN moves the cursor-row byte there. The
// cursor needs no playback (unlike the playing flags), so this works on any real sav. active=0xE8 is
// asserted through the resolved layout (the old tool measured active + cursor together).
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { MemoryRegion } from "../src/backend";
import { LsdjReader } from "../src/lsdj/runtime";

declare const __RESOURCES_DIR__: string;
const ROM = __RESOURCES_DIR__ + "/roms/lsdj/lsdj6_9_0.gb";
const SAV = __RESOURCES_DIR__ + "/roms/lsdj/lsdj6_9_0.sav";
const DOWN = 3;
// v6.9.0 legacy row: [active 232, phraseRows 364, chainRows 380, songRows 512, songCursorCol 887, songCursorRow 888].
const SONG_CURSOR_ROW = 888; // 0xC378

test("ported legacy offsets: v6.9.0 resolves the 232-block and its SONG cursor is live-correct", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM) || !be.fileExists(SAV)) {
    console.log(`# SKIP lsdj-legacy-offsets: v6.9.0 ROM/sav not found`);
    return;
  }
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({ romPath: ROM, platform: "gb", core: "sameboy", embeddedRom: "", savPath: SAV, statePath: null }, id)).toBeTruthy();

  const reader = LsdjReader.fromHeader(be.readFilePrefix(ROM, 0x150)!);
  expect(reader.supported).toBeTruthy();
  expect(reader.version!.major).toBe(6);
  expect(reader.version!.minor).toBe(9);
  // The port wired end-to-end: 232-block playing-flag offset (0xE8), NOT the modern 0xE0; song cursor
  // from the ported row.
  expect(reader.layout!.active).toBe(0x0e8);
  expect(reader.layout!.cursors!.song).toEqual({ col: 887, row: 888 });

  audio.renderAudio(6000); // to the SONG screen from the real sav (skips the self-test)
  const before = be.readMemory(id, MemoryRegion.Ram)!;
  // Two DOWN presses move the SONG cursor row down by 2 at the ported offset.
  for (let n = 0; n < 2; n++) {
    audio.pressButton(id, DOWN, true);
    audio.renderAudio(70);
    audio.pressButton(id, DOWN, false);
    audio.renderAudio(70);
  }
  const after = be.readMemory(id, MemoryRegion.Ram)!;
  console.log(`[lsdj-legacy] v6.9.0 songCursorRow(0xC378) ${before[SONG_CURSOR_ROW]} -> ${after[SONG_CURSOR_ROW]}`);
  // The ported cursor-row byte tracked the DOWN presses (moved by exactly 2) — proving the ported
  // drifting-block offset is correct on a live v6 core, an era the detector never covered.
  expect(after[SONG_CURSOR_ROW]).toBe(before[SONG_CURSOR_ROW] + 2);
});

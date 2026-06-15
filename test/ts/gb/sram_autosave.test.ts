// SRAM auto-save: the shared flush helper (the same one the plugin's idle pump
// drives) creates the sibling <rom>.sav, keeps it up to date when the battery
// changes, and skips writing when it hasn't. Run: pnpm test:cli gb/sram_autosave

import { test, expect, emu } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const ROM  = "/tmp/rp_autosave.gb";
const SAV  = "/tmp/rp_autosave.sav"; // sibling auto-save target

function authoredSav(note: number): ArrayBuffer {
  return emu.savFromJson(JSON.stringify({
    workingSong: {
      formatVersion: 22,
      rows:    [{ chains: [0] }],
      chains:  [{ phrases: [0] }],
      phrases: [{ notes: [note], instruments: [0] }],
      instruments: [{ type: "pulse" }],
    },
  }));
}

function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

test("auto-save creates the sibling .sav, updates on change, skips when unchanged", () => {
  // Stage a ROM at a writable path and make sure no sibling .sav exists yet.
  emu.writeFile(ROM, emu.readFile(LSDJ).buffer);
  emu.removeFile(SAV);

  const sys = emu.loadRom(ROM);
  emu.runMs(500);

  // Dirty the live battery to a known image, then auto-save.
  const first = authoredSav(1);
  emu.loadSram(sys, first);
  expect(emu.autoSaveSram(sys)).toBeTruthy();      // create-if-missing

  // The sibling now exists and matches the system's serialized battery RAM.
  const onDisk = emu.readFile(SAV);
  expect(onDisk.length).toBeGreaterThan(0);
  expect(sameBytes(onDisk, emu.saveSram(sys))).toBeTruthy();

  // Nothing changed since the last flush -> no write.
  expect(emu.autoSaveSram(sys)).toBeFalsy();        // dedup

  // Change the battery -> the next flush writes again and the file updates.
  const second = authoredSav(7);
  emu.loadSram(sys, second);
  expect(emu.autoSaveSram(sys)).toBeTruthy();        // keep-updated
  expect(sameBytes(emu.readFile(SAV), emu.saveSram(sys))).toBeTruthy();

  // The two authored images really differ, so the update was meaningful.
  expect(sameBytes(new Uint8Array(first), new Uint8Array(second))).toBeFalsy();
});

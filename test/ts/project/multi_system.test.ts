// A multi-system path-only JSON project: two systems, each with its own staged
// ROM + sibling .sav. The thin save references both ROM paths (no embedded
// bytes); on load each system is rebuilt from its path and its battery RAM from
// the matching sibling .sav. Stresses the autodetecting loader + multi-system
// addSystem path. Run via: pnpm test:cli project/multi_system

import { test, expect, emu } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const ROM_A = "/tmp/rp_multi_a.gb";
const ROM_B = "/tmp/rp_multi_b.gb";
const SAV_A = "/tmp/rp_multi_a.sav";
const SAV_B = "/tmp/rp_multi_b.sav";
const PROJ  = "/tmp/rp_multi.rplg";

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

test("two-system thin project reloads each ROM + sibling .sav", () => {
  const rom = emu.readFile(LSDJ);
  emu.writeFile(ROM_A, rom.buffer);
  emu.writeFile(ROM_B, rom.buffer);
  const savA = authoredSav(1);
  const savB = authoredSav(5); // distinct songs -> distinct battery RAM
  emu.writeFile(SAV_A, savA);
  emu.writeFile(SAV_B, savB);

  emu.loadRom(ROM_A);
  emu.loadRom(ROM_B);
  emu.runMs(500);
  emu.saveProjectFile(PROJ);

  // Thin: both ROM paths present, no embedded ROM bytes.
  let text = "";
  const bytes = emu.readFile(PROJ);
  for (let i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
  expect(text.indexOf(ROM_A) >= 0).toBeTruthy();
  expect(text.indexOf(ROM_B) >= 0).toBeTruthy();
  expect(text.indexOf('"romBytes":[]') >= 0).toBeTruthy();

  // Reload from scratch: both systems come back, each with its own sibling SRAM.
  emu.loadRplg(PROJ);
  const audio = emu.runMsPerSystem(200);
  expect(audio.length).toBe(2);

  // Per-system battery RAM matches the sibling .sav it was saved next to.
  const sramA = emu.saveSram(1);
  const sramB = emu.saveSram(2);
  const expA = new Uint8Array(savA);
  const expB = new Uint8Array(savB);
  expect(sramA.length).toBe(expA.length);
  expect(sramB.length).toBe(expB.length);
  let diffA = 0, diffB = 0;
  for (let i = 0; i < expA.length; i++) {
    if (sramA[i] !== expA[i]) diffA++;
    if (sramB[i] !== expB[i]) diffB++;
  }
  expect(diffA).toBe(0);
  expect(diffB).toBe(0);
  // Sanity: the two savs really were different, so this proved per-system routing.
  let abDiff = 0;
  for (let i = 0; i < expA.length; i++) if (expA[i] !== expB[i]) abDiff++;
  expect(abDiff).toBeGreaterThan(0);
});

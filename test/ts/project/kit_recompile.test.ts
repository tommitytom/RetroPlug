// Path-only JSON save drops compiled LSDj kit bytes and recompiles them from the
// linked source WAVs on load — kits are derived artifacts, so the project only
// stores the sample links. Verified by comparing the kit's ROM bank before save
// and after a JSON round-trip: byte-identical means recompile reproduced it.
// Run via: pnpm test:cli project/kit_recompile

import { test, expect, emu, Mem } from "harness";

const LSDJ  = "../resources/roms/lsdj/lsdj9_4_2.gb";
const KICK  = "../resources/samples/mule/kick.wav";
const SNARE = "../resources/samples/mule/snare.wav";
const PROJ  = "/tmp/rp_kit_recompile.rplg";

const SLOT      = 0;
const KIT_SIZE  = 0x4000;
const BANK_OFF  = 8 * KIT_SIZE; // slot 0 -> kit bank 8 (OffsetLookup::kSlotBank)

function decodeAscii(bytes: Uint8Array): string {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return s;
}

test("path-only JSON recompiles kits from source WAVs on load", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(3000); // boot so the cartridge ROM is mapped

  emu.patchKit(sys, SLOT, "TEST", [
    { path: KICK,  name: "KCK" },
    { path: SNARE, name: "SNR" },
  ]);
  emu.runMs(200); // role writes the bank at the next process block

  const before = emu.readMemory(sys, Mem.Rom).slice(BANK_OFF, BANK_OFF + KIT_SIZE);
  // Sanity: the bank isn't empty (the kit was actually applied).
  let nonzero = 0;
  for (const b of before) if (b !== 0) nonzero++;
  expect(nonzero).toBeGreaterThan(0);

  emu.saveProjectFile(PROJ);

  // JSON drops the compiled bytes but keeps the sample links.
  const text = decodeAscii(emu.readFile(PROJ));
  expect(text.indexOf('"compiledBytes":[]') >= 0).toBeTruthy();
  expect(text.indexOf("kick.wav") >= 0).toBeTruthy();
  expect(text.indexOf("snare.wav") >= 0).toBeTruthy();

  // Reload: the kit is recompiled from the linked WAVs.
  const reSys = emu.loadRplg(PROJ);
  emu.runMs(200);
  const after = emu.readMemory(reSys, Mem.Rom).slice(BANK_OFF, BANK_OFF + KIT_SIZE);

  expect(after.length).toBe(before.length);
  let diffs = 0;
  for (let i = 0; i < before.length; i++) if (before[i] !== after[i]) diffs++;
  expect(diffs).toBe(0);
});

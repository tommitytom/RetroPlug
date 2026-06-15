// The Export Zip bundle must be fully self-contained: it embeds the ROM bytes
// and the compiled kit, so it loads even after the source ROM and sample WAVs
// are gone. This is the contrast to the path-only JSON save (which needs them).
// Run via: pnpm test:cli project/zip_self_contained

import { test, expect, emu, Mem } from "harness";

const LSDJ  = "../resources/roms/lsdj/lsdj9_4_2.gb";
const KICK  = "/tmp/rp_zip_kick.wav";
const SNARE = "/tmp/rp_zip_snare.wav";
const ROM   = "/tmp/rp_zip_rom.gb";
const ZIP   = "/tmp/rp_zip_project.zip";

const SLOT     = 0;
const KIT_SIZE = 0x4000;
const BANK_OFF = 8 * KIT_SIZE; // slot 0 -> kit bank 8

test("zip export loads after source ROM + WAVs are deleted", () => {
  // Stage the ROM and sample WAVs on disk.
  emu.writeFile(ROM,   emu.readFile(LSDJ).buffer);
  emu.writeFile(KICK,  emu.readFile("../resources/samples/mule/kick.wav").buffer);
  emu.writeFile(SNARE, emu.readFile("../resources/samples/mule/snare.wav").buffer);

  const sys = emu.loadRom(ROM);
  emu.runMs(3000);
  emu.patchKit(sys, SLOT, "TEST", [
    { path: KICK,  name: "KCK" },
    { path: SNARE, name: "SNR" },
  ]);
  emu.runMs(200);
  const before = emu.readMemory(sys, Mem.Rom).slice(BANK_OFF, BANK_OFF + KIT_SIZE);

  // Export the self-contained zip (embeds ROM + compiled kit bytes).
  emu.saveRplg(ZIP);

  // Now make the sources disappear — the zip must not need them.
  emu.removeFile(ROM);
  emu.removeFile(KICK);
  emu.removeFile(SNARE);

  const reSys = emu.loadRplg(ZIP);
  emu.runMs(200);

  // ROM came from the embedded bytes (the file is gone): the system runs.
  const audio = emu.runMsPerSystem(200);
  expect(audio.length).toBe(1);

  // The kit came from the embedded compiled bytes (no recompile needed): the
  // bank is byte-identical to before the export.
  const after = emu.readMemory(reSys, Mem.Rom).slice(BANK_OFF, BANK_OFF + KIT_SIZE);
  expect(after.length).toBe(before.length);
  let diffs = 0;
  for (let i = 0; i < before.length; i++) if (before[i] !== after[i]) diffs++;
  expect(diffs).toBe(0);
});

// Graceful degradation: a path-only JSON project whose kit source WAV has gone
// missing must still load — the kit just can't be rebuilt. The project, its
// system, and any other kit must come back intact (no crash, no aborted load).
// Run via: pnpm test:cli project/kit_missing_wav

import { test, expect, emu, Mem } from "harness";

const LSDJ  = "../resources/roms/lsdj/lsdj9_4_2.gb";
const ROM   = "/tmp/rp_miss_rom.gb";
const GONE  = "/tmp/rp_miss_gone.wav";   // deleted before reload
const KEEP  = "/tmp/rp_miss_keep.wav";   // survives
const PROJ  = "/tmp/rp_miss.rplg";

const KIT_SIZE = 0x4000;
const bankOff  = (slot: number) => (8 + slot) * KIT_SIZE; // kSlotBank[slot] = 8+slot

test("missing kit WAV degrades gracefully; other kit still recompiles", () => {
  emu.writeFile(ROM,  emu.readFile(LSDJ).buffer);
  emu.writeFile(GONE, emu.readFile("../resources/samples/mule/kick.wav").buffer);
  emu.writeFile(KEEP, emu.readFile("../resources/samples/mule/snare.wav").buffer);

  const sys = emu.loadRom(ROM);
  emu.runMs(3000);
  emu.patchKit(sys, 0, "GONE", [{ path: GONE, name: "KCK" }]);
  emu.patchKit(sys, 1, "KEEP", [{ path: KEEP, name: "SNR" }]);
  emu.runMs(200);
  const keepBefore = emu.readMemory(sys, Mem.Rom).slice(bankOff(1), bankOff(1) + KIT_SIZE);

  emu.saveProjectFile(PROJ);

  // The source for slot 0 disappears before the reload.
  emu.removeFile(GONE);

  // Reload must succeed despite the missing WAV.
  const reSys = emu.loadRplg(PROJ);
  expect(reSys).toBeGreaterThan(0);
  emu.runMs(200);

  // The system is alive and runs.
  const audio = emu.runMsPerSystem(200);
  expect(audio.length).toBe(1);

  // Slot 1's WAV survived, so that kit recompiled byte-identically.
  const keepAfter = emu.readMemory(reSys, Mem.Rom).slice(bankOff(1), bankOff(1) + KIT_SIZE);
  let diffs = 0;
  for (let i = 0; i < keepBefore.length; i++) if (keepBefore[i] !== keepAfter[i]) diffs++;
  expect(diffs).toBe(0);
});

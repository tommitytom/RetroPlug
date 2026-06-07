// Replacement for examples/scripts/lsdj_kit_patch_smoke.json.
//
// The JSON booted LSDj 16s (waiting out the fresh-ROM self-test, because
// patching mid-self-test corrupts it), patched a custom drum kit (mule
// kick/snare/hat) into kit slot 0, and screenshotted to eyeball the PROJECT
// screen's KIT row showing the new name. We boot from a valid sav (no self-test,
// so we can patch immediately) and assert the patch landed in the cartridge ROM:
// slot 0's kit bank changes and the 3-char sample names appear in it.
import { test, expect, emu, Mem } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
const emptySav = () => emu.savFromJson(JSON.stringify({ workingSong: { formatVersion: 22 } }));

// LSDj kit slot N lives in cartridge ROM bank 8+N (0x4000 each); slot 0 = 0x20000.
const KIT0 = 0x20000, KIT_SIZE = 0x4000;

const findIn = (rom: Uint8Array, ascii: string, start: number, end: number): number => {
  const t = Array.from(ascii, (c) => c.charCodeAt(0));
  for (let i = start; i + t.length <= end; i++) {
    let ok = true;
    for (let j = 0; j < t.length; j++) if (rom[i + j] !== t[j]) { ok = false; break; }
    if (ok) return i;
  }
  return -1;
};

test("patch a custom kit into slot 0 and verify it lands in cartridge ROM", () => {
  const sys = emu.loadRom(LSDJ, emptySav());
  emu.runMs(4000); // valid sav skips the self-test, so we can patch right away
  const before = emu.readMemory(sys, Mem.Rom).slice(KIT0, KIT0 + KIT_SIZE);

  emu.patchKit(sys, 0, "TEST", [
    { path: "../resources/samples/mule/kick.wav", name: "KIK" },
    { path: "../resources/samples/mule/snare.wav", name: "SNR" },
    { path: "../resources/samples/mule/hat.wav", name: "HAT" },
  ]);
  emu.runMs(500); // the role writes the bank at the next process block
  emu.screenshot(sys, "/tmp/lsdj_kit_patch.png");

  const rom = emu.readMemory(sys, Mem.Rom);
  const after = rom.slice(KIT0, KIT0 + KIT_SIZE);

  // The kit bank actually changed.
  let changed = false;
  for (let i = 0; i < after.length; i++) if (after[i] !== before[i]) { changed = true; break; }
  expect(changed).toBeTruthy();

  // The three sample slot names are now present inside slot 0's kit bank.
  expect(findIn(rom, "KIK", KIT0, KIT0 + KIT_SIZE)).toBeGreaterThan(-1);
  expect(findIn(rom, "SNR", KIT0, KIT0 + KIT_SIZE)).toBeGreaterThan(-1);
  expect(findIn(rom, "HAT", KIT0, KIT0 + KIT_SIZE)).toBeGreaterThan(-1);
});

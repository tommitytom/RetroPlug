// Native proofs for the EverMIDI ROM-asset override path (kits + fonts). Mirrors test-native/risa-rom.test.ts.
//
// (1) MesenBackend::build must load TS-supplied effective ROM bytes (spec.romBytes) instead of slurping
//     romPath — proven by constructing with a nonexistent romPath but valid romBytes (only the romBytes path
//     can boot it). This support already shipped for risa; here it's exercised for an EverMIDI ROM.
// (2) End-to-end: patch a kit + a font into the base ROM in memory via EverMidiRom, construct from those
//     bytes, and confirm the patched image boots (and the on-disk .nes is never touched).
// Points at the sibling evermidi build (which carries the EVERMIDI marker + baked kit); SKIPs cleanly if absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { EverMidiRom } from "../src/evermidi/rom";

const EVERMIDI_ROM = "/workspaces/evermidi/rom/n8-midi.nes";

test("constructSystem romBytes boots an EverMIDI (Mesen) system over a nonexistent romPath", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(EVERMIDI_ROM)) { console.log(`# SKIP evermidi-rom romBytes: no ROM at ${EVERMIDI_ROM}`); return; }

  const bytes = be.readFile(EVERMIDI_ROM)!;
  // romPath points at nothing; only romBytes can boot this. (Pre-fix Mesen slurps the path -> nullptr.)
  expect(be.constructSystem({
    romPath: "/definitely/not/here/nope.nes", platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: bytes,
  }, 11)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(11) != null).toBeTruthy(); // booted from romBytes, not romPath
  console.log(`[evermidi-rom] Mesen booted from spec.romBytes (nonexistent romPath)`);
});

test("a kit + font patched into the ROM in memory boots (the on-disk .nes is untouched)", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(EVERMIDI_ROM)) { console.log(`# SKIP evermidi-rom patch-boot: no ROM at ${EVERMIDI_ROM}`); return; }

  const base = be.readFile(EVERMIDI_ROM)!;
  const rom = EverMidiRom.fromBytes(base);
  expect(rom.isEverMidi).toBeTruthy();
  expect(rom.isKitPopulated(0)).toBeTruthy(); // the baked kit

  // Nudge the kit bank + font slot 0 in memory (keeping the kit populated).
  const kit = rom.getKitBank(0)!.slice();
  kit[0] ^= 0xff; // flip a DPCM byte
  rom.setKit(0, kit);
  const font = rom.getChrFontSlot(0)!.slice();
  font[0] ^= 0xff; // flip a tile byte
  rom.setChrFontSlot(0, font);
  const effective = rom.bytes();

  expect(be.constructSystem({
    romPath: EVERMIDI_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: effective,
  }, 12)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(12) != null).toBeTruthy(); // patched image boots

  // The patch is really in the effective ROM, and the stock ROM on disk is byte-identical.
  expect(EverMidiRom.fromBytes(effective).getKitBank(0)![0]).toBe(kit[0]);
  expect([...be.readFile(EVERMIDI_ROM)!]).toEqual([...base]);
  console.log(`[evermidi-rom] patched-in-memory kit+font ROM boots; on-disk .nes unchanged`);
});

test("the baked theme sets the background palette; a theme override changes it", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(EVERMIDI_ROM)) { console.log(`# SKIP evermidi-rom theme: no ROM at ${EVERMIDI_ROM}`); return; }

  const base = be.readFile(EVERMIDI_ROM)!;

  // The ROM applies theme 0 at boot: $3F00 = bg ($0D), $3F01 = text ($30) — the baked DFLT theme.
  expect(be.constructSystem({
    romPath: EVERMIDI_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: base,
  }, 13)).toBeTruthy();
  audio.renderAudio(4000); // let the reset + sysInit palette write run
  const pal0 = be.getPpuState(13).paletteRam;
  expect(pal0[0]).toBe(0x0d); // $3F00 universal background
  expect(pal0[1]).toBe(0x30); // $3F01 text color

  // Override the theme's bg + text (in memory) → the ROM reads the patched table and applies it.
  const rom = EverMidiRom.fromBytes(base);
  const t = rom.getTheme(0)!;
  const rec = t.recordBytes.slice();
  rec[0] = 0x21; // bg
  rec[1] = 0x11; // text
  rom.setTheme(0, rec, t.nameBytes);

  expect(be.constructSystem({
    romPath: EVERMIDI_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: rom.bytes(),
  }, 14)).toBeTruthy();
  audio.renderAudio(4000);
  const pal1 = be.getPpuState(14).paletteRam;
  expect(pal1[0]).toBe(0x21); // the override's bg is what the ROM applied
  expect(pal1[1]).toBe(0x11); // ...and its text color
  expect([...be.readFile(EVERMIDI_ROM)!]).toEqual([...base]); // on-disk .nes untouched
  console.log(`[evermidi-rom] baked theme applied ($3F00/$3F01); override changes the palette`);
});

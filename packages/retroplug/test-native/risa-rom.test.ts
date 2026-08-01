// M3 native proofs for the risa ROM-asset override path.
//
// (1) The load-bearing native change: MesenBackend::build must load TS-supplied effective ROM bytes
//     (spec.romBytes) INSTEAD of slurping romPath. Proven unambiguously by constructing with a nonexistent
//     romPath but valid romBytes — on the old code Mesen slurps the bad path and returns nullptr; only the
//     fix boots it.
// (2) End-to-end: patch a theme + a font into the base ROM in memory via RisaRom, construct from those
//     bytes, and confirm the patched image boots (and the on-disk .nes is never touched).
// SKIPs cleanly when the built risa ROM is absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { rom as risaRom } from "../src/risa";

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";

test("constructSystem romBytes boots a NES (Mesen) system over a nonexistent romPath", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-rom romBytes: no ROM at ${RISA_ROM}`); return; }

  const bytes = be.readFile(RISA_ROM)!;
  // romPath points at nothing; only romBytes can boot this. (Pre-fix Mesen slurps the path -> nullptr.)
  expect(be.constructSystem({
    romPath: "/definitely/not/here/nope.nes", platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: bytes,
  }, 11)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(11) != null).toBeTruthy(); // booted from romBytes, not romPath
  console.log(`[risa-rom] Mesen booted from spec.romBytes (nonexistent romPath)`);
});

test("a theme + font patched into the ROM in memory boots (the on-disk .nes is untouched)", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-rom patch-boot: no ROM at ${RISA_ROM}`); return; }

  const base = be.readFile(RISA_ROM)!;
  const rom = risaRom.RisaRom.fromBytes(base);
  expect(rom.isRisa).toBeTruthy();
  expect(rom.hasThemes).toBeTruthy();

  // Override theme 0's palette + replace font slot 0 with a distinctive CHR bank, in memory.
  const theme = rom.getTheme(0)!;
  const rec = theme.recordBytes.slice();
  rec[0] = (rec[0] + 1) & 0x3f; // nudge the bg role
  rom.setTheme(0, rec, theme.nameBytes);
  const font = rom.getChrFontSlot(0)!.slice();
  font[0] ^= 0xff; // flip a tile byte
  rom.setChrFontSlot(0, font);
  const effective = rom.bytes();

  expect(be.constructSystem({
    romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: effective,
  }, 12)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(12) != null).toBeTruthy(); // patched image boots

  // The patch is really in the effective ROM, and the stock ROM on disk is byte-identical.
  expect(risaRom.RisaRom.fromBytes(effective).getTheme(0)!.recordBytes[0]).toBe(rec[0]);
  expect([...be.readFile(RISA_ROM)!]).toEqual([...base]);
  console.log(`[risa-rom] patched-in-memory theme+font ROM boots; on-disk .nes unchanged`);
});

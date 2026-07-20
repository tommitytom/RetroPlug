// The LSDj ROM asset module (src/lsdj/rom) against a REAL ROM: read kits/palettes/fonts out of
// lsdj9_4_2.gb, patch a kit name, write the .gb back, re-open and confirm — and assert the patch is
// surgical (only the intended bytes changed) and the patched ROM still boots on a real core.
import { test, expect } from "../testing/harness";
import { lsdjRomTool } from "../cli/sessions/lsdj-rom";
import type { Session } from "../cli/session";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { LsdjRom } from "../src/lsdj/rom";
import { KitView, decodeNibbles } from "../src/lsdj/rom/kit";
import { KIT_LOOKUP, BANK_SIZE, ROM_SIZE } from "../src/lsdj/rom/constants";
import { buildKitBank, sampleBytesFromBank, kitSampleSpace } from "../src/lsdj/rom";
import { encodeWav } from "../cli/wav";

declare const __RESOURCES_DIR__: string;
const LSDJ = __RESOURCES_DIR__ + "/roms/lsdj/lsdj9_4_2.gb";
const OUT = "/tmp/rp-lsdj-rom-patched.gb";
// Community asset files (sibling ../resources), for the file import/export round-trips.
const FONT_MAIN_PNG = __RESOURCES_DIR__ + "/lsdfonts/png/7.png"; // 64×72, main tiles only
const FONT_EXT_PNG = __RESOURCES_DIR__ + "/lsdfonts/png/BLSD.png"; // 64×120, extended (+46 gfx)
const LSDPAL = __RESOURCES_DIR__ + "/lsdpals/lsdpal/0D-BLOO.lsdpal";
const KIT_FILE = __RESOURCES_DIR__ + "/kits/DONK.kit";

// Author a short mono WAV (a decaying sine) at 44.1k, written to `path` for the native decoder to read.
function writeTestWav(be: ReturnType<typeof createRealBackend>, path: string, frames = 8000): void {
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 220) * Math.exp(-i / 3000);
  if (!be.writeFile(path, encodeWav(pcm, 44100, 1))) throw new Error(`write failed: ${path}`);
}

test("LsdjRom reads real kit/palette/font assets and round-trips a surgical kit-name patch", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-rom: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const bytes = be.readFile(LSDJ)!;
  expect(bytes.length).toBe(0x100000);

  const lr = LsdjRom.fromBytes(bytes);
  expect(lr.isLsdj).toBeTruthy();
  expect(lr.version?.raw).toBe("LSDJ-V9.4.2");

  // Kits: slot 0 (bank 8) is the TR-606 with 7 named drum samples.
  const kit0 = lr.kit(0).toObject();
  expect(kit0.valid).toBeTruthy();
  expect(kit0.name).toBe("TR-606");
  expect(kit0.samples.map((s) => s.name)).toEqual(["BD-", "SD-", "CHH", "OHH", "HT-", "LT-", "CYM"]);
  expect(kit0.samples[0].pcm.length > 0).toBeTruthy();
  // slot 1 is the TR-707.
  expect(lr.kit(1).name()).toBe("TR-707");

  // Palettes: 7 (version-derived count), marker-located; structural check on set/colour counts.
  const palettes = lr.palettes();
  expect(palettes.length).toBe(7); // 9.4.2 has 7 palettes (version-derived count)
  const p0 = palettes[0].toObject();
  expect(p0.colorSets.length).toBe(5);
  expect(p0.colorSets[0].colors.length).toBe(4);

  // Fonts: 3, marker-located; font 0 has 71 tiles and tile 0 isn't blank (a real glyph).
  const fonts = lr.fonts();
  expect(fonts.length).toBe(3);
  const f0 = fonts[0].toObject();
  expect(f0.tiles.length).toBe(71);
  expect(f0.tiles.some((t) => t.some((px) => px !== 0))).toBeTruthy();

  // Patch kit 0's name and confirm the diff vs the original is confined to the 6 name bytes.
  lr.kit(0).setName("MYKIT");
  const patched = lr.bytes();
  const kitNameBase = 8 * 0x4000 + 0x52;
  let diffs = 0;
  let firstDiff = -1;
  let lastDiff = -1;
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] !== patched[i]) {
      diffs++;
      if (firstDiff < 0) firstDiff = i;
      lastDiff = i;
    }
  }
  expect(diffs > 0 && diffs <= 6).toBeTruthy();
  expect(firstDiff).toBe(kitNameBase);
  expect(lastDiff < kitNameBase + 6).toBeTruthy();

  // Write it back, re-open, and confirm the new name (and that everything else still reads).
  expect(be.writeFileAtomic(OUT, patched)).toBeTruthy();
  const reopened = LsdjRom.fromBytes(be.readFile(OUT)!);
  expect(reopened.kit(0).name()).toBe("MYKIT");
  expect(reopened.kit(1).name()).toBe("TR-707"); // untouched
  expect(reopened.palettes().length).toBe(7);

  // The patched ROM still boots on a real SameBoy core.
  const audio = createAudioDriver();
  expect(be.constructSystem({
    romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null,
  }, 1)).toBeTruthy();
  audio.renderAudio(500);
  const frame = be.getFrame(1);
  expect(frame != null).toBeTruthy();
  console.log(`[lsdj-rom] TR-606 kit(7 samples) + 6 palettes + 3 fonts read; kit rename round-trips; patched ROM boots`);
});

test("compileKit renders an authored WAV into a 16 KB kit bank a KitView can read", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const wav = "/tmp/rp-kit-src.wav";
  writeTestWav(be, wav);

  const bank = audio.compileKit({ name: "TEST", samples: [{ path: wav, name: "SIN", effects: [] }] });
  expect(bank.length).toBe(BANK_SIZE); // a whole 16 KB kit bank

  // Read it back by dropping the bank into a fresh ROM's slot 0 (bank 8) and using KitView.
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bank, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.valid).toBeTruthy();
  expect(kit.name()).toBe("TEST");
  expect(kit.sampleCount()).toBe(1);
  expect(kit.sampleName(0)).toBe("SIN");
  const pcm = kit.sampleData(0);
  expect(pcm.length > 0).toBeTruthy(); // resampled 44100→11468, 4-bit packed, decoded back
  // Not silent: the decaying sine retains energy.
  let peak = 0;
  for (const v of pcm) peak = Math.max(peak, Math.abs(v));
  expect(peak > 0.3).toBeTruthy();
  console.log(`[lsdj-rom] compileKit: TEST/SIN → ${bank.length}B bank, sample ${pcm.length} frames, peak ${peak.toFixed(2)}`);
});

test("import-sample splice: compile one sample + rebuild a kit around the existing raw samples", () => {
  const be = createRealBackend();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-rom import: LSDj ROM not found at ${LSDJ}`);
    return;
  }
  const audio = createAudioDriver();
  const wav = "/tmp/rp-kit-short.wav";
  writeTestWav(be, wav, 600); // < 1024 source frames → exercises the convertSamplerate overflow fix

  // Compile the one sample (a throwaway 1-sample bank) and pull its nibble bytes.
  const oneBank = audio.compileKit({ name: "", samples: [{ path: wav, name: "NEW", effects: [] }] });
  const bytes = sampleBytesFromBank(oneBank, 0);
  expect(bytes.length > 0).toBeTruthy(); // short-sample compile no longer crashes / drops

  // Splice into the stock TR-606, replacing slot 0 (keeps us within the 0x3fa0 budget).
  const rom = LsdjRom.fromBytes(be.readFile(LSDJ)!);
  const kit = rom.kit(0);
  expect(kit.name()).toBe("TR-606");
  const samples = kit.samplesRaw();
  const originalSlot1 = [...samples[1].bytes]; // SD-, must survive untouched
  samples[0] = { name: "NEW", bytes };
  expect(kitSampleSpace(samples) <= 0x3fa0).toBeTruthy();

  rom.setKitBank(0, buildKitBank(kit.name(), samples));
  const out = "/tmp/rp-lsdj-import.gb";
  expect(be.writeFileAtomic(out, rom.bytes())).toBeTruthy();

  // Re-open: slot 0 is the new sample, the rest preserved byte-for-byte.
  const re = LsdjRom.fromBytes(be.readFile(out)!).kit(0);
  expect(re.name()).toBe("TR-606");
  expect(re.sampleCount()).toBe(7);
  expect(re.sampleName(0)).toBe("NEW");
  expect(re.sampleName(1)).toBe("SD-");
  expect([...re.rawSampleBytes(0)]).toEqual([...bytes]);
  expect([...re.rawSampleBytes(1)]).toEqual(originalSlot1);

  // The patched ROM still boots on a real core.
  expect(be.constructSystem({
    romPath: out, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null,
  }, 2)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(2) != null).toBeTruthy();
  console.log(`[lsdj-rom] import-sample: TR-606 slot0 → NEW (${bytes.length}B), others preserved, ROM boots`);
});

test("compileKit rotate flag: rotated vs un-rotated banks differ but each round-trips per its mode", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const wav = "/tmp/rp-kit-rot.wav";
  writeTestWav(be, wav, 4000);

  const rotBank = audio.compileKit({ name: "R", rotate: true, samples: [{ path: wav, name: "S", effects: [] }] });
  const flatBank = audio.compileKit({ name: "F", rotate: false, samples: [{ path: wav, name: "S", effects: [] }] });

  const rotBytes = sampleBytesFromBank(rotBank, 0);
  const flatBytes = sampleBytesFromBank(flatBank, 0);
  expect(rotBytes.length).toBe(flatBytes.length);
  expect(rotBytes.length > 0).toBeTruthy();
  // The rotation shifts nibbles within each 32-sample frame → the packed bytes differ.
  const sameBytes = rotBytes.length === flatBytes.length && rotBytes.every((b, i) => b === flatBytes[i]);
  expect(sameBytes).toBeFalsy();

  // But decoding each with its MATCHING rotation recovers the identical PCM (same quantised signal).
  const decRot = decodeNibbles(rotBytes, true);
  const decFlat = decodeNibbles(flatBytes, false);
  expect(decRot.length).toBe(decFlat.length);
  let maxDiff = 0;
  for (let i = 0; i < decRot.length; i++) maxDiff = Math.max(maxDiff, Math.abs(decRot[i] - decFlat[i]));
  expect(maxDiff).toBe(0); // rotate-encode∘rotate-decode == flat-encode∘flat-decode
  console.log(`[lsdj-rom] rotate flag: rotated≠flat bytes, but matched decode identical (${decRot.length} frames)`);
});

const TRIP = __RESOURCES_DIR__ + "/roms/tripledipper942.gbc";

test("custom-font ROM: fonts detect via the header anchor + palette/font names resolve", () => {
  const be = createRealBackend();
  if (!be.fileExists(TRIP)) {
    console.log(`# SKIP lsdj-rom custom: tripledipper ROM not found at ${TRIP}`);
    return;
  }
  const rom = LsdjRom.fromBytes(be.readFile(TRIP)!);
  expect(rom.version?.raw).toBe("LSDJ-V9.4.2");

  // Fonts must DETECT even though custom glyphs overwrote the old glyph-tile marker (the reported bug).
  const fonts = rom.fonts();
  expect(fonts.length).toBe(3);
  expect(fonts.map((f) => f.name)).toEqual(["ZERO", "ZER1", "KIKO"]); // custom font names (bank-27 table)
  // Font 1 is the user's custom font — its tiles differ from a fixed graphics font (font 0).
  const f0 = fonts[0].toObject().tiles;
  const f1 = fonts[1].toObject().tiles;
  expect(f1.some((t, i) => t.some((px, k) => px !== f0[i][k]))).toBeTruthy();

  // Palettes: version-derived count (7, not the old hard-coded 6) + custom names.
  const palettes = rom.palettes();
  expect(palettes.length).toBe(7);
  expect(palettes.map((p) => p.name)).toEqual(["BRIL", "CRES", "BALL", "FORK", "COLR", "DINK", "SYNT"]);
  console.log(`[lsdj-rom] tripledipper: 3 fonts detected (custom ZER1) + 7 named palettes`);
});

// --- asset FILE import/export against real community files + the real lodepng codec ---

test("import a real .png font (main + extended) via the native codec; ROM boots; export round-trips", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(LSDJ) || !be.fileExists(FONT_MAIN_PNG) || !be.fileExists(FONT_EXT_PNG)) {
    console.log(`# SKIP lsdj-rom font-file: ROM or font PNGs not found`);
    return;
  }

  // Decode a real 64×72 (main-only) PNG through the host codec (lodepng).
  const mainImg = be.pngDecode(be.readFile(FONT_MAIN_PNG)!);
  expect(mainImg != null).toBeTruthy();
  expect(mainImg!.width).toBe(64);
  expect(mainImg!.height).toBe(72);

  const rom = LsdjRom.fromBytes(be.readFile(LSDJ)!);
  const before = rom.fonts()[0].toObject().tiles;
  rom.importFontImage(0, mainImg!); // main-only → 71 tiles written, gfx untouched
  const after = rom.fonts()[0].toObject().tiles;
  expect(after.some((t, i) => t.some((px, k) => px !== before[i][k]))).toBeTruthy(); // font actually changed

  // Export back through the codec and re-import: the tiles are stable (idempotent round-trip).
  const outImg = rom.exportFontImage(0, false);
  const png = be.pngEncode(outImg.width, outImg.height, outImg.rgba);
  expect(png != null && png![0] === 0x89 && png![1] === 0x50).toBeTruthy(); // real PNG signature
  const rom2 = LsdjRom.fromBytes(be.readFile(LSDJ)!);
  rom2.importFontImage(0, be.pngDecode(png!)!);
  expect(rom2.fonts()[0].toObject().tiles).toEqual(after);

  // Extended 64×120 PNG writes the 46 shared gfx tiles too.
  const extImg = be.pngDecode(be.readFile(FONT_EXT_PNG)!);
  expect(extImg!.height).toBe(120);
  const gfxBefore = rom.fonts()[0].gfxTile(5);
  rom.importFontImage(1, extImg!);
  const gfxAfter = rom.fonts()[0].gfxTile(5); // gfx is shared — visible from any font view
  expect(gfxAfter.some((px, k) => px !== gfxBefore[k])).toBeTruthy();

  // The patched ROM boots on a real core.
  expect(be.writeFileAtomic(OUT, rom.bytes())).toBeTruthy();
  expect(be.constructSystem({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 3)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(3) != null).toBeTruthy();
  console.log(`[lsdj-rom] font-file: 7.png (main) + BLSD.png (extended gfx) import, round-trip stable, ROM boots`);
});

test("import a real .lsdpal palette (colours + name) and a real .kit bank; both boot", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(LSDJ) || !be.fileExists(LSDPAL) || !be.fileExists(KIT_FILE)) {
    console.log(`# SKIP lsdj-rom pal/kit-file: ROM or asset files not found`);
    return;
  }
  const rom = LsdjRom.fromBytes(be.readFile(LSDJ)!);

  // .lsdpal → palette 1: name becomes BLOO and the exported file is byte-identical to the source.
  const palFile = be.readFile(LSDPAL)!;
  rom.importPaletteFile(1, palFile);
  expect(rom.palettes()[1].name).toBe("BLOO");
  expect([...rom.exportPaletteFile(1)]).toEqual([...palFile]);

  // .kit → slot 20: the bank equals the file, and the kit's name/samples read back.
  const kitFile = be.readFile(KIT_FILE)!;
  rom.importKitFile(20, kitFile);
  expect([...rom.exportKitFile(20)]).toEqual([...kitFile]);
  expect(rom.kit(20).name()).toBe("DONK");
  expect(rom.kit(20).sampleCount() > 0).toBeTruthy();

  expect(be.writeFileAtomic(OUT, rom.bytes())).toBeTruthy();
  expect(be.constructSystem({ romPath: OUT, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 4)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(4) != null).toBeTruthy();
  console.log(`[lsdj-rom] pal/kit-file: 0D-BLOO.lsdpal + DONK.kit import byte-identical, ROM boots`);
});

// --- the CLI verbs themselves (build-kit → .kit, apply manifest) driven through the real tool ---

// The lsdj-rom tool only touches s.backend + s.audio, so a minimal session over the real backend/driver is
// enough (mirrors the other tests' createRealBackend/createAudioDriver usage).
function toolSession(): { be: ReturnType<typeof createRealBackend>; audio: ReturnType<typeof createAudioDriver>; s: Session } {
  const be = createRealBackend();
  const audio = createAudioDriver();
  return { be, audio, s: { backend: be, audio } as unknown as Session };
}
const jenc = (o: unknown): Uint8Array => new TextEncoder().encode(JSON.stringify(o));

test("CLI build-kit writes a bootable .kit that import-kit places into a ROM", () => {
  const { be, audio, s } = toolSession();
  if (!be.fileExists(LSDJ)) {
    console.log(`# SKIP lsdj-rom build-kit: ROM not found`);
    return;
  }
  const wav = "/tmp/rp-bk-src.wav";
  writeTestWav(be, wav);
  expect(be.writeFile("/tmp/rp-bk-spec.json", jenc({ name: "MYDR", build: [{ file: wav, name: "BD" }] }))).toBeTruthy();

  lsdjRomTool.run(s, ["build-kit", "/tmp/rp-bk-spec.json", "/tmp/rp-bk.kit"]);
  const kitBytes = be.readFile("/tmp/rp-bk.kit")!;
  expect(kitBytes.length).toBe(BANK_SIZE); // a raw 16 KB bank, not a ROM

  // import-kit it into slot 20 of a copy of the ROM.
  expect(be.writeFile("/tmp/rp-bk.gb", be.readFile(LSDJ)!)).toBeTruthy();
  lsdjRomTool.run(s, ["import-kit", "/tmp/rp-bk.gb", "/tmp/rp-bk.kit", "20", "--out", "/tmp/rp-bk.gb"]);
  const rom = LsdjRom.fromBytes(be.readFile("/tmp/rp-bk.gb")!);
  expect(rom.kit(20).name()).toBe("MYDR");
  expect(rom.kit(20).sampleName(0)).toBe("BD-"); // the native compiler pads a short name to 3 chars with '-'

  expect(be.constructSystem({ romPath: "/tmp/rp-bk.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 5)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(5) != null).toBeTruthy();
  console.log(`[lsdj-rom] build-kit → .kit (16KB) → import-kit slot 20 (MYDR/BD), ROM boots`);
});

test("CLI patch realizes a mixed manifest (build + .kit + rename + palette + font) and boots", () => {
  const { be, audio, s } = toolSession();
  if (!be.fileExists(LSDJ) || !be.fileExists(KIT_FILE) || !be.fileExists(LSDPAL) || !be.fileExists(FONT_EXT_PNG)) {
    console.log(`# SKIP lsdj-rom patch: ROM or asset files not found`);
    return;
  }
  const wav = "/tmp/rp-ap-src.wav";
  writeTestWav(be, wav);
  const manifest = {
    kits: [
      { slot: 21, name: "BILT", build: [{ file: wav, name: "BD" }] }, // compile from audio
      { slot: 22, file: KIT_FILE }, // import a .kit
      { slot: 0, name: "RENAMD", samples: [{ index: 0, name: "KIK" }] }, // rename
    ],
    palettes: [
      { slot: 1, file: LSDPAL }, // import .lsdpal
      { slot: 0, set: 0, color: 0, rgb: [255, 0, 0] }, // set a colour
    ],
    fonts: [{ slot: 0, file: FONT_EXT_PNG }], // import an extended .png (via the native codec)
  };
  expect(be.writeFile("/tmp/rp-ap-manifest.json", jenc(manifest))).toBeTruthy();
  expect(be.writeFile("/tmp/rp-ap.gb", be.readFile(LSDJ)!)).toBeTruthy();

  lsdjRomTool.run(s, ["patch", "/tmp/rp-ap.gb", "/tmp/rp-ap-manifest.json", "/tmp/rp-ap-out.gb"]);

  const rom = LsdjRom.fromBytes(be.readFile("/tmp/rp-ap-out.gb")!);
  expect(rom.kit(21).name()).toBe("BILT"); // built
  expect(rom.kit(22).name()).toBe("DONK"); // imported .kit
  expect(rom.kit(0).name()).toBe("RENAMD"); // renamed
  expect(rom.kit(0).sampleName(0)).toBe("KIK"); // sample renamed
  expect(rom.palettes()[1].name).toBe("BLOO"); // .lsdpal
  expect(rom.palettes()[0].color(0, 0)).toEqual({ r: 255, g: 0, b: 0 }); // colour set

  expect(be.constructSystem({ romPath: "/tmp/rp-ap-out.gb", platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null }, 6)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(6) != null).toBeTruthy();
  console.log(`[lsdj-rom] patch: build+import+rename+palette+font manifest realized, ROM boots`);
});

// The romBytes construct channel that powers the non-destructive LSDj asset overrides (lsdj-assets role):
// native must load TS-supplied effective ROM bytes INSTEAD of slurping romPath, and boot them.
test("constructSystem romBytes loads a patched effective ROM (over the on-disk romPath) and boots", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(LSDJ) || !be.fileExists(KIT_FILE)) {
    console.log(`# SKIP lsdj-rom romBytes: ROM/kit not found`);
    return;
  }
  // Patch the base ROM IN MEMORY (import DONK.kit into slot 20) — the on-disk .gb is never written.
  const patched = LsdjRom.fromBytes(be.readFile(LSDJ)!);
  patched.importKitFile(20, be.readFile(KIT_FILE)!);
  const effective = patched.bytes();

  // Construct with romPath = the STOCK ROM but romBytes = the patched image; native must honour romBytes.
  expect(be.constructSystem({
    romPath: LSDJ, platform: "gb", core: "sameboy", embeddedRom: "", savPath: null, statePath: null, romBytes: effective,
  }, 7)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(7) != null).toBeTruthy(); // booted the patched effective ROM

  // Sanity: the effective ROM really carries the override, and the on-disk file is unchanged.
  expect(LsdjRom.fromBytes(effective).kit(20).name()).toBe("DONK");
  expect(LsdjRom.fromBytes(be.readFile(LSDJ)!).kit(20).name() !== "DONK").toBeTruthy(); // stock unchanged
  console.log(`[lsdj-rom] romBytes: patched effective ROM (kit20=DONK) boots via the construct channel, on-disk .gb untouched`);
});

// End-to-end for the `retroplug-cli bliptoaster-rom` verbs, driven through the real tool against a real backend +
// audio driver (mirrors test-native/risa-rom-cli.test.ts). The compile path (build-kit) uses the REAL native
// compileDmc RPC and needs no ROM, so it always runs; the ROM-splice verbs are gated on the built FME-7
// banking ROM (16 kit banks) and SKIP when absent. The tool only touches s.backend + s.audio, so a minimal
// Session over the real backend/driver suffices.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { Session } from "../cli/session";
import { blipToasterRomTool } from "../cli/sessions/bliptoaster-rom";
import { BlipToasterRom } from "../src/bliptoaster/rom";
import { bankToModel, isBankPopulated, decodeThemeFromRom } from "../src/risa/rom";
import { encodeWav } from "../cli/wav";

// The base build (FME-7, taken for kit banking alone — no expansion audio): 16 switchable kit banks, 16 baked
// themes, 4 CHR fonts. Built by a bare `make` in the bliptoaster checkout; these legs SKIP cleanly if absent.
// Keep this path current with that repo: it pointed at a pre-rename layout for a while, and since a missing ROM
// only SKIPS, every ROM-splice leg here silently passed without running — which is how the theme-table bug
// (§9 of its HARNESS-NOTES) reached a release with a green suite behind it.
const BLIPTOASTER_ROM = "/workspaces/bliptoaster/build/bliptoaster.nes";

function toolSession(): { be: ReturnType<typeof createRealBackend>; audio: ReturnType<typeof createAudioDriver>; s: Session } {
  const be = createRealBackend();
  const audio = createAudioDriver();
  return { be, audio, s: { backend: be, audio } as unknown as Session };
}
const jenc = (o: unknown): Uint8Array => new TextEncoder().encode(JSON.stringify(o));
function writeSine(be: ReturnType<typeof createRealBackend>, path: string, freq: number, frames = 6000): void {
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * freq) * Math.exp(-i / 3000);
  if (!be.writeFile(path, encodeWav(pcm, 44100, 1))) throw new Error(`write failed: ${path}`);
}
const copyRom = (be: ReturnType<typeof createRealBackend>, to: string): string => {
  if (!be.writeFile(to, be.readFile(BLIPTOASTER_ROM)!)) throw new Error(`copy failed: ${to}`);
  return to;
};
const romSkip = (be: ReturnType<typeof createRealBackend>, what: string): boolean => {
  if (!be.fileExists(BLIPTOASTER_ROM)) { console.log(`# SKIP bliptoaster-rom ${what}: no ROM at ${BLIPTOASTER_ROM}`); return true; }
  return false;
};

test("bliptoaster-rom build-kit compiles a WAV into a populated 8 KB .rkit (no ROM needed)", () => {
  const { be, s } = toolSession();
  writeSine(be, "/tmp/rp-em-src.wav", 220);
  expect(be.writeFile("/tmp/rp-em-spec.json", jenc({ name: "MYDR", build: [{ file: "/tmp/rp-em-src.wav", name: "BD" }] }))).toBeTruthy();

  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-spec.json", "/tmp/rp-em.rkit"]);
  const kit = be.readFile("/tmp/rp-em.rkit")!;
  expect(kit.length).toBe(0x2000); // a raw 8 KB bank, not a ROM
  expect(isBankPopulated(kit)).toBe(true);
  const model = bankToModel(kit);
  expect(model.name).toBe("MYDR");
  expect(model.slots[0]?.name).toBe("BD");
  console.log(`[bliptoaster-rom] build-kit compiled WAV → 8 KB .rkit (MYDR/BD)`);
});

test("bliptoaster-rom import-kit replaces one bank and leaves the other 15 byte-identical; the ROM boots", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "import-kit")) return;
  writeSine(be, "/tmp/rp-em-ik.wav", 300);
  expect(be.writeFile("/tmp/rp-em-ik-spec.json", jenc({ name: "HATS", build: [{ file: "/tmp/rp-em-ik.wav", name: "HH" }] }))).toBeTruthy();
  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-ik-spec.json", "/tmp/rp-em-ik.rkit"]);

  // Every one of the 16 banks ships POPULATED (the cart bakes all 16 of its .rkit assets), so an import is a
  // replace, not a fill. It used to assume slot 3 was reserved and empty - true of an earlier build.
  const SLOT = 3;
  const base = BlipToasterRom.fromBytes(be.readFile(BLIPTOASTER_ROM)!);
  expect(base.kitBankCapacity()).toBe(16);
  expect(base.kitCount()).toBe(16);
  const before = base.kits().find((k) => k.slot === SLOT)!.name;
  expect(before === "HATS").toBe(false); // the baked kit must not already be the one we import

  const nes = copyRom(be, "/tmp/rp-em-ik.nes");
  blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-ik.rkit", String(SLOT), "--out", nes]);

  const rom = BlipToasterRom.fromBytes(be.readFile(nes)!);
  expect(rom.isKitPopulated(SLOT)).toBe(true);
  expect(rom.kits().find((k) => k.slot === SLOT)!.name).toBe("HATS");
  // The splice is bank-local: every other bank still matches the source byte for byte.
  for (let i = 0; i < 16; i++) {
    if (i === SLOT) continue;
    expect([...rom.getKitBank(i)!]).toEqual([...base.getKitBank(i)!]);
  }
  expect([...be.readFile(BLIPTOASTER_ROM)!]).toEqual([...base.bytes()]); // source untouched

  expect(be.constructSystem({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 40)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(40) != null).toBeTruthy();
  console.log(`[bliptoaster-rom] import-kit replaced bank ${SLOT} (${before} -> HATS); patched ROM boots`);
});

test("bliptoaster-rom import-sample splices into a kit, remove-sample empties a slot (index preserved)", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "import-sample")) return;
  writeSine(be, "/tmp/rp-em-a.wav", 200);
  writeSine(be, "/tmp/rp-em-b.wav", 500);
  expect(be.writeFile("/tmp/rp-em-is-spec.json", jenc({ name: "KT", build: [{ file: "/tmp/rp-em-a.wav", name: "AAA" }] }))).toBeTruthy();
  blipToasterRomTool.run(s, ["build-kit", "/tmp/rp-em-is-spec.json", "/tmp/rp-em-is.rkit"]);
  const nes = copyRom(be, "/tmp/rp-em-is.nes");
  blipToasterRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-is.rkit", "2", "--out", nes]);

  // import-sample lands in the first empty slot (1).
  blipToasterRomTool.run(s, ["import-sample", nes, "2", "/tmp/rp-em-b.wav", "--name", "BBB", "--out", nes]);
  let model = bankToModel(BlipToasterRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
  expect(model.slots[0]?.name).toBe("AAA");
  expect(model.slots[1]?.name).toBe("BBB");

  // remove-sample empties slot 0; slot 1 stays put (index preserved).
  blipToasterRomTool.run(s, ["remove-sample", nes, "2", "0", "--out", nes]);
  model = bankToModel(BlipToasterRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
  expect(model.slots[0]).toBe(null);
  expect(model.slots[1]?.name).toBe("BBB");
  console.log(`[bliptoaster-rom] import-sample (slot 1) + remove-sample (slot 0) — index-addressed splice`);
});

// The real-artifact guard for the asset TABLES. The synthetic fixtures in test/systems/fixtures.ts can only
// prove the reader matches what they themselves bake, and for a while both baked the wrong theme layout, so the
// count read as 1 with the whole suite green. Asserting the shipped ROM's own names is what catches a drift
// between the two repos - the table's stride, its length, or a build that reshuffles it.
test("the real ROM's baked tables read in full: 16 named themes, 4 fonts, 16 kit banks", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "tables")) return;
  const rom = BlipToasterRom.fromBytes(be.readFile(BLIPTOASTER_ROM)!);

  expect(rom.themeCount).toBe(16);
  expect(rom.themes().map((t) => t.theme.name.trim())).toEqual([
    "DFLT", "DARK", "NEON", "LITE", "CRT", "ICE", "FIRE", "GB",
    "AQUA", "MONO", "PLSM", "MTRX", "FOG", "SUN", "MOON", "AMBR",
  ]);
  expect(rom.chrFontSlotCount).toBe(4);
  expect(rom.kitBankCapacity()).toBe(16);
  // Every entry decodes to in-range palette roles - i.e. the stride lands on records, not on adjacent RODATA.
  for (const { theme } of rom.themes()) {
    for (const role of ["bg", "normal", "shaded", "alternate", "status", "cursor", "selection"] as const) {
      expect(parseInt(theme[role].slice(2), 16) <= 0x3f).toBe(true);
    }
  }
  // And `info` reports the same thing, since that is the surface a user reads.
  blipToasterRomTool.run(s, ["info", BLIPTOASTER_ROM]);
  console.log(`[bliptoaster-rom] real ROM: 16 themes / 4 fonts / 16 kit banks`);
});

test("bliptoaster-rom export-theme → import-theme round-trips a .rit; export-font → import-font a .chr", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "theme/font")) return;
  const rom0 = BlipToasterRom.fromBytes(be.readFile(BLIPTOASTER_ROM)!);
  // A HIGH theme index on purpose: slot 0 is the one entry risa's split layout happened to read correctly, so
  // a slot-0-only round-trip passes against the broken reader too.
  const SLOT = 11;
  const wantTheme = decodeThemeFromRom(rom0.getTheme(SLOT)!.recordBytes, rom0.getTheme(SLOT)!.nameBytes);
  expect(wantTheme.name.trim()).toBe("MTRX");
  const wantFont = rom0.getChrFontSlot(3)!; // likewise the LAST font bank, not bank 0

  blipToasterRomTool.run(s, ["export-theme", BLIPTOASTER_ROM, String(SLOT), "/tmp/rp-em.rit"]);
  blipToasterRomTool.run(s, ["export-font", BLIPTOASTER_ROM, "3", "/tmp/rp-em.chr"]);
  expect(be.readFile("/tmp/rp-em.chr")!.length).toBe(0x2000);

  const nes = copyRom(be, "/tmp/rp-em-tf.nes");
  blipToasterRomTool.run(s, ["import-theme", nes, "/tmp/rp-em.rit", String(SLOT), "--out", nes]);
  blipToasterRomTool.run(s, ["import-font", nes, "/tmp/rp-em.chr", "3", "--out", nes]);

  const rom = BlipToasterRom.fromBytes(be.readFile(nes)!);
  expect(decodeThemeFromRom(rom.getTheme(SLOT)!.recordBytes, rom.getTheme(SLOT)!.nameBytes)).toEqual(wantTheme);
  expect([...rom.getChrFontSlot(3)!]).toEqual([...wantFont]);
  // Re-importing the same bytes is a no-op, so the whole file must still match the source byte for byte - which
  // also proves the splice addressed entry 11 and not something adjacent.
  expect([...be.readFile(nes)!]).toEqual([...rom0.bytes()]);
  expect([...be.readFile(BLIPTOASTER_ROM)!]).toEqual([...rom0.bytes()]); // source ROM untouched
});

test("bliptoaster-rom settings prints the real ROM's block, patches named fields, and rejects a bad value", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "settings")) return;
  const base = BlipToasterRom.fromBytes(be.readFile(BLIPTOASTER_ROM)!);
  expect(base.hasSettings).toBe(true);
  // A shipped ROM is at its power-on defaults - that is what "baked settings" means out of the build. `ppu`
  // is the one that is not zero: the screen draws unless someone bakes the dark boot.
  expect(base.settings()).toEqual({ baseChannel: 0, kit: 0, ppu: true, velCurve: false, theme: 0, font: 0 });

  // No flags: read-only. It must not write the ROM it is inspecting.
  const nes = copyRom(be, "/tmp/rp-em-set.nes");
  blipToasterRomTool.run(s, ["settings", nes]);
  expect([...be.readFile(nes)!]).toEqual([...base.bytes()]);

  blipToasterRomTool.run(s, ["settings", nes, "--theme", "11", "--font", "2", "--base-channel", "4", "--kit", "9", "--ppu", "off", "--curve", "log"]);
  const after = BlipToasterRom.fromBytes(be.readFile(nes)!);
  expect(after.settings()).toEqual({ baseChannel: 3, kit: 9, ppu: false, velCurve: true, theme: 11, font: 2 });
  // Only the block moved: the assets are byte-identical, so this cannot have disturbed a kit or the theme table.
  expect(after.themes().map((t) => t.theme.name)).toEqual(base.themes().map((t) => t.theme.name));
  for (let i = 0; i < 16; i++) expect([...after.getKitBank(i)!]).toEqual([...base.getKitBank(i)!]);

  // A value out of range throws rather than clamping - a CLI typo must not quietly bake something else.
  expect(() => blipToasterRomTool.run(s, ["settings", nes, "--theme", "16"])).toThrow();
  expect(() => blipToasterRomTool.run(s, ["settings", nes, "--curve", "sharp"])).toThrow();
  // The two spellings of the screen byte disagree by design, so asking for both is refused rather than resolved.
  expect(() => blipToasterRomTool.run(s, ["settings", nes, "--ppu", "on", "--mode1", "on"])).toThrow();
  expect([...be.readFile(nes)!]).toEqual([...after.bytes()]); // and wrote nothing

  // --mode1 still works, writing the INVERSE - `--mode1 off` is the dark-boot flag's old "off", which is the
  // screen drawing, so it must land the same byte `--ppu on` does.
  blipToasterRomTool.run(s, ["settings", nes, "--mode1", "off"]);
  expect(BlipToasterRom.fromBytes(be.readFile(nes)!).settings()!.ppu).toBe(true);
  console.log(`[bliptoaster-rom] settings: printed, patched 6 fields, rejected 3 bad values, --mode1 inverted`);
});

test("a settings-patched ROM boots, and the core comes up in the theme the block names", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "settings boot")) return;
  const nes = copyRom(be, "/tmp/rp-em-setboot.nes");
  blipToasterRomTool.run(s, ["settings", nes, "--theme", "11"]); // MTRX: bg $0F, text $2A

  expect(be.constructSystem({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 44)).toBeTruthy();
  audio.renderAudio(44100); // ~1 s: past the ROM's boot, so the palette has been written
  // The proof that the byte reached the hardware, not just the file: $3F00/$3F01 hold theme 11's two roles.
  const pal = be.getPpuState(44).paletteRam;
  expect(pal[0]).toBe(0x0f);
  expect(pal[1]).toBe(0x2a);
  console.log(`[bliptoaster-rom] settings --theme 11 -> the core boots with $3F00/$3F01 = 0F/2A`);
});

// The committed resources ROM is a real artifact predating the two screen fields, so it is the honest fixture
// for the capability probe - its bytes are the 0xFF a pre-field build leaves, not something a test contrived.
// __REPO_RESOURCES_DIR__ (an absolute path the runner injects), not a relative one - the suite does not run from
// the repo root, so a relative path here would silently SKIP, which is the failure this file was already bitten by.
declare const __REPO_RESOURCES_DIR__: string;
const OLD_ROM = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("a ROM predating the screen fields is reported as not reading them, and refuses a write", () => {
  const { be, s } = toolSession();
  if (!be.fileExists(OLD_ROM)) { console.log(`# SKIP capability probe: no ROM at ${OLD_ROM}`); return; }
  const rom = BlipToasterRom.fromBytes(be.readFile(OLD_ROM)!);
  expect(rom.hasSettings).toBe(true); // the block is there: its four original fields DO work on this cart
  expect(rom.settingSupported("baseChannel")).toBe(true);
  expect(rom.settingSupported("theme")).toBe(false);
  expect(rom.settingSupported("font")).toBe(false);

  // Writing one of them would "succeed" and change nothing about how the cart boots, which is the failure the
  // bug report described. Refuse instead, and leave the file alone.
  const nes = "/tmp/rp-em-old.nes";
  expect(be.writeFile(nes, rom.bytes())).toBeTruthy();
  const before = be.readFile(nes)!;
  expect(() => blipToasterRomTool.run(s, ["settings", nes, "--font", "2"])).toThrow();
  expect(() => blipToasterRomTool.run(s, ["settings", nes, "--theme", "11"])).toThrow();
  expect([...be.readFile(nes)!]).toEqual([...before]);
  // A field the block shipped with still writes on the same cart - an old ROM is not cut off from the rest.
  blipToasterRomTool.run(s, ["settings", nes, "--base-channel", "4"]);
  expect(BlipToasterRom.fromBytes(be.readFile(nes)!).settings()!.baseChannel).toBe(3);
  console.log(`[bliptoaster-rom] ${OLD_ROM}: theme/font unsupported (reserved 0xFF), base channel still writable`);
});

// The font's other half. The theme leg above reads palette RAM, but nothing exposes which CHR bank is MAPPED -
// no readMemory region follows the window, and FME-7's $8000/$A000 writes are not in Mesen's event log - so the
// ROM's own g_fontIdx was as far as the consumer's suite could go, and that only says the ROM took the setting.
// The rendered FRAME is the missing evidence: two ROMs identical but for the font byte must not draw the same
// pixels. Without this, "the setting is applied" and "the glyphs changed" are two different claims and only the
// first was tested.
function frameOf(be: ReturnType<typeof createRealBackend>, audio: ReturnType<typeof createAudioDriver>, rom: string, id: number): Uint8Array {
  expect(be.constructSystem({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, id)).toBeTruthy();
  audio.renderAudio(44100 * 2); // ~2 s: past boot and the first drawn page
  const frame = be.getFrame(id);
  expect(frame != null && frame.published).toBeTruthy();
  return new Uint8Array(frame!.pixels);
}

test("the baked font byte reaches the screen: two ROMs differing only in it render different pixels", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "font pixels")) return;

  const a = copyRom(be, "/tmp/rp-em-font0.nes");
  const b = copyRom(be, "/tmp/rp-em-font2.nes");
  blipToasterRomTool.run(s, ["settings", a, "--font", "0"]);
  blipToasterRomTool.run(s, ["settings", b, "--font", "2"]);
  // The two images differ in exactly one byte, so any pixel difference below is the font and nothing else.
  const ra = be.readFile(a)!, rb = be.readFile(b)!;
  let romDiff = 0;
  for (let i = 0; i < ra.length; i++) if (ra[i] !== rb[i]) romDiff++;
  expect(romDiff).toBe(1);

  const pa = frameOf(be, audio, a, 60);
  const pb = frameOf(be, audio, b, 61);
  expect(pa.length).toBe(pb.length);
  let pixDiff = 0;
  for (let i = 0; i < pa.length; i++) if (pa[i] !== pb[i]) pixDiff++;
  console.log(`[bliptoaster-rom] font 0 vs font 2: ${pixDiff} of ${pa.length} frame bytes differ`);
  expect(pixDiff).toBeGreaterThan(0);
});

test("bliptoaster-rom patch realizes a mixed manifest (build kit + import theme + font) and boots", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "patch")) return;
  writeSine(be, "/tmp/rp-em-pt.wav", 300);
  blipToasterRomTool.run(s, ["export-theme", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.rit"]);
  blipToasterRomTool.run(s, ["export-font", BLIPTOASTER_ROM, "0", "/tmp/rp-em-pt.chr"]);
  const manifest = {
    kits: [{ slot: 4, name: "PT", build: [{ file: "/tmp/rp-em-pt.wav", name: "KIK" }] }],
    themes: [{ slot: 0, file: "/tmp/rp-em-pt.rit" }],
    fonts: [{ slot: 0, file: "/tmp/rp-em-pt.chr" }],
  };
  expect(be.writeFile("/tmp/rp-em-pt-manifest.json", jenc(manifest))).toBeTruthy();

  const src = copyRom(be, "/tmp/rp-em-pt-src.nes");
  blipToasterRomTool.run(s, ["patch", src, "/tmp/rp-em-pt-manifest.json", "/tmp/rp-em-pt-out.nes"]);
  const rom = BlipToasterRom.fromBytes(be.readFile("/tmp/rp-em-pt-out.nes")!);
  expect(rom.kits().some((k) => k.slot === 4 && k.name === "PT")).toBeTruthy();

  expect(be.constructSystem({ romPath: "/tmp/rp-em-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 41)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(41) != null).toBeTruthy();
  console.log(`[bliptoaster-rom] patch: built kit 4 + theme 0 + font 0; patched ROM boots`);
});

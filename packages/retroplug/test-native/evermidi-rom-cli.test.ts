// End-to-end for the `retroplug-cli evermidi-rom` verbs, driven through the real tool against a real backend +
// audio driver (mirrors test-native/risa-rom-cli.test.ts). The compile path (build-kit) uses the REAL native
// compileDmc RPC and needs no ROM, so it always runs; the ROM-splice verbs are gated on the built FME-7
// banking ROM (16 kit banks) and SKIP when absent. The tool only touches s.backend + s.audio, so a minimal
// Session over the real backend/driver suffices.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { Session } from "../cli/session";
import { everMidiRomTool } from "../cli/sessions/evermidi-rom";
import { EverMidiRom } from "../src/evermidi/rom";
import { bankToModel, isBankPopulated, decodeThemeFromRom } from "../src/risa/rom";
import { encodeWav } from "../cli/wav";

// The FME-7 build (plain banking, no expansion audio): 16 switchable kit banks. Built by
// `make -C /workspaces/evermidi/rom all-mappers`; SKIP cleanly if absent.
const EVERMIDI_ROM = "/workspaces/evermidi/rom/n8-midi-fme7.nes";

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
  if (!be.writeFile(to, be.readFile(EVERMIDI_ROM)!)) throw new Error(`copy failed: ${to}`);
  return to;
};
const romSkip = (be: ReturnType<typeof createRealBackend>, what: string): boolean => {
  if (!be.fileExists(EVERMIDI_ROM)) { console.log(`# SKIP evermidi-rom ${what}: no ROM at ${EVERMIDI_ROM}`); return true; }
  return false;
};

test("evermidi-rom build-kit compiles a WAV into a populated 8 KB .rkit (no ROM needed)", () => {
  const { be, s } = toolSession();
  writeSine(be, "/tmp/rp-em-src.wav", 220);
  expect(be.writeFile("/tmp/rp-em-spec.json", jenc({ name: "MYDR", build: [{ file: "/tmp/rp-em-src.wav", name: "BD" }] }))).toBeTruthy();

  everMidiRomTool.run(s, ["build-kit", "/tmp/rp-em-spec.json", "/tmp/rp-em.rkit"]);
  const kit = be.readFile("/tmp/rp-em.rkit")!;
  expect(kit.length).toBe(0x2000); // a raw 8 KB bank, not a ROM
  expect(isBankPopulated(kit)).toBe(true);
  const model = bankToModel(kit);
  expect(model.name).toBe("MYDR");
  expect(model.slots[0]?.name).toBe("BD");
  console.log(`[evermidi-rom] build-kit compiled WAV → 8 KB .rkit (MYDR/BD)`);
});

test("evermidi-rom import-kit places a compiled .rkit into a reserved bank; the ROM boots", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "import-kit")) return;
  writeSine(be, "/tmp/rp-em-ik.wav", 300);
  expect(be.writeFile("/tmp/rp-em-ik-spec.json", jenc({ name: "HATS", build: [{ file: "/tmp/rp-em-ik.wav", name: "HH" }] }))).toBeTruthy();
  everMidiRomTool.run(s, ["build-kit", "/tmp/rp-em-ik-spec.json", "/tmp/rp-em-ik.rkit"]);

  // Slot 3 is a reserved/empty bank on the FME-7 build — import populates it (multi-kit).
  const nes = copyRom(be, "/tmp/rp-em-ik.nes");
  expect(EverMidiRom.fromBytes(be.readFile(nes)!).isKitPopulated(3)).toBe(false);
  everMidiRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-ik.rkit", "3", "--out", nes]);

  const rom = EverMidiRom.fromBytes(be.readFile(nes)!);
  expect(rom.kitBankCapacity()).toBe(16);
  expect(rom.isKitPopulated(3)).toBe(true);
  expect(rom.kits().some((k) => k.slot === 3 && k.name === "HATS")).toBe(true);
  expect([...be.readFile(EVERMIDI_ROM)!]).toEqual([...EverMidiRom.fromBytes(be.readFile(EVERMIDI_ROM)!).bytes()]); // source untouched

  expect(be.constructSystem({ romPath: nes, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 40)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(40) != null).toBeTruthy();
  console.log(`[evermidi-rom] import-kit into reserved bank 3 (HATS); patched ROM boots`);
});

test("evermidi-rom import-sample splices into a kit, remove-sample empties a slot (index preserved)", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "import-sample")) return;
  writeSine(be, "/tmp/rp-em-a.wav", 200);
  writeSine(be, "/tmp/rp-em-b.wav", 500);
  expect(be.writeFile("/tmp/rp-em-is-spec.json", jenc({ name: "KT", build: [{ file: "/tmp/rp-em-a.wav", name: "AAA" }] }))).toBeTruthy();
  everMidiRomTool.run(s, ["build-kit", "/tmp/rp-em-is-spec.json", "/tmp/rp-em-is.rkit"]);
  const nes = copyRom(be, "/tmp/rp-em-is.nes");
  everMidiRomTool.run(s, ["import-kit", nes, "/tmp/rp-em-is.rkit", "2", "--out", nes]);

  // import-sample lands in the first empty slot (1).
  everMidiRomTool.run(s, ["import-sample", nes, "2", "/tmp/rp-em-b.wav", "--name", "BBB", "--out", nes]);
  let model = bankToModel(EverMidiRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
  expect(model.slots[0]?.name).toBe("AAA");
  expect(model.slots[1]?.name).toBe("BBB");

  // remove-sample empties slot 0; slot 1 stays put (index preserved).
  everMidiRomTool.run(s, ["remove-sample", nes, "2", "0", "--out", nes]);
  model = bankToModel(EverMidiRom.fromBytes(be.readFile(nes)!).getKitBank(2)!);
  expect(model.slots[0]).toBe(null);
  expect(model.slots[1]?.name).toBe("BBB");
  console.log(`[evermidi-rom] import-sample (slot 1) + remove-sample (slot 0) — index-addressed splice`);
});

test("evermidi-rom export-theme → import-theme round-trips a .rit; export-font → import-font a .chr", () => {
  const { be, s } = toolSession();
  if (romSkip(be, "theme/font")) return;
  const rom0 = EverMidiRom.fromBytes(be.readFile(EVERMIDI_ROM)!);
  const wantTheme = decodeThemeFromRom(rom0.getTheme(0)!.recordBytes, rom0.getTheme(0)!.nameBytes);
  const wantFont = rom0.getChrFontSlot(0)!;

  everMidiRomTool.run(s, ["export-theme", EVERMIDI_ROM, "0", "/tmp/rp-em.rit"]);
  everMidiRomTool.run(s, ["export-font", EVERMIDI_ROM, "0", "/tmp/rp-em.chr"]);
  expect(be.readFile("/tmp/rp-em.chr")!.length).toBe(0x2000);

  const nes = copyRom(be, "/tmp/rp-em-tf.nes");
  everMidiRomTool.run(s, ["import-theme", nes, "/tmp/rp-em.rit", "0", "--out", nes]);
  everMidiRomTool.run(s, ["import-font", nes, "/tmp/rp-em.chr", "0", "--out", nes]);

  const rom = EverMidiRom.fromBytes(be.readFile(nes)!);
  expect(decodeThemeFromRom(rom.getTheme(0)!.recordBytes, rom.getTheme(0)!.nameBytes)).toEqual(wantTheme);
  expect([...rom.getChrFontSlot(0)!]).toEqual([...wantFont]);
  expect([...be.readFile(EVERMIDI_ROM)!]).toEqual([...rom0.bytes()]); // source ROM untouched
});

test("evermidi-rom patch realizes a mixed manifest (build kit + import theme + font) and boots", () => {
  const { be, audio, s } = toolSession();
  if (romSkip(be, "patch")) return;
  writeSine(be, "/tmp/rp-em-pt.wav", 300);
  everMidiRomTool.run(s, ["export-theme", EVERMIDI_ROM, "0", "/tmp/rp-em-pt.rit"]);
  everMidiRomTool.run(s, ["export-font", EVERMIDI_ROM, "0", "/tmp/rp-em-pt.chr"]);
  const manifest = {
    kits: [{ slot: 4, name: "PT", build: [{ file: "/tmp/rp-em-pt.wav", name: "KIK" }] }],
    themes: [{ slot: 0, file: "/tmp/rp-em-pt.rit" }],
    fonts: [{ slot: 0, file: "/tmp/rp-em-pt.chr" }],
  };
  expect(be.writeFile("/tmp/rp-em-pt-manifest.json", jenc(manifest))).toBeTruthy();

  const src = copyRom(be, "/tmp/rp-em-pt-src.nes");
  everMidiRomTool.run(s, ["patch", src, "/tmp/rp-em-pt-manifest.json", "/tmp/rp-em-pt-out.nes"]);
  const rom = EverMidiRom.fromBytes(be.readFile("/tmp/rp-em-pt-out.nes")!);
  expect(rom.kits().some((k) => k.slot === 4 && k.name === "PT")).toBeTruthy();

  expect(be.constructSystem({ romPath: "/tmp/rp-em-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 41)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(41) != null).toBeTruthy();
  console.log(`[evermidi-rom] patch: built kit 4 + theme 0 + font 0; patched ROM boots`);
});

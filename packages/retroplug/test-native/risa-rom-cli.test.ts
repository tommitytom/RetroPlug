// End-to-end for the `retroplug-cli risa-rom` verbs, driven through the real tool against a real backend +
// audio driver (mirrors test-native/lsdj-rom.test.ts). Gated on the built risa ROM — SKIPs when absent.
// The tool only touches s.backend + s.audio, so a minimal Session over the real backend/driver suffices.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { Session } from "../cli/session";
import { risaRomTool } from "../cli/sessions/risa-rom";
import { RisaRom, bankToModel, decodeThemeFromRom } from "../src/risa/rom";
import { encodeWav } from "../cli/wav";

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";

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
  if (!be.writeFile(to, be.readFile(RISA_ROM)!)) throw new Error(`copy failed: ${to}`);
  return to;
};
const skip = (be: ReturnType<typeof createRealBackend>, what: string): boolean => {
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-rom ${what}: no ROM at ${RISA_ROM}`); return true; }
  return false;
};

test("risa-rom build-kit writes a bootable .rkit that import-kit places into a ROM", () => {
  const { be, audio, s } = toolSession();
  if (skip(be, "build-kit")) return;
  writeSine(be, "/tmp/rp-rk-src.wav", 220);
  expect(be.writeFile("/tmp/rp-rk-spec.json", jenc({ name: "MYDR", build: [{ file: "/tmp/rp-rk-src.wav", name: "BD" }] }))).toBeTruthy();

  risaRomTool.run(s, ["build-kit", "/tmp/rp-rk-spec.json", "/tmp/rp-rk.rkit"]);
  const kit = be.readFile("/tmp/rp-rk.rkit")!;
  expect(kit.length).toBe(0x2000); // a raw 8 KB bank, not a ROM
  expect(bankToModel(kit).name).toBe("MYDR");

  const rom = copyRom(be, "/tmp/rp-rk.nes");
  risaRomTool.run(s, ["import-kit", rom, "/tmp/rp-rk.rkit", "0", "--out", rom]);
  const patched = RisaRom.fromBytes(be.readFile(rom)!);
  expect(patched.isKitPopulated(0)).toBe(true);
  expect(patched.kits().some((k) => k.slot === 0 && k.name === "MYDR")).toBe(true);

  expect(be.constructSystem({ romPath: rom, platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 30)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(30) != null).toBeTruthy();
  console.log(`[risa-rom] build-kit → .rkit (8KB) → import-kit slot 0 (MYDR/BD); ROM boots`);
});

test("risa-rom export-theme → import-theme round-trips a .rit", () => {
  const { be, s } = toolSession();
  if (skip(be, "theme")) return;
  const rom0 = RisaRom.fromBytes(be.readFile(RISA_ROM)!);
  const want = decodeThemeFromRom(rom0.getTheme(0)!.recordBytes, rom0.getTheme(0)!.nameBytes);

  risaRomTool.run(s, ["export-theme", RISA_ROM, "0", "/tmp/rp-th.rit"]);
  const nes = copyRom(be, "/tmp/rp-th.nes");
  risaRomTool.run(s, ["import-theme", nes, "/tmp/rp-th.rit", "5", "--out", nes]);

  const rom = RisaRom.fromBytes(be.readFile(nes)!);
  const got = decodeThemeFromRom(rom.getTheme(5)!.recordBytes, rom.getTheme(5)!.nameBytes);
  expect(got).toEqual(want); // theme 0 → .rit → theme 5, decoded identically
  expect([...be.readFile(RISA_ROM)!]).toEqual([...rom0.bytes()]); // source ROM untouched
});

test("risa-rom export-font → import-font round-trips a .chr", () => {
  const { be, s } = toolSession();
  if (skip(be, "font")) return;
  const rom0 = RisaRom.fromBytes(be.readFile(RISA_ROM)!);
  const want = rom0.getChrFontSlot(0)!;

  risaRomTool.run(s, ["export-font", RISA_ROM, "0", "/tmp/rp-ft.chr"]);
  expect(be.readFile("/tmp/rp-ft.chr")!.length).toBe(0x2000);
  const nes = copyRom(be, "/tmp/rp-ft.nes");
  risaRomTool.run(s, ["import-font", nes, "/tmp/rp-ft.chr", "1", "--out", nes]);

  expect([...RisaRom.fromBytes(be.readFile(nes)!).getChrFontSlot(1)!]).toEqual([...want]);
});

test("risa-rom import-sample splices a sample into a kit, remove-sample empties a slot", () => {
  const { be, s } = toolSession();
  if (skip(be, "import-sample")) return;
  // Start from a 1-sample kit in slot 0.
  writeSine(be, "/tmp/rp-is-a.wav", 200);
  writeSine(be, "/tmp/rp-is-b.wav", 500);
  expect(be.writeFile("/tmp/rp-is-spec.json", jenc({ name: "KT", build: [{ file: "/tmp/rp-is-a.wav", name: "AAA" }] }))).toBeTruthy();
  risaRomTool.run(s, ["build-kit", "/tmp/rp-is-spec.json", "/tmp/rp-is.rkit"]);
  const nes = copyRom(be, "/tmp/rp-is.nes");
  risaRomTool.run(s, ["import-kit", nes, "/tmp/rp-is.rkit", "0", "--out", nes]);

  // import-sample lands in the first empty slot (1).
  risaRomTool.run(s, ["import-sample", nes, "0", "/tmp/rp-is-b.wav", "--name", "BBB", "--out", nes]);
  let model = bankToModel(RisaRom.fromBytes(be.readFile(nes)!).getKitBank(0)!);
  expect(model.slots[0]?.name).toBe("AAA");
  expect(model.slots[1]?.name).toBe("BBB");

  // remove-sample empties slot 0; slot 1 stays put (index preserved).
  risaRomTool.run(s, ["remove-sample", nes, "0", "0", "--out", nes]);
  model = bankToModel(RisaRom.fromBytes(be.readFile(nes)!).getKitBank(0)!);
  expect(model.slots[0]).toBe(null);
  expect(model.slots[1]?.name).toBe("BBB");
  console.log(`[risa-rom] import-sample (slot 1) + remove-sample (slot 0) — index-addressed splice`);
});

test("risa-rom patch realizes a mixed manifest (build kit + import theme + import font) and boots", () => {
  const { be, audio, s } = toolSession();
  if (skip(be, "patch")) return;
  writeSine(be, "/tmp/rp-pt.wav", 300);
  // Seed a .rit + .chr by exporting from the base ROM.
  risaRomTool.run(s, ["export-theme", RISA_ROM, "0", "/tmp/rp-pt.rit"]);
  risaRomTool.run(s, ["export-font", RISA_ROM, "0", "/tmp/rp-pt.chr"]);
  const manifest = {
    kits: [{ slot: 1, name: "PT", build: [{ file: "/tmp/rp-pt.wav", name: "KIK" }] }],
    themes: [{ slot: 3, file: "/tmp/rp-pt.rit" }],
    fonts: [{ slot: 2, file: "/tmp/rp-pt.chr" }],
  };
  expect(be.writeFile("/tmp/rp-pt-manifest.json", jenc(manifest))).toBeTruthy();

  const src = copyRom(be, "/tmp/rp-pt-src.nes");
  risaRomTool.run(s, ["patch", src, "/tmp/rp-pt-manifest.json", "/tmp/rp-pt-out.nes"]);
  const rom = RisaRom.fromBytes(be.readFile("/tmp/rp-pt-out.nes")!);
  expect(rom.kits().some((k) => k.slot === 1 && k.name === "PT")).toBeTruthy();

  expect(be.constructSystem({ romPath: "/tmp/rp-pt-out.nes", platform: "nes", core: "mesen", embeddedRom: "", savPath: null, statePath: null }, 31)).toBeTruthy();
  audio.renderAudio(300);
  expect(be.getFrame(31) != null).toBeTruthy();
  console.log(`[risa-rom] patch: built kit 1 + theme 3 + font 2; patched ROM boots`);
});

// M5 native proof for the risa NES-DPCM (DMC) kit compiler: author a WAV → compileDmc → an 8 KB DMC kit
// bank → decode the DPCM back to audio (round-trip, the r8brain path is not byte-parity with wav2dmc so we
// gate on the delta/pack/decode invariants) → splice it into a real risa ROM via setKit (bank + mirror) →
// the patched ROM boots. SKIPs when the built risa ROM is absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RisaRom, bankToModel, isBankPopulated, dpcmDecode, assembleKitBank } from "../src/risa/rom";
import { applyOverridesToRom, type RisaAssetOverride } from "../src/risaAssetsRole";
import { encodeWav } from "../cli/wav";

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const WAV = "/tmp/rp-risa-kit-src.wav";

function writeTestWav(be: ReturnType<typeof createRealBackend>, path: string, frames = 8000): void {
  writeSine(be, path, 220, frames);
}

function writeSine(be: ReturnType<typeof createRealBackend>, path: string, freq: number, frames: number): void {
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * freq) * Math.exp(-i / 3000);
  if (!be.writeFile(path, encodeWav(pcm, 44100, 1))) throw new Error(`write failed: ${path}`);
}

function peakOf(a: Float32Array): number {
  let p = 0;
  for (const v of a) p = Math.max(p, Math.abs(v));
  return p;
}

test("compileDmc renders a WAV into an 8 KB DMC kit bank that decodes back to audio", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  writeTestWav(be, WAV);

  const bank = audio.compileDmc({ name: "TEST", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
  expect(bank.length).toBe(0x2000);
  expect(isBankPopulated(bank)).toBe(true); // 0xA5 magic

  const model = bankToModel(bank);
  expect(model.name).toBe("TEST");
  const slot = model.slots[0]!;
  expect(slot).toBeTruthy();
  expect(slot.name).toBe("KIK");
  expect(slot.rate).toBe(12);
  expect(slot.addr).toBe(0); // first sample packs at offset 0
  // Legal DMC length: 16k+1, capped at 4081.
  expect((slot.dpcm.length - 1) % 16).toBe(0);
  expect(slot.dpcm.length <= 4081).toBe(true);
  expect(slot.dpcm.length > 1).toBe(true);

  // The decaying sine survives the ±2 delta encode → the decode has real energy (r8brain, not byte-parity).
  const peak = peakOf(dpcmDecode(slot.dpcm));
  console.log(`[risa-kit] TEST/KIK → ${bank.length}B bank, ${slot.dpcm.length}B DPCM, decoded peak ${peak.toFixed(2)}`);
  expect(peak > 0.1).toBe(true);
});

test("a compiled DMC kit splices into a real risa ROM (bank + mirror) and the ROM boots", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-kit boot: no ROM at ${RISA_ROM}`); return; }
  writeTestWav(be, WAV);

  const bank = audio.compileDmc({ name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
  const rom = RisaRom.fromBytes(be.readFile(RISA_ROM)!);
  expect(rom.isRisa).toBe(true);
  expect(rom.hasKitMeta).toBe(true); // the mirror is located (needed for the dual-write)

  rom.setKit(0, bank);
  expect(rom.isKitPopulated(0)).toBe(true);
  expect([...rom.getKitBank(0)!]).toEqual([...bank]);
  expect(rom.kits().some((k) => k.slot === 0 && k.name === "DRUMS")).toBe(true); // mirror-consistent

  // The patched image boots (also exercises the Mesen romBytes path), on-disk ROM untouched.
  const base = be.readFile(RISA_ROM)!;
  expect(be.constructSystem({
    romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: rom.bytes(),
  }, 21)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(21) != null).toBe(true);
  expect([...be.readFile(RISA_ROM)!]).toEqual([...base]);
  console.log(`[risa-kit] compiled kit spliced into risa ROM; patched image boots; on-disk .nes unchanged`);
});

test("assembleKitBank re-packs separately-compiled DPCM byte-identically to a whole-kit compileDmc", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const WAV_A = "/tmp/rp-risa-parity-a.wav";
  const WAV_B = "/tmp/rp-risa-parity-b.wav";
  writeSine(be, WAV_A, 220, 6000);
  writeSine(be, WAV_B, 440, 4000);

  // A whole-kit compile of both samples together (native assemble).
  const both = audio.compileDmc({
    name: "DRUMS",
    samples: [
      { path: WAV_A, name: "AAA", rate: 12, effects: [] },
      { path: WAV_B, name: "BBB", rate: 8, loop: true, effects: [] },
    ],
  });
  expect(both.length).toBe(0x2000);

  // Compile each sample ALONE, pull its DPCM, and re-pack with the TS assembler. Since encode() carries no
  // cross-sample state, this must be byte-identical to the whole-kit compile above.
  const a = bankToModel(audio.compileDmc({ name: "", samples: [{ path: WAV_A, name: "AAA", rate: 12, effects: [] }] })).slots[0]!;
  const b = bankToModel(audio.compileDmc({ name: "", samples: [{ path: WAV_B, name: "BBB", rate: 8, loop: true, effects: [] }] })).slots[0]!;
  const reassembled = assembleKitBank("DRUMS", [
    { dpcm: a.dpcm, rate: a.rate, loop: a.loop, name: "AAA" },
    { dpcm: b.dpcm, rate: b.rate, loop: b.loop, name: "BBB" },
  ]);

  expect([...reassembled]).toEqual([...both]); // the TS packer matches native RisaDmcCodec::assemble
  console.log(`[risa-kit] assembleKitBank == compileDmc (${both.length}B, 2 samples) — byte-identical`);
});

test("the risa-assets kit override links a pre-built .rkit at construct (offline compile) and boots", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  if (!be.fileExists(RISA_ROM)) { console.log(`# SKIP risa-kit link: no ROM at ${RISA_ROM}`); return; }
  writeTestWav(be, WAV);

  // Compile a bank OFFLINE (the plugin can't reach compileDmc) and persist it as a .rkit — exactly what the
  // Kits menu's "Export..." writes and "Replace from Disk..." later links.
  const bank = audio.compileDmc({ name: "DRUMS", samples: [{ path: WAV, name: "KIK", rate: 12, effects: [] }] });
  const KIT = "/tmp/rp-risa-kit.rkit";
  expect(be.writeFileAtomic(KIT, bank)).toBe(true);

  // The risa-assets role folds the LINKED bank into the base ROM in memory (the onConstruct path: read the
  // .rkit by path via caps.readFile → setKit → romBytes), leaving the base .nes on disk untouched.
  const base = be.readFile(RISA_ROM)!;
  const overrides: RisaAssetOverride[] = [{ type: "kit", slot: 0, name: "DRUMS", path: KIT }];
  const patched = applyOverridesToRom(base, overrides, be);
  expect(patched !== base).toBe(true);
  const rom = RisaRom.fromBytes(patched);
  expect([...rom.getKitBank(0)!]).toEqual([...bank]); // bank linked from disk, byte-for-byte
  expect(rom.kits().some((k) => k.slot === 0 && k.name === "DRUMS")).toBe(true); // mirror-consistent

  // Mesen boots from the patched bytes (the romBytes channel); on-disk .nes + .rkit unchanged.
  expect(be.constructSystem({
    romPath: RISA_ROM, platform: "nes", core: "mesen", embeddedRom: "",
    savPath: null, statePath: null, romBytes: patched,
  }, 22)).toBeTruthy();
  audio.renderAudio(600);
  expect(be.getFrame(22) != null).toBe(true);
  expect([...be.readFile(RISA_ROM)!]).toEqual([...base]);
  expect([...be.readFile(KIT)!]).toEqual([...bank]);
  console.log(`[risa-kit] .rkit linked by path at construct; patched image boots; on-disk .nes + .rkit unchanged`);
});

// M5 native proof for the risa NES-DPCM (DMC) kit compiler: author a WAV → compileDmc → an 8 KB DMC kit
// bank → decode the DPCM back to audio (round-trip, the r8brain path is not byte-parity with wav2dmc so we
// gate on the delta/pack/decode invariants) → splice it into a real risa ROM via setKit (bank + mirror) →
// the patched ROM boots. SKIPs when the built risa ROM is absent.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RisaRom, bankToModel, isBankPopulated, dpcmDecode } from "../src/risa/rom";
import { encodeWav } from "../cli/wav";

const RISA_ROM = "/workspaces/risa-v2.2.1-source/build/risa-pal.nes";
const WAV = "/tmp/rp-risa-kit-src.wav";

function writeTestWav(be: ReturnType<typeof createRealBackend>, path: string, frames = 8000): void {
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 220) * Math.exp(-i / 3000);
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

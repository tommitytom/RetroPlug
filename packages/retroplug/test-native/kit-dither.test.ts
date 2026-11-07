// The kit compiler's two TPDF dithers are seeded from the samples they quantise, so two compiles of one sample give
// one bank. A risa kit's DMC encode skips the dither, so an LSDj kit compile is the only path that reaches one; its
// callers are the CLI sessions (`cli/sessions/lsdj-rom.ts`) and the tests.
//
// Everything here goes through the path a session uses (audioDriver.compileKit -> the TypeScript kit compiler -> its
// offline jobs -> rp.dsp.dither), not the kernel directly, so the case covers the effect settings decoding and the
// node's seeded quantise as well.
// The bank is read back through `KitView` before it is compared: an empty or undecoded slot would make two compiles
// agree byte for byte for the wrong reason, so the readback holds the slot to a real sample's shape (both polarities
// and a span no constant gives) and not only to a peak. LSDj stores inverted nibbles, so an all-zero data region
// decodes to a constant +1.0 and would satisfy a peak alone. Each dithered bank must also differ from the same source
// compiled with no effects, which is what proves the descriptor reached the compiler and the algorithm ran. The source
// WAV is generated, so this needs no fixtures.
import { test, expect } from "@retroplug/testing";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { KitEffect } from "../src/audioDriver";
import { KitView } from "@retroplug/lsdj/rom/kit";
import { KIT_LOOKUP, BANK_SIZE, ROM_SIZE } from "@retroplug/lsdj/rom/constants";
import { encodeWav } from "../cli/wav";

const WAV = "/tmp/rp-kit-dither.wav";

// A decaying 330 Hz tone at 44.1k, so the dither quantises a real signal through its whole range.
function writeSource(be: ReturnType<typeof createRealBackend>): void {
  const frames = 6000;
  const pcm = new Float32Array(frames);
  for (let i = 0; i < frames; i++) pcm[i] = Math.sin((i / 44100) * 2 * Math.PI * 330) * Math.exp(-i / 2500);
  if (!be.writeFile(WAV, encodeWav(pcm, 44100, 1))) throw new Error(`write failed: ${WAV}`);
}

// Drop a compiled bank into a fresh ROM's first kit slot and read the slot back, the way a ROM's own kit is read.
function readBack(bank: Uint8Array): { frames: number; lo: number; hi: number } {
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bank, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.valid).toBeTruthy();
  expect(kit.sampleCount()).toBe(1);
  const pcm = kit.sampleData(0);
  let lo = Infinity, hi = -Infinity;
  for (const v of pcm) {
    lo = Math.min(lo, v);
    hi = Math.max(hi, v);
  }
  return { frames: pcm.length, lo, hi };
}

function firstDifference(a: Uint8Array, b: Uint8Array): number {
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return i;
  return -1;
}

test("compileKit gives identical bytes twice for each TPDF dither", async () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  writeSource(be);

  // The control: the same source with no effect at all. Its slot is a real, varied sample too, so a difference from
  // the dithered banks below is the dither's and not an empty slot's.
  const plain = (await audio.compileKit({ name: "DITH", samples: [{ path: WAV, name: "SIN", effects: [] }] })).bytes;
  const plainRead = readBack(plain);
  expect(plainRead.frames).toBeGreaterThan(0);
  expect(plainRead.hi - plainRead.lo).toBeGreaterThan(0.5);

  for (const ditherType of ["HighPassTPDF", "ShapedTPDF"] as const) {
    const effects: KitEffect[] = [{ type: "dither", ditherType }];
    const spec = { name: "DITH", samples: [{ path: WAV, name: "SIN", effects }] };

    const first = (await audio.compileKit(spec)).bytes;
    const second = (await audio.compileKit(spec)).bytes;

    // The compared bank is a whole 16 KB bank whose slot decoded to a varying sample in both polarities (a decaying
    // sine keeps its energy through the resample and the 4-bit quantise), so "identical" can never mean "identically
    // empty" or "identically constant".
    expect(first.length).toBe(BANK_SIZE);
    expect(second.length).toBe(BANK_SIZE);
    const { frames, lo, hi } = readBack(first);
    expect(frames).toBeGreaterThan(0);
    expect(hi - lo).toBeGreaterThan(0.5);
    expect(lo).toBeLessThan(0);
    expect(hi).toBeGreaterThan(0);

    expect(firstDifference(first, second)).toBe(-1);
    // The dither ran: the descriptor decoded and the algorithm quantised, so the bank is not the undithered one.
    expect(firstDifference(first, plain)).toBeGreaterThan(-1);
    console.log(
      `[kit-dither] ${ditherType}: two compiles agree over ${first.length} bytes, ` +
        `slot ${frames} frames, levels ${lo.toFixed(2)}..${hi.toFixed(2)}`,
    );
  }
});

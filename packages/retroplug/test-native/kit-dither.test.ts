// The kit compiler's two TPDF dithers are seeded from the samples they quantise, so two compiles of one sample give
// one bank. The callers that compile a kit from scratch are the CLI sessions (`cli/sessions/lsdj-rom.ts`), and today
// that is the only path that reaches `KitCompiler`.
//
// Everything here goes through the path a session uses (audioDriver.compileKit -> the harness RPC -> KitCompiler ->
// LsdjKitCodec), not the codec directly, so the case covers the effect descriptor decoding and the per-slot fan-out
// as well. The bank is read back through `KitView` before it is compared: an empty or undecoded slot would make two
// compiles agree byte for byte for the wrong reason. The source WAV is generated, so this needs no fixtures.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import type { KitEffect } from "../src/audioDriver";
import { KitView } from "../src/lsdj/rom/kit";
import { KIT_LOOKUP, BANK_SIZE, ROM_SIZE } from "../src/lsdj/rom/constants";
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
function readBack(bank: Uint8Array): { frames: number; peak: number } {
  const rom = new Uint8Array(ROM_SIZE);
  rom.set(bank, KIT_LOOKUP[0] * BANK_SIZE);
  const kit = new KitView(rom, 0);
  expect(kit.valid).toBeTruthy();
  expect(kit.sampleCount()).toBe(1);
  const pcm = kit.sampleData(0);
  let peak = 0;
  for (const v of pcm) peak = Math.max(peak, Math.abs(v));
  return { frames: pcm.length, peak };
}

test("compileKit gives identical bytes twice for each TPDF dither", () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  writeSource(be);

  for (const ditherType of ["HighPassTPDF", "ShapedTPDF"] as const) {
    const effects: KitEffect[] = [{ type: "dither", ditherType }];
    const spec = { name: "DITH", samples: [{ path: WAV, name: "SIN", effects }] };

    const first = audio.compileKit(spec);
    const second = audio.compileKit(spec);

    // The compared bank is a whole 16 KB bank whose slot decoded to audible samples (a decaying sine keeps its
    // energy through the resample and the 4-bit quantise), so "identical" can never mean "identically empty".
    expect(first.length).toBe(BANK_SIZE);
    expect(second.length).toBe(BANK_SIZE);
    const { frames, peak } = readBack(first);
    expect(frames > 0).toBeTruthy();
    expect(peak > 0.3).toBeTruthy();

    let firstDiff = -1;
    for (let i = 0; i < first.length; i++) {
      if (first[i] !== second[i]) {
        firstDiff = i;
        break;
      }
    }
    expect(firstDiff).toBe(-1);
    console.log(
      `[kit-dither] ${ditherType}: two compiles agree over ${first.length} bytes, ` +
        `slot ${frames} frames, peak ${peak.toFixed(2)}`,
    );
  }
});

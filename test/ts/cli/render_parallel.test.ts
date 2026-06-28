// Coverage for emu.renderWavPerSystemParallel — the parallel offline render
// (system/OfflineRender.cpp) wired through the harness. The C++
// ParallelRenderTests prove byte-identity vs the single-threaded path; this just
// checks the WAV plumbing and that the mix is the per-sample sum of the
// per-system files (within int16 rounding).
import { test, expect, emu, Routing } from "harness";

const MGB = "resources/roms/mGB.gb";

// A WavWriter file starts with the standard RIFF/WAVE chunk header.
const isWav = (b: Uint8Array): boolean =>
  b.length > 44 &&
  b[0] === 0x52 && b[1] === 0x49 && b[2] === 0x46 && b[3] === 0x46 && // "RIFF"
  b[8] === 0x57 && b[9] === 0x41 && b[10] === 0x56 && b[11] === 0x45; // "WAVE"

// Decode interleaved 16-bit PCM from a minimal WavWriter file (44-byte header).
function pcm16(b: Uint8Array): Int16Array {
  const dv = new DataView(b.buffer, b.byteOffset + 44, b.length - 44);
  const out = new Int16Array((b.length - 44) >> 1);
  for (let i = 0; i < out.length; i++) out[i] = dv.getInt16(i * 2, true);
  return out;
}

test("renderWavPerSystemParallel writes valid WAVs whose mix == sum of per-system", () => {
  emu.loadRom(MGB); // system 0
  emu.loadRom(MGB); // system 1
  emu.runMs(1500);
  // Distinct note per instance (channel nibble -> system) so the two per-system
  // outputs DIFFER — otherwise p0==p1 and the mix==sum check is invariant under a
  // slot swap, proving nothing about per-system routing.
  emu.dispatchMidi([0x90, 60, 100], Routing.OneChannelPerInstance); // ch0 -> sys0
  emu.dispatchMidi([0x91, 67, 100], Routing.OneChannelPerInstance); // ch1 -> sys1

  const mix = "/tmp/rp-par-mix.test.wav";
  const s0 = "/tmp/rp-par-s0.test.wav";
  const s1 = "/tmp/rp-par-s1.test.wav";
  emu.renderWavPerSystemParallel(mix, [s0, s1], 300);

  const mb = emu.readFile(mix);
  const b0 = emu.readFile(s0);
  const b1 = emu.readFile(s1);
  for (const b of [mb, b0, b1]) expect(isWav(b)).toBeTruthy();
  // One window -> same length for the mix and per-system files.
  expect(mb.length).toBe(b0.length);
  expect(b0.length).toBe(b1.length);

  const m = pcm16(mb);
  const p0 = pcm16(b0);
  const p1 = pcm16(b1);
  expect(m.length).toBe(p0.length);

  // Per-system isolation: distinct notes -> the two files must actually differ
  // (a degenerate p0==p1 would make the sum check swap-invariant).
  let diff = 0;
  let energy = 0;
  let within = 0;
  for (let i = 0; i < m.length; i++) {
    if (p0[i] !== p1[i]) diff++;
    // The mix is the per-sample sum of the (float) per-system buffers, quantized;
    // independent int16 rounding allows a couple of LSB of slack.
    const sum = Math.max(-32768, Math.min(32767, p0[i] + p1[i]));
    if (Math.abs(m[i] - sum) <= 2) within++;
    energy += Math.abs(m[i]);
  }
  expect(energy).toBeGreaterThan(0);                 // non-silent render
  expect(diff).toBeGreaterThan(0);                   // per-system outputs are distinct
  expect(within / m.length).toBeGreaterThan(0.99);   // mix ≈ sum of per-system
});

// "Expansion Volume" (the `mesen` role's expansionVolume, System menu) against a REAL Mesen NES core: the
// level of the CARTRIDGE's own sound chip, as a percentage of the level the hardware mixes it at. Mesen keeps
// a per-channel volume for each expansion chip and scales its contribution by volume/100 in
// GetExpansionOutput, so this sets all six together - a cart has at most one.
//
// Two things have to hold for the knob to mean what the menu says, and only a render can show either: it
// moves the expansion chip (0% silences it), and it leaves the console's own 2A03 exactly where it was. The
// same value is what the UI writes to a connected Everdrive N8's `master_vol` (expVolToN8), which is why it
// is expressed against unity rather than in dB.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

declare const __REPO_RESOURCES_DIR__: string;
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes";

const cc = (ch: number, num: number, val: number) => [0xb0 | (ch - 1), num, val];
const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

function rms(x: Float32Array, from = 0, to = x.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += x[i] * x[i];
  return Math.sqrt(s / Math.max(to - from, 1));
}

/** Hold one note on `channel` with the role's expansionVolume at `percent`, and report the sustain's level.
 *  ch6 is the ROM's first VRC6 pulse; ch1 is 2A03 pulse 1. */
function levelAt(percent: number, channel: number): number {
  const s = bootSession();
  const id = s.project.systems.addSystem(VRC6);
  if (id == null) throw new Error("addSystem failed");
  if (!s.project.systems.setRoleConfig(id, "mesen", { expansionVolume: percent })) throw new Error("setRoleConfig failed");
  const tl = new Timeline()
    .midi(20, cc(channel, 7, 127))
    .note(100, 69, { durationMs: 1200, channel, velocity: 100 });
  const pcm = renderTimeline(s, tl, { durationMs: 1400, warmupMs: 1200 });
  return db(rms(pcm, Math.floor(pcm.length * 0.3), Math.floor(pcm.length * 0.8)));
}

test("expansionVolume scales the cartridge sound chip, and 0 silences it", () => {
  const full = levelAt(100, 6);
  const half = levelAt(50, 6);
  const off = levelAt(0, 6);
  console.log(`[expvol] VRC6 pulse: 100% ${full.toFixed(2)} dBFS, 50% ${half.toFixed(2)}, 0% ${off.toFixed(2)}`);

  expect(full, "the VRC6 sounds at unity").toBeGreaterThan(-60);
  // 50% is a halving of the chip's contribution to the mix: -6 dB, with room for the mixer's non-linearity.
  expect(half, "50% is ~6 dB down").toBeCloseTo(full - 6, 2);
  expect(off, "0% mutes the chip entirely").toBeLessThan(-80);
});

test("expansionVolume leaves the console's own 2A03 alone", () => {
  // The register it mirrors on hardware (`master_vol`) scales only the cartridge's audio path, never the
  // console's. A knob that quietly pulled the 2A03 down with it would be a different feature.
  const full = levelAt(100, 1);
  const off = levelAt(0, 1);
  console.log(`[expvol] 2A03 pulse 1: expansion at 100% ${full.toFixed(2)} dBFS, at 0% ${off.toFixed(2)}`);

  expect(full, "the 2A03 pulse sounds").toBeGreaterThan(-60);
  expect(off, "muting the expansion chip does not touch it").toBeCloseTo(full, 0.5);
});

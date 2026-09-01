// The two "mesen" role switches that choose between the documented CHIP behaviour and an Everdrive N8
// Pro's FPGA core. Both default to "chip" so ordinary games are untouched; "n8" exists so software
// developed against that cartridge (BlipToaster) sounds the same in the emulator as it does on the console.
//
// Each is a real behavioural fork, measured on a real NES + N8 (PAL, capture ch5):
//   s5bNoise       - the N8's 5B has no noise generator, and since the mixer ANDs tone with noise,
//                    enabling noise MUTES the channel: -34.09 dBFS -> -81.32, at every noise period.
//                    A real 5B rasps instead.
//   mmc5PhaseReset - the N8's MMC5 does not restart the duty sequencer on a $5003 write. Under BlipToaster's
//                    MOD hack the real MMC5 holds duty 0.500 and does not change level at ANY reset rate
//                    (only its pitch moves), where the 2A03 skews to 0.067 duty and drops 7.2 dB.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

const S5B_ROM = "/workspaces/evermidi/rom/build/bliptoaster-s5b.nes";
const MMC5_ROM = "/workspaces/evermidi/rom/build/bliptoaster-mmc5.nes";

const cc = (ch: number, num: number, val: number) => [0xb0 | (ch - 1), num, val];
const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

function rms(x: Float32Array, from = 0, to = x.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += x[i] * x[i];
  return Math.sqrt(s / Math.max(to - from, 1));
}

/** Duty: the fraction of the sustain above its own mean. A clean square sits at ~0.5; a phase reset that
 *  truncates cycles pushes it away, which is exactly what the two MMC5 modes differ on. */
function duty(pcm: Float32Array): number {
  const from = Math.floor(pcm.length * 0.25);
  const to = Math.floor(pcm.length * 0.85);
  let mean = 0;
  for (let i = from; i < to; i++) mean += pcm[i];
  mean /= Math.max(to - from, 1);
  let above = 0;
  for (let i = from; i < to; i++) if (pcm[i] > mean) above++;
  return above / Math.max(to - from, 1);
}

function boot(rom: string, mode: "chip" | "n8", field: "s5bNoise" | "mmc5PhaseReset") {
  const s = bootSession();
  if (!s.backend.fileExists(rom)) return null;
  const id = s.project.systems.addSystem(rom);
  if (id == null) throw new Error("addSystem failed");
  if (!s.project.systems.setRoleConfig(id, "mesen", { [field]: mode })) throw new Error("setRoleConfig failed");
  return s;
}

test("s5bNoise: 'chip' rasps where 'n8' mutes the channel", () => {
  const levels: Record<string, number> = {};
  for (const mode of ["chip", "n8"] as const) {
    const s = boot(S5B_ROM, mode, "s5bNoise");
    if (!s) { console.log(`# SKIP: no ROM at ${S5B_ROM}`); return; }
    // CC1 must come AFTER the note-on: BlipToaster's s5b_note_on unconditionally sets the noise-disable bit.
    const tl = new Timeline()
      .midi(20, cc(6, 20, 0))
      .midi(40, cc(6, 7, 127))
      .midi(60, cc(6, 30, 64))
      .note(100, 69, { durationMs: 1500, channel: 6, velocity: 100 })
      .midi(700, cc(6, 1, 127)); // noise on, mid-note
    const pcm = renderTimeline(s, tl, { durationMs: 1700, warmupMs: 1200 });
    levels[mode] = db(rms(pcm, Math.floor(pcm.length * 0.65), Math.floor(pcm.length * 0.95)));
    console.log(`[accuracy] s5bNoise=${mode}: after noise-on ${levels[mode].toFixed(2)} dBFS`);
  }
  expect(levels.chip > -60).toBeTruthy();            // the chip keeps making sound (tone gated by the LFSR)
  expect(levels.n8 < levels.chip - 30).toBeTruthy(); // the N8 gates it off entirely
});

test("mmc5PhaseReset: 'n8' keeps a 50% duty where 'chip' skews it", () => {
  const duties: Record<string, number> = {};
  for (const mode of ["chip", "n8"] as const) {
    const s = boot(MMC5_ROM, mode, "mmc5PhaseReset");
    if (!s) { console.log(`# SKIP: no ROM at ${MMC5_ROM}`); return; }
    const tl = new Timeline()
      .midi(0, cc(6, 7, 127))
      .midi(20, cc(6, 116, 127)) // fastest reset the shipped ROM allows (floored to reload 65)
      .midi(40, cc(6, 115, 127)) // MOD hack on
      .note(100, 36, { durationMs: 1500, channel: 6, velocity: 100 }); // C2, where the skew is worst
    const pcm = renderTimeline(s, tl, { durationMs: 1700, warmupMs: 1200 });
    duties[mode] = duty(pcm);
    console.log(`[accuracy] mmc5PhaseReset=${mode}: duty ${duties[mode].toFixed(3)}  rms ${db(rms(pcm)).toFixed(2)} dBFS`);
  }
  // Hardware holds 0.500 at every rate; the chip default truncates cycles and pushes duty away from it.
  expect(Math.abs(duties.n8 - 0.5) < 0.1).toBeTruthy();
  expect(Math.abs(duties.n8 - 0.5) < Math.abs(duties.chip - 0.5)).toBeTruthy();
});

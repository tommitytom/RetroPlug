// Guards Mesen's MMC5 pulse under BlipToaster's "MOD hack" (CC115 on, CC116 rate), which re-writes the pulse
// HI register from the idle loop to reset the duty phase.
//
// The bug this locks down: SquareChannel::WriteRam used to run `if(!_isMmc5Square) UpdateOutput();`, so an
// MMC5 pulse never refreshed its output on a register write - Mmc5Square::_currentOutput only moved when
// its timer expired. A phase reset repeated faster than the note's period then walked _dutyPos through the
// same few steps every time, the level never changed, and the channel rendered DIGITAL SILENCE. Hardware
// instead emits a transition on every reset, so it stays at full level and the reset rate becomes the
// audible pitch. Fixed by giving the write path a virtual UpdateOutputAfterWrite(), which the MMC5 square
// overrides to refresh its own latch (it must not push into the 2A03 mixer the way UpdateOutput does).
//
// Measured on a real NES + Everdrive N8 (PAL, capture ch5), reload 1: -31.50 dBFS at C4 and -31.51 at C2,
// i.e. no level change at all from mod-off, with the pitch instead pulled sharp (C2 +2591 cents).
//
// A full CC116 sweep on hardware then explained WHY, and left one gap still open. Duty (fraction of the
// waveform above zero) at reload 1, hardware vs this core:
//
//                    hardware duty   this core   hardware rms change   core rms change
//   MMC5 C4              0.500         0.738           0.0 dB             -1.6 dB
//   MMC5 C2              0.501         0.922           0.0 dB             -5.4 dB
//   2A03 C4              0.216         0.210          -2.3 dB             -2.4 dB
//   2A03 C2              0.067         0.061          -7.2 dB             -5.2 dB
//
// So the 2A03 model is RIGHT: it skews duty to a narrow pulse train exactly as the chip does, and loses
// level in proportion. The real MMC5 does not skew AT ALL - it stays a clean 50% square and just retriggers
// faster, which is why its level never moves. This core still skews it (the wrong way, toward 0.9), because
// Mmc5Square inherits the 2A03's `_dutyPos = 0` phase reset from SquareChannel.
//
// The remaining question is whose behaviour that is. nesdev's MMC5 page says "phase reset [is] the same as
// their APU counterparts", which would predict the 2A03's skew - the hardware says otherwise, so either the
// wiki is imprecise or the Everdrive N8's MMC5 core does not implement the sequencer reset. That cannot be
// told apart without a real MMC5 cartridge, exactly like the 5B noise question.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

const MMC5_ROM = "/workspaces/evermidi/rom/build/bliptoaster-mmc5.nes";
// The shipped ROM FLOORS its reset rate to [65,128], so it cannot reach the rates that exposed the bug.
// An un-floored build (mmc5.c: `128 - val` in place of `128 - (val >> 1)`) is what proves the fix; point
// MMC5_ROM at one to re-run that. Numbers with reload 1, before -> after the core fix:
//   C4  -240.00 dBFS (digital silence) -> -27.71     hardware: -31.50
//   C2  -240.00 dBFS (digital silence) -> -32.32     hardware: -31.51

const CH_MMC5 = 6; // BlipToaster's MMC5 Pulse 1 (BASE01)
const CH_2A03 = 1; // 2A03 Pulse 1 - the unfloored reference the ROM compares against

const cc = (ch: number, num: number, val: number) => [0xb0 | (ch - 1), num, val];

function rms(pcm: Float32Array, from = 0, to = pcm.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += pcm[i] * pcm[i];
  const n = Math.max(to - from, 1);
  return Math.sqrt(s / n);
}

const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

/** Duty (fraction above zero) and crest (peak/rms) of the sustain. On real hardware the MMC5 pulse holds
 *  duty 0.500 at EVERY reset rate - the reset just retriggers a clean 50% square faster, which is why its
 *  level never moves - while the 2A03 skews to 0.067-0.833 and loses level in proportion. */
function shape(pcm: Float32Array): { duty: number; crest: number } {
  const from = Math.floor(pcm.length * 0.25);
  const to = Math.floor(pcm.length * 0.85);
  let above = 0;
  let mean = 0;
  for (let i = from; i < to; i++) mean += pcm[i];
  mean /= Math.max(to - from, 1);
  let peak = 0;
  for (let i = from; i < to; i++) {
    if (pcm[i] - mean > 0) above++;
    peak = Math.max(peak, Math.abs(pcm[i] - mean));
  }
  return { duty: above / Math.max(to - from, 1), crest: peak / Math.max(rms(pcm, from, to), 1e-12) };
}

/** Hold `note` on `ch` for 1.5 s with the MOD hack at `rate`, and report the sustain level in dBFS. */
function modLevelDb(s: ReturnType<typeof bootSession>, ch: number, note: number, rate: number): number {
  const tl = new Timeline()
    .midi(0, cc(ch, 7, 127))
    .midi(20, cc(ch, 116, rate))
    .midi(40, cc(ch, 115, 127))
    .note(100, note, { durationMs: 1500, channel: ch, velocity: 100 });
  const pcm = renderTimeline(s, tl, { durationMs: 1700, warmupMs: 1200 });
  lastShape = shape(pcm);
  // Measure the sustain only (skip attack/release), by fraction so the sample rate need not be known.
  return db(rms(pcm, Math.floor(pcm.length * 0.25), Math.floor(pcm.length * 0.85)));
}

let lastShape = { duty: 0, crest: 0 };
const shapeStr = () => `duty ${lastShape.duty.toFixed(3)} crest ${lastShape.crest.toFixed(2)}`;

test("MMC5 MOD hack: Mesen thins the pulse as the reset rate rises (hardware does not)", () => {
  const s = bootSession();
  if (!s.backend.fileExists(MMC5_ROM)) {
    console.log(`# SKIP mmc5-mod-hack: no ROM at ${MMC5_ROM} (build it: make -C rom MAPPER=mmc5 all)`);
    return;
  }
  const id = s.project.systems.addSystem(MMC5_ROM);
  if (id == null) throw new Error("addSystem failed");
  // PAL, to match the bench NES these numbers are compared against (BlipToaster times a frame to detect the
  // region and picks its PAL tuning table). This only reaches the core since the UpdateRegion fix - see
  // nes-region-apply.test.ts, which probes the APU timer period because rms cannot see a region change.
  // mmc5PhaseReset "chip" explicitly: the DEFAULT is "n8" (no phase reset at all, which is what the
  // cartridge does), and this file guards the chip-behaviour path - the one the silence bug lived in.
  s.project.systems.setRoleConfig(id, "mesen", { region: "pal", mmc5PhaseReset: "chip" });

  // C4 and C2: the ROM's comment says low notes pin to silence soonest, so walk both.
  for (const [name, note] of [["C4", 60], ["C2", 36]] as const) {
    const off = modLevelDb(s, CH_MMC5, note, 0);
    const offShape = shapeStr();
    const slow = modLevelDb(s, CH_MMC5, note, 32);
    const fast = modLevelDb(s, CH_MMC5, note, 127); // floored to reload 65 by the shipped ROM
    console.log(`[mmc5-mod] ${name} MMC5  mod-off ${off.toFixed(2)} (${offShape})  rate32 ${slow.toFixed(2)}  rate127 ${fast.toFixed(2)} dBFS (${shapeStr()})`);

    const ref = modLevelDb(s, CH_2A03, note, 127);
    console.log(`[mmc5-mod] ${name} 2A03  rate127 ${ref.toFixed(2)} dBFS (${shapeStr()})`);

    // The note must still exist at all - a totally dead render would mean the harness, not the core.
    expect(off > -90).toBeTruthy();
    // The MOD hack must never silence the channel, at any rate this ROM can ask for.
    expect(fast > -60).toBeTruthy();
    expect(slow > -60).toBeTruthy();
    // The MMC5 pulse is register-identical to the 2A03 pulse, so under the same hack they should land in
    // the same ballpark. Before the fix this was 208 dB apart (-240 vs -32) on an un-floored build.
    expect(Math.abs(fast - ref) < 10).toBeTruthy();
  }
});

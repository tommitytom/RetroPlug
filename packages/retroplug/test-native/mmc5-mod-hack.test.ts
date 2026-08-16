// Characterises Mesen's MMC5 pulse under EverMIDI's "MOD hack" (CC115 on, CC116 rate), which re-writes the
// pulse HI register from the idle loop to reset the duty phase.
//
// Why this exists: on a real NES + Everdrive N8 the MMC5 pulse holds FULL level at every reset rate, right
// down to reload 1, and the phase reset instead pulls the pitch sharp. In Mesen it thins toward silence.
// The cause is in the core: SquareChannel::WriteRam runs `if(!_isMmc5Square) UpdateOutput();`, so an MMC5
// pulse does not refresh its output on a register write - Mmc5Square::_currentOutput only changes when its
// timer expires. Reset the phase faster than the timer period and the emulated output is pinned to one duty
// step (a constant = silence), where hardware emits a transition on every reset.
//
// So this test does NOT assert hardware parity (the core cannot deliver it). It pins the CURRENT emulated
// behaviour, so that if the core is ever fixed the change is visible here rather than silently shifting the
// ROM's tuning. The hardware numbers are in the EverMIDI hardware-test report.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

const MMC5_ROM = "/workspaces/evermidi/rom/build/n8-midi-mmc5.nes";

const CH_MMC5 = 6; // EverMIDI's MMC5 Pulse 1 (BASE01)
const CH_2A03 = 1; // 2A03 Pulse 1 - the unfloored reference the ROM compares against

const cc = (ch: number, num: number, val: number) => [0xb0 | (ch - 1), num, val];

function rms(pcm: Float32Array, from = 0, to = pcm.length): number {
  let s = 0;
  for (let i = from; i < to; i++) s += pcm[i] * pcm[i];
  const n = Math.max(to - from, 1);
  return Math.sqrt(s / n);
}

const db = (x: number) => 20 * Math.log10(Math.max(x, 1e-12));

/** Hold `note` on `ch` for 1.5 s with the MOD hack at `rate`, and report the sustain level in dBFS. */
function modLevelDb(s: ReturnType<typeof bootSession>, ch: number, note: number, rate: number): number {
  const tl = new Timeline()
    .midi(0, cc(ch, 7, 127))
    .midi(20, cc(ch, 116, rate))
    .midi(40, cc(ch, 115, 127))
    .note(100, note, { durationMs: 1500, channel: ch, velocity: 100 });
  const pcm = renderTimeline(s, tl, { durationMs: 1700, warmupMs: 1200 });
  // Measure the sustain only (skip attack/release), by fraction so the sample rate need not be known.
  return db(rms(pcm, Math.floor(pcm.length * 0.25), Math.floor(pcm.length * 0.85)));
}

test("MMC5 MOD hack: Mesen thins the pulse as the reset rate rises (hardware does not)", () => {
  const s = bootSession();
  if (!s.backend.fileExists(MMC5_ROM)) {
    console.log(`# SKIP mmc5-mod-hack: no ROM at ${MMC5_ROM} (build it: make -C rom MAPPER=mmc5 all)`);
    return;
  }
  if (s.project.systems.addSystem(MMC5_ROM) == null) throw new Error("addSystem failed");

  // C4 and C2: the ROM's comment says low notes pin to silence soonest, so walk both.
  for (const [name, note] of [["C4", 60], ["C2", 36]] as const) {
    const off = modLevelDb(s, CH_MMC5, note, 0);
    const slow = modLevelDb(s, CH_MMC5, note, 32);
    const fast = modLevelDb(s, CH_MMC5, note, 127); // floored to reload 65 by the shipped ROM
    console.log(`[mmc5-mod] ${name} MMC5  mod-off ${off.toFixed(2)}  rate32 ${slow.toFixed(2)}  rate127 ${fast.toFixed(2)} dBFS`);

    const ref = modLevelDb(s, CH_2A03, note, 127);
    console.log(`[mmc5-mod] ${name} 2A03  rate127 ${ref.toFixed(2)} dBFS (unfloored reference)`);

    // The note must still exist at all - a totally dead render would mean the harness, not the core.
    expect(off > -90).toBeTruthy();
  }
});

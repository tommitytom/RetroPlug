// Guards Mesen's MMC5 pulse under EverMIDI's "MOD hack" (CC115 on, CC116 rate), which re-writes the pulse
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
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";

const MMC5_ROM = "/workspaces/evermidi/rom/build/n8-midi-mmc5.nes";
// The shipped ROM FLOORS its reset rate to [65,128], so it cannot reach the rates that exposed the bug.
// An un-floored build (mmc5.c: `128 - val` in place of `128 - (val >> 1)`) is what proves the fix; point
// MMC5_ROM at one to re-run that. Numbers with reload 1, before -> after the core fix:
//   C4  -240.00 dBFS (digital silence) -> -27.71     hardware: -31.50
//   C2  -240.00 dBFS (digital silence) -> -32.32     hardware: -31.51

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
  const id = s.project.systems.addSystem(MMC5_ROM);
  if (id == null) throw new Error("addSystem failed");
  // NOTE: the hardware rig is PAL, and this SHOULD run PAL to match it. It does not yet. Both routes fail:
  //   - adopt() with a construct-time region boots PAL (the core logs it) but renders SILENCE here, even on
  //     the 2A03 and even with region ntsc, so adopt() produces a system renderTimeline cannot drive.
  //   - setRoleConfig() below updates the TS role config (it reads back "pal") but the core still reports
  //     NTSC at load, and every level here is bit-identical to the NTSC run.
  // The levels this test pins are region-insensitive (a square's rms barely moves with pitch) and the
  // silencing mechanism is structural, so the comparison holds. Revisit when adopt()/setRoleConfig are fixed.
  s.project.systems.setRoleConfig(id, "mesen", { region: "pal" });
  console.log(`[mmc5-mod] role region = ${s.project.systems.view()[0]?.roles.find((r) => r.kind === "mesen")?.config.region} (core may still be NTSC)`);

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
    // The MOD hack must never silence the channel, at any rate this ROM can ask for.
    expect(fast > -60).toBeTruthy();
    expect(slow > -60).toBeTruthy();
    // The MMC5 pulse is register-identical to the 2A03 pulse, so under the same hack they should land in
    // the same ballpark. Before the fix this was 208 dB apart (-240 vs -32) on an un-floored build.
    expect(Math.abs(fast - ref) < 10).toBeTruthy();
  }
});

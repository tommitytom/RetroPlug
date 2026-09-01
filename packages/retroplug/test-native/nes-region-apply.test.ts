// Does a NES region change actually reach the core? RMS cannot answer that (a square's level does not
// depend on its frequency), and the "[NTSC]"/"[PAL]" banner is only printed at LoadRom, so both of the
// obvious signals are blind. The APU timer PERIOD is not: EverMIDI carries separate NTSC and PAL tuning
// tables and picks one from the detected region, so the same MIDI note is written as a DIFFERENT period
// per region while coming out at the same pitch in Hz.
//
// This exists because a hardware comparison had to run PAL (the bench NES is PAL) and it was not obvious
// whether the region knob was landing.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

/** Boot the ROM, optionally switch region, then read pulse1 while a C4 is sounding. */
function periodFor(region: "ntsc" | "pal" | null): { period: number; frequency: number } {
  const s = bootSession();
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");
  if (region) {
    if (!s.project.systems.setRoleConfig(id, "mesen", { region })) throw new Error("setRoleConfig failed");
  }
  let apu: ApuState | null = null;
  const tl = new Timeline()
    .note(200, 60, { channel: 1, durationMs: 600 })
    .at(500, (sess) => (apu = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 900, warmupMs: 1500 });
  expect(apu != null).toBeTruthy();
  return { period: apu!.pulse1.period, frequency: apu!.pulse1.frequency };
}

test("a live region change reaches the core: the APU timer period moves with it", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }

  const ntsc = periodFor("ntsc");
  const pal = periodFor("pal");
  console.log(`[region] ntsc period ${ntsc.period} (${ntsc.frequency.toFixed(2)} Hz)`);
  console.log(`[region] pal  period ${pal.period} (${pal.frequency.toFixed(2)} Hz)`);

  // Both must be sounding at all.
  expect(ntsc.period > 0 && pal.period > 0).toBeTruthy();
  // The ROM's per-region tuning tables differ, so the written period must differ...
  expect(ntsc.period !== pal.period).toBeTruthy();
  // ...while both still land on C4, because that is the point of having two tables.
  expect(ntsc.frequency > 240 && ntsc.frequency < 290).toBeTruthy();
  expect(pal.frequency > 240 && pal.frequency < 290).toBeTruthy();
});

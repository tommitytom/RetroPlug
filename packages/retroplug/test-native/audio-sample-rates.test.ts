// Every sample rate Settings > Audio offers must render CORRECT audio, not merely open a device. The
// standalone's list runs to 192 kHz (menuDefs AUDIO_RATES), and nothing in the path clamps: the rate goes
// Engine::setSampleRate -> the core's own setter (GB_set_sample_rate / Mesen's AudioConfig). A core that
// mishandled a rate would still "work" from the host's point of view - it would just play at the wrong
// speed - so this checks the rendered pitch against the emulator's own decoded frequency, which is
// rate-independent ground truth.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { detectPitch, centsError } from "../cli/pitch";
import { window } from "../cli/dsp";
import { type ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

// The 48 k family's top end and the 44.1 k family's, plus 48 k as the control. 22050/32000/44100/88200 take
// the same path with a smaller ratio; these are the ones the ceiling used to exclude.
const RATES = [48000, 88200, 96000, 192000];

test("every offered sample rate renders in tune - including the ones above 48k", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) { console.log("# SKIP: no NES rom"); return; }

  for (const rate of RATES) {
    // setSampleRate requires an empty engine (it is the render-rate seam, deliberately off the audio
    // thread), so the system is added after the rate and removed before the next one.
    expect(s.audio.setSampleRate(rate)).toBeTruthy();
    expect(s.audio.sampleRate()).toBe(rate);

    const id = s.project.systems.addSystem(NES);
    if (id == null) throw new Error("addSystem failed");
    let apu: ApuState | null = null;
    const tl = new Timeline()
      .note(200, 69, { channel: 1, velocity: 100, durationMs: 500 })   // A4 on 2A03 pulse 1
      .at(450, (sess) => (apu = sess.backend.getApuState(id)));
    const pcm = renderTimeline(s, tl, { durationMs: 900, warmupMs: 1100 });
    s.project.systems.removeSystem(id);

    const truth = apu!.pulse1.frequency;   // what the chip thinks it is playing, whatever the output rate
    // A window of constant DURATION (~341 ms, i.e. 16384 frames at 48 k) rather than constant sample count:
    // a fixed count shrinks the analysed span as the rate climbs, which starves the detector rather than
    // testing the renderer.
    const p = detectPitch(window(pcm, 450, Math.round((16384 * rate) / 48000), rate), { sampleRate: rate, fmin: 30 });
    console.log(`# ${rate} Hz: apu=${truth.toFixed(2)}Hz detect=${p.hz.toFixed(2)}Hz (${centsError(p.hz, truth).toFixed(1)}c)`);
    // A rate the core got wrong shows up here as a whole-ratio pitch error (2x = 1200 cents), not a near miss.
    expect(p.hz > 0 && Math.abs(centsError(p.hz, truth)) < 10).toBeTruthy();
  }
});

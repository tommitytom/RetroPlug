// Regression: a channelExportMode (per-channel) NES system whose audio is drained through the MIX path
// (renderAudio) must still play afterwards through renderAudioPerChannel. The bug: MesenNesSystem::
// finishBlock's mix branch drained only the mix ring, never the capture streams. So a mix render that
// produced audio (e.g. a split render's boot settle, where risa emits audio) piled frames up in the capture
// streams. The next renderAudioPerChannel then found AvailableCaptureFrames() already >= the block and
// never stepped the CPU — the core froze: input stopped reaching it and no fresh audio came out. Surfaced as
// risa --split renders that stopped instantly (auto-detect saw "stopped") and were silent. The fix drains +
// discards the capture streams on the mix path too.
//
// Reproduced on n8-midi (adopt without a sav → reliable per-channel path in the test host, unlike a
// sav/seed adopt): sound a note, drain ~1.5 s through the MIX path (fills the capture streams), then switch
// to renderAudioPerChannel. Before the fix the pins are dead; after it the note still rings on the Pulse pin.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;
declare const __REPO_RESOURCES_DIR__: string;

const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const NOTE_ON_CH1 = [0x90, 60, 100]; // ch1 NoteOn C4 → APU Pulse1 → the Pulse pin

const leftRms = (a: Float32Array): number => {
  let s = 0, n = 0;
  for (let i = 0; i < a.length; i += 2) { s += a[i] * a[i]; n++; }
  return n ? Math.sqrt(s / n) : 0;
};

test("NES per-channel capture survives a MIX-path drain (renderAudio) then plays via renderAudioPerChannel", () => {
  const be = createRealBackend();
  if (!be.fileExists(NES)) { console.log(`# SKIP nes-capture-mix-drain: no ROM at ${NES}`); return; }

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  project.systems.adopt({
    romPath: NES,
    roles: [{ kind: "nes-n8-midi", config: {} }, { kind: "mesen", config: { channelExportMode: "stereoModPins" } }],
  });
  syncDspFromStore(project, dsp);
  const id = project.systems.view()[0]!.id;

  // Drain ~1.5 s through the MIX path — this is what fills the capture streams (a split render's renderAudio
  // boot settle; risa emits audio here). n8-midi drops its first MIDI message, so prime it with a note whose
  // envelope has plenty of time to decay, isolating the freeze from note sustain.
  audio.stageMidiIn(NOTE_ON_CH1);
  audio.stageMidiIn(NOTE_ON_CH1);
  audio.renderAudio(1500); // MIX drain: before the fix, the capture streams pile up undrained here

  // Now sound a FRESH note and pull per-channel. If the capture streams were left full by the mix drain,
  // stepIfBelowTarget never runs the CPU: the fresh note never reaches the APU and every pin stays dead.
  // With the fix the capture was drained, the CPU keeps stepping, and the note rings on the Pulse pin.
  audio.stageMidiIn(NOTE_ON_CH1);
  let pulsePeak = 0, streamCount = 0;
  for (let i = 0; i < 8; i++) {
    const streams = audio.renderAudioPerChannel(id, 100);
    streamCount = streams.length;
    if (streams.length >= 1) pulsePeak = Math.max(pulsePeak, leftRms(streams[0])); // stream 0 = Pulse pin
  }
  console.log(`[nes-capture-mix-drain] streams=${streamCount} pulsePeak=${pulsePeak.toFixed(5)}`);
  expect(streamCount).toBe(3); // Pulse / TND / Expansion — the per-channel path is alive (not stalled to 0)
  expect(pulsePeak > 0.001).toBe(true); // the core kept stepping through the mix drain → the note still rings
});

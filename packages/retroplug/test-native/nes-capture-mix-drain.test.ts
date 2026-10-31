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
import { test, expect, skip } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

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
  if (!be.fileExists(NES)) skip(`nes-capture-mix-drain: no ROM at ${NES}`);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver(project.graph);

  project.systems.adopt({
    romPath: NES,
    roles: [{ kind: "nes-n8-midi", config: {} }, { kind: "mesen", config: { channelExportMode: "stereoModPins" } }],
  });
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

test("a NES split export's 1500 ms mix settle is the unarmed mix's, so the render does not drift", () => {
  // A split render (src/render/render.ts) builds a NES whose mesen role carries the channel export, then settles
  // 1500 ms through renderAudio: the tap is armed while the block is wired as the mix, which is also the path a host
  // too narrow for the split takes (docs/modular-graph-phase-b0-design.md section 2.5). The core gates on the ring its
  // block drains, so that settle is an unarmed NES's settle and the sound from there on is the same, block for block.
  // Before that fix the armed core gated on the tap's rings while the block drained the mix: the mix fell one flush
  // behind every block and the settle ran the console ahead (about 5% fast), so a render started from a different
  // place. Work RAM is not compared: two Mesen cores do not power on byte-identically (uninitialised RAM), while their
  // sound for the same input is bit-identical. The two runs are one project each, in turn, since a split is wired for
  // a single system only.
  const be = createRealBackend();
  if (!be.fileExists(NES)) skip(`nes-split-render-drift: no ROM at ${NES}`);

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const audio = createAudioDriver(project.graph);
  const settle = (exportMode: "stereoModPins" | undefined): { mix: Float32Array; id: number } => {
    const id = project.systems.adopt({
      romPath: NES,
      roles: [{ kind: "nes-n8-midi", config: {} }, { kind: "mesen", config: exportMode ? { channelExportMode: exportMode } : {} }],
    });
    expect(id !== null).toBe(true);
    // BlipToaster ignores its first MIDI message, so the note is sent twice.
    for (let i = 0; i < 2; i++) audio.stageMidiIn(NOTE_ON_CH1);
    audio.renderAudio(1500); // the split render's boot settle, through the mix
    // A fresh note after the settle, so the captured mix carries signal either way: a drifted core's mix still holds
    // the previous blocks' frames, and the note reaches the host late (a core that ran ahead reaches its own later).
    for (let i = 0; i < 2; i++) audio.stageMidiIn(NOTE_ON_CH1);
    const [mix] = audio.renderAudioPerSystem(1000);
    expect(mix !== undefined).toBe(true);
    return { mix: mix!, id: id! };
  };

  const armed = settle("stereoModPins");
  // The armed core really was armed: its per-channel export offers the three pin streams. Read after the mix is
  // captured, since draining the tap discards the mix backlog the comparison below covers.
  expect(audio.renderAudioPerChannel(armed.id, 20).length).toBe(3);
  project.systems.clear();
  const plain = settle(undefined);

  let diff = -1;
  for (let i = 0; i < Math.min(armed.mix.length, plain.mix.length); i++)
    if (armed.mix[i] !== plain.mix[i]) { diff = i; break; }
  console.log(`[nes-split-render-drift] post-settle mix: ${armed.mix.length}/${plain.mix.length} samples, first difference ${diff}`);
  expect(armed.mix.length === plain.mix.length && armed.mix.length > 0).toBe(true);
  expect(diff).toBe(-1);
});

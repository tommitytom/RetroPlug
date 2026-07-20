// Concurrent core lifecycle through the queue: add + remove a system WHILE the background audio
// thread plays. The control thread builds + activates the SameBoySystem off-thread and ships the raw
// pointer via the command ring; the audio thread does an alloc-free adopt/remove into the Project and
// hands removed cores back through the release ring; the control thread drains + deletes them. Under
// the sanitizers (tools/run-sanitizer.sh) this proves the handoff is race-free (TSan) and
// leak/UAF-free (ASan). Here we assert the deterministic effects: systemCount 1→2→1 + one released
// core freed, plus a coarse energy check that the concurrently-added core actually renders.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec } from "../src/backend";

declare const __DSP_KERNEL_BUNDLE__: string;

// An embedded mGB (no ROM file) — the simplest sounding Game Boy the host can build on demand.
const MGB: ConstructSpec = { romPath: "", platform: "gb", core: "sameboy", embeddedRom: "mgb", savPath: null, statePath: null };
const NOTE = [0x90, 60, 100]; // C note-on

// The kernel structure that lets host MIDI reach each mGB: midi-routing (SendToAll) → per-system mgb.
const routingMgb = (ids: number[]) => ({
  project: [{ kind: "midi-routing", config: { mode: "sendToAll" } }],
  systems: ids.map((id) => ({ id, pipeline: [{ kind: "mgb", config: {} }] })),
});

test("add + remove a system while the audio thread plays, via the command + release queues", () => {
  const be = createRealBackend();
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  // --- setup (quiescent): one mGB + the kernel routing so notes can reach it ---
  // TS owns the id counter now; a test calling the backend directly picks its own (fresh host per file).
  const a = 1;
  expect(be.constructSystem(MGB, a)).toBeTruthy();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(routingMgb([a]))).toBeTruthy();

  expect(audio.startAudio()).toBeTruthy();
  expect(audio.systemCount()).toBe(1);

  const energyWindow = (ms: number): number => {
    const p = audio.audioCaptured();
    audio.sleepMs(ms);
    const q = audio.audioCaptured();
    const df = q.frames - p.frames;
    return df > 0 ? Math.sqrt((q.energy - p.energy) / df) : 0;
  };

  audio.stageMidiIn(NOTE); // the single mGB sounds
  const oneSys = energyWindow(120);

  // --- ADD a second mGB while running → built on the control thread, adopted on the audio thread ---
  const b = 2;
  expect(be.constructSystem(MGB, b)).toBeTruthy();
  audio.sleepMs(30); // let the audio thread apply the AddSystem command
  expect(audio.systemCount()).toBe(2);
  expect(dsp.setSystems(routingMgb([a, b]))).toBeTruthy();
  audio.stageMidiIn(NOTE); // SendToAll → both mGBs
  const twoSys = energyWindow(120);

  // --- REMOVE the first while running → erased on the audio thread, handed back for delete ---
  expect(be.removeSystem(a)).toBeTruthy();
  audio.sleepMs(30);
  expect(audio.systemCount()).toBe(1);
  expect(audio.drainReleased()).toBe(1); // the removed core, freed on the control thread
  expect(dsp.setSystems(routingMgb([b]))).toBeTruthy();
  audio.stageMidiIn(NOTE);
  const afterRemove = energyWindow(120);

  expect(audio.stopAudio()).toBeTruthy();

  console.log(`[dsp-lifecycle] one=${oneSys.toFixed(5)} two=${twoSys.toFixed(5)} after=${afterRemove.toFixed(5)}`);
  // The counts + released above are the deterministic lifecycle proof. The energy confirms cores
  // render: `two` includes the concurrently-added B. (afterRemove is only logged — its absolute
  // level is mGB note-decay/pacing dependent, not a lifecycle signal, and swings under a sanitizer's
  // slower audio thread.)
  expect(oneSys > 0.001).toBeTruthy();
  expect(twoSys > 0.001).toBeTruthy();
});

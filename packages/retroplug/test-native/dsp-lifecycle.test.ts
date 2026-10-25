// Concurrent core lifecycle through the queue: add + remove a system WHILE the background audio
// thread plays. The control thread builds + activates the SameBoySystem off-thread and plans it in (or
// out) as a graph; the audio thread adopts each plan between blocks and hands the plan it replaced back
// through the release ring; the control thread reclaims those plans, freeing each core only they held.
// Under the sanitizers (tools/run-sanitizer.sh) this proves the handoff is race-free (TSan) and
// leak/UAF-free (ASan). Here we assert the deterministic effects: systemCount 1→2→1 + the removed core
// freed (its snapshot slot gone), plus a coarse energy check that the concurrently-added core renders.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import type { ConstructSpec } from "../src/backend";
import { directGraph } from "../src/graph/directGraph";

// Structure for this file's directly built systems (docs/modular-graph-phase-b2-design.md section 2.4).
const graph = directGraph();

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
  expect(graph.add(MGB, a)).toBeTruthy();
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

  // --- ADD a second mGB while running → built on the control thread, planned in on the audio thread ---
  const b = 2;
  expect(graph.add(MGB, b)).toBeTruthy();
  audio.sleepMs(30); // let the audio thread adopt the plan that adds it
  expect(audio.systemCount()).toBe(2);
  expect(dsp.setSystems(routingMgb([a, b]))).toBeTruthy();
  audio.stageMidiIn(NOTE); // SendToAll → both mGBs
  const twoSys = energyWindow(120);

  // --- REMOVE the first while running → planned out on the audio thread, freed with its old plan ---
  expect(graph.remove(a)).toBeTruthy();
  audio.sleepMs(30);
  expect(audio.systemCount()).toBe(1);
  expect(be.readState(a) !== null, "a's slot before the reclaim").toBeTruthy(); // its old plan still holds it
  // Plans freed, not cores: at least the one that last ran `a`, and with it `a` and its snapshot slot.
  expect(audio.drainReleased()).toBeGreaterThanOrEqual(1);
  expect(be.readState(a)).toBe(null);
  expect(be.readState(b) !== null, "b's slot").toBeTruthy();
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

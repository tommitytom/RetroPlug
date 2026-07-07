// Game input reaches a live core BOTH quiescent and while the audio thread runs. pressButton used to
// mutate the core directly and bail with `if (audioRunning_) return false`, so a joypad press was dead the
// moment audio started (the live-plugin state). It now routes through the invoker — a queued
// DspCommand::PressButton drained on the audio thread — so this presses a button with the background audio
// thread free-running and asserts the call is accepted (the old path returned false there). Resource-free:
// embedded mGB, no ROM file. Ownership discipline (per dsp-threaded): construct + load the kernel BEFORE
// startAudio; never read core state while the thread runs — a button press is a WRITE over the queue, the
// intended path.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../src/appHost";

declare const __DSP_KERNEL_BUNDLE__: string;

const RIGHT = 0; // GameboyButton::Right
const START = 7; // GameboyButton::Start

test("pressButton reaches a live core quiescent AND while the audio thread runs", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  project.setOnSystemsChange(() => syncDspFromStore(project, dsp));

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  audio.renderAudio(1500); // GB boot + mGB firmware init

  // Quiescent (DirectInvoker): applied inline on the calling thread.
  expect(audio.pressButton(id, START, true)).toBeTruthy();
  expect(audio.pressButton(id, START, false)).toBeTruthy();
  // An absent system is still accepted (the store owns existence; the invoker no-ops a missing id).
  expect(audio.pressButton(id, RIGHT, true)).toBeTruthy();
  expect(audio.pressButton(id, RIGHT, false)).toBeTruthy();

  // Running (QueuedInvoker): the press crosses the command ring to the audio thread. The OLD code
  // returned false here (audioRunning_ guard) — the regression this locks in.
  expect(audio.startAudio()).toBeTruthy();
  expect(audio.pressButton(id, START, true)).toBeTruthy();
  audio.sleepMs(20); // let the audio thread drain the PressButton command
  expect(audio.pressButton(id, START, false)).toBeTruthy();
  audio.sleepMs(20);
  expect(audio.stopAudio()).toBeTruthy();

  audio.drainReleased(); // no cores were released, but keep the teardown discipline
});

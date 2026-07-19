// getExpansionAudioState against a REAL Mesen NES core, driven through the CLI session + Timeline.
// The expansion-audio registers are write-only (like the 2A03's), so this snapshot is how a test
// observes what a MIDI-driven ROM programmed into the mapper sound chip. Proves the end-to-end RPC +
// per-chip decode: the base ROM (mapper 0) reports "none"; the VRC6 ROM reports its 3 voices, and a
// sounding pulse reads back enabled with a non-zero normalized volume.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { type ExpansionAudioState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/n8-midi-vrc6.nes";

test("getExpansionAudioState reports 'none' for a cart without expansion audio", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  let st: ExpansionAudioState | null = null;
  const tl = new Timeline().at(400, (sess) => (st = sess.backend.getExpansionAudioState(id)));
  renderTimeline(s, tl, { durationMs: 600, warmupMs: 1000 });

  expect(st != null).toBeTruthy();
  expect(st!.chip === "none").toBeTruthy();
  expect(Array.isArray(st!.channels) && st!.channels.length === 0).toBeTruthy();
});

test("getExpansionAudioState decodes VRC6: 3 voices, a sounding pulse reads enabled + volume>0", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) {
    console.log("# SKIP: no VRC6 rom");
    return;
  }
  const id = s.project.systems.addSystem(VRC6);
  if (id == null) throw new Error("addSystem failed");

  // Play a note on MIDI ch6 = VRC6 pulse1 (channel 0) and snapshot mid-note.
  let st: ExpansionAudioState | null = null;
  const tl = new Timeline()
    .note(200, 60, { channel: 6, velocity: 127, durationMs: 400 })
    .at(400, (sess) => (st = sess.backend.getExpansionAudioState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });

  expect(st != null).toBeTruthy();
  expect(st!.chip === "vrc6").toBeTruthy();
  // VRC6 = pulse1, pulse2, saw.
  expect(st!.channels.length === 3).toBeTruthy();

  const p1 = st!.channels[0];
  // The pulse1 note is sounding: enabled, a non-zero normalized volume, and a real timer period.
  expect(p1.enabled).toBeTruthy();
  expect(p1.volume > 0).toBeTruthy();
  expect(p1.period > 0).toBeTruthy();

  // Field shapes are sane across every channel.
  for (const c of st!.channels) {
    expect(typeof c.enabled === "boolean").toBeTruthy();
    expect(typeof c.constantOutput === "boolean").toBeTruthy();
    expect(c.volume >= 0 && c.volume <= 15).toBeTruthy();
    expect(typeof c.outputLevel === "number").toBeTruthy();
    expect(typeof c.instrument === "number").toBeTruthy();
  }
});

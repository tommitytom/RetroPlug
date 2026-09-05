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
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";
const VRC6 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-vrc6.nes";

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
    // waveLength/activeChannels are N163-only cross-check terms; 0 for VRC6.
    expect(c.waveLength === 0).toBeTruthy();
    expect(c.activeChannels === 0).toBeTruthy();
  }
});

// Signed cents of a measured pitch vs an expected equal-tempered frequency (no octave folding).
function cents(measuredHz: number, expectedHz: number): number {
  return 1200 * Math.log2(measuredHz / expectedHz);
}

test("getExpansionAudioState decodes VRC6 pitch in Hz: A4 in tune + octave-doubling", () => {
  const s = bootSession();
  if (!s.backend.fileExists(VRC6)) {
    console.log("# SKIP: no VRC6 rom");
    return;
  }

  // Snapshot VRC6 pulse1 (MIDI ch6) mid-note and read the decoded output pitch.
  const freqOf = (note: number): number => {
    const id = s.project.systems.addSystem(VRC6);
    if (id == null) throw new Error("addSystem failed");
    let st: ExpansionAudioState | null = null;
    const tl = new Timeline()
      .note(200, note, { channel: 6, velocity: 127, durationMs: 400 })
      .at(400, (sess) => (st = sess.backend.getExpansionAudioState(id)));
    renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });
    s.project.systems.removeSystem(id);
    return st!.channels[0].frequency;
  };

  // A4 (MIDI note 69) must decode to ~440 Hz. This is the exact blind spot that let the
  // N163/VRC7 detune ship: RMS/octave-ratio checks never asserted absolute Hz. The VRC6
  // pulse formula clk/(16*((freq>>shift)+1)) lands note 69 at 440.40 Hz (period 253).
  const a4 = freqOf(69);
  expect(Math.abs(cents(a4, 440)) < 10).toBeTruthy();

  // Octave-doubling: note+12 reads ~2x. Immune to any constant tuning offset, so it
  // independently proves `frequency` tracks pitch by the correct register->Hz ratio.
  const a3 = freqOf(57);
  const a5 = freqOf(81);
  expect(Math.abs(a4 / a3 - 2) < 0.02).toBeTruthy();
  expect(Math.abs(a5 / a4 - 2) < 0.02).toBeTruthy();
});

// The N163 read is the PROGRAMMED register file (Namco163Audio::GetState reads _internalRam), so two reads
// of one held note must agree on every pitch term - period (18-bit freq reg), waveLength, waveAddress and
// activeChannels - and only outputLevel may differ (it is the live multiplexed sample). BlipToaster's notes
// reported the opposite (period 61007/56547, waveLength 36/184 for one held note); this pins the contract.
// BlipToaster programs every voice's wave length as 32 samples (n163.c writes 0xE0 to reg +4).
const N163 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-n163.nes";

test("getExpansionAudioState decodes N163 from the programmed registers: two reads of a held note agree", () => {
  const s = bootSession();
  if (!s.backend.fileExists(N163)) {
    console.log("# SKIP: no N163 rom");
    return;
  }
  const id = s.project.systems.addSystem(N163);
  if (id == null) throw new Error("addSystem failed");

  // MIDI ch6 = the ROM's N163 wave 0. Two snapshots 60 ms apart, both mid-note.
  let a: ExpansionAudioState | null = null, b: ExpansionAudioState | null = null;
  const tl = new Timeline()
    .noteOn(200, 57, { channel: 6, velocity: 127 })
    .at(400, (sess) => (a = sess.backend.getExpansionAudioState(id)))
    .at(460, (sess) => (b = sess.backend.getExpansionAudioState(id)))
    .noteOff(700, 57, { channel: 6 });
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1100 });
  s.project.systems.removeSystem(id);

  expect(a!.chip).toBe("n163");
  expect(a!.channels.length, "N163 reports all 8 hardware voices").toBe(8);
  const sounding = a!.channels.filter((c) => c.enabled && c.volume > 0);
  expect(sounding.length, "voices sounding for one held note").toBe(1);
  const v = sounding[0];
  expect(v.period, "18-bit frequency register").toBeGreaterThan(0);
  expect(v.frequency, "decoded Hz").toBeGreaterThan(0);
  expect(v.waveLength, "the ROM programs 32-sample waves").toBe(32);
  expect(v.activeChannels).toBeGreaterThanOrEqual(1);
  // The programmed registers are stable across reads; only the live sample level may move.
  for (let i = 0; i < 8; i++) {
    const x = a!.channels[i], y = b!.channels[i];
    expect(y.period, `voice ${i} period`).toBe(x.period);
    expect(y.waveLength, `voice ${i} waveLength`).toBe(x.waveLength);
    expect(y.waveAddress, `voice ${i} waveAddress`).toBe(x.waveAddress);
    expect(y.activeChannels, `voice ${i} activeChannels`).toBe(x.activeChannels);
    expect(y.volume, `voice ${i} volume`).toBe(x.volume);
    expect(y.enabled, `voice ${i} enabled`).toBe(x.enabled);
    expect(y.frequency, `voice ${i} frequency`).toBe(x.frequency);
  }
});

// MMC5 used to report NO channels at all; now [pulse1, pulse2, pcm]. MIDI ch6 = MMC5 pulse 1 in the ROM.
const MMC5 = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster-mmc5.nes";

test("getExpansionAudioState decodes MMC5: pulse1/pulse2/pcm, a sounding pulse reads enabled + volume + pitch", () => {
  const s = bootSession();
  if (!s.backend.fileExists(MMC5)) {
    console.log("# SKIP: no MMC5 rom");
    return;
  }
  const id = s.project.systems.addSystem(MMC5);
  if (id == null) throw new Error("addSystem failed");

  let st: ExpansionAudioState | null = null, after: ExpansionAudioState | null = null;
  const tl = new Timeline()
    .noteOn(200, 69, { channel: 6, velocity: 127 })
    .at(400, (sess) => (st = sess.backend.getExpansionAudioState(id)))
    .noteOff(500, 69, { channel: 6 })
    .at(800, (sess) => (after = sess.backend.getExpansionAudioState(id)));
  renderTimeline(s, tl, { durationMs: 900, warmupMs: 1100 });
  s.project.systems.removeSystem(id);

  expect(st!.chip).toBe("mmc5");
  expect(st!.channels.length, "pulse1, pulse2, pcm").toBe(3);
  const p1 = st!.channels[0];
  expect(p1.enabled, "pulse1 enabled").toBeTruthy();
  expect(p1.volume, "pulse1 envelope output").toBeGreaterThan(0);
  expect(p1.period, "pulse1 11-bit period").toBeGreaterThan(0);
  // A4 on an NTSC clock: the same timer formula as the 2A03, so ~440 Hz give or take the table's rounding.
  expect(p1.frequency, "pulse1 Hz").toBeCloseTo(440, 6);
  expect(p1.duty).toBeLessThanOrEqual(3);
  // The other pulse is idle, and the PCM channel carries the DAC level (nothing playing = 0).
  expect(st!.channels[1].volume, "pulse2 idle").toBe(0);
  expect(st!.channels[2].outputLevel, "pcm idle").toBe(0);
  // After note-off the pulse is silent again.
  expect(after!.channels[0].volume, "pulse1 after note-off").toBe(0);
});

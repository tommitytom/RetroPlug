// Does the controller layer's launch encoding actually move a REAL LSDj cart?
//
// The pure-TS side already proves a pad press comes out of the kernel as a row byte on a system's link
// port (test/dsp/controller-role.test.ts), and that every row survives a round trip through `midiMapRow`
// (test/controller/trackerTarget.test.ts). But both of those check our encoder against our own decoder.
// This checks it against LSDj, which is the only opinion that counts - and in particular checks the
// two-channel split, where rows 128-255 ride MIDI channel 2 and a wrong nibble would silently launch a
// row 128 away.
//
// It also checks the claim the LEDs rest on: after a launch, the cart is where the app's model says.
//
// Run: pnpm test:native lsdj-launchpad
import { test, expect } from "../testing/harness";
import { type SavInput } from "../src/lsdjSav";
import { launchMessage } from "../src/controller";
import { PredictedLsdjModel } from "../src/lsdj/playback";
import { SongSchema } from "../src/lsdj/model";
import { LsdjProbe, fmtSample } from "./lsdjPlaybackProbe";
import { projectKernelStructure, type ControllerProjection } from "../src/kernelProjection";
import type { SystemView } from "../src/systemsStore";

// 130 rows, every one a single-phrase chain, so every row is playable and 96 ticks long. Reaching past
// row 127 is the point: that is where the wire protocol switches to MIDI channel 2. All rows share one
// chain because LSDj only has 128 of them and nothing here reads chain identity - only position.
//
// Rows must be CONTIGUOUS: an empty row is the end of the song (B9), so a sparse song could never reach
// the high rows at all, and a launch aimed there would scan back to the last playable one.
const ROWS = 130;
const songJson = {
  formatVersion: 22,
  settings: { syncMode: "MidiMap" as const, tempo: 128 },
  rows: Array.from({ length: ROWS }, () => ({ chains: [0, 0, 0, 0] })),
  chains: [{ phrases: [0] }],
  phrases: [{ notes: Array.from({ length: 16 }, () => 1), instruments: Array.from({ length: 16 }, () => 0) }],
  instruments: [{ type: "pulse" as const, panning: "LeftRight" as const, adsr: { initialLevel: 8, attackSpeed: 8 } }],
};
const SONG: SavInput = { workingSong: songJson };

test("the controller layer's launch bytes move a real cart, on both MIDI channels", () => {
  const p = LsdjProbe.create({ song: SONG, mode: "midiMap" });
  if (!p) return console.log("# SKIP lsdj-launchpad: aboy ROM not found / unsupported version");

  // The app's own model of the same song, driven by the same launches. If the two disagree, the LEDs are
  // describing a cart that is somewhere else.
  const model = new PredictedLsdjModel(SongSchema.parse(songJson));

  // Rows either side of the 128 boundary. 5 and 100 ride channel 1; 128 and 129 ride channel 2, which is
  // the encoding no test outside this file can confirm.
  for (const row of [5, 100, 128, 129]) {
    const msg = launchMessage(row)!;
    p.stage(msg);
    const after = p.render(400); // well under the 2 s a row lasts, so it cannot have advanced off it yet
    model.launch(row);

    const channel = (msg[0] & 0x0f) + 1;
    console.log(`[lsdj-launchpad] row ${row} via ch${channel} (${msg.map((b) => b.toString(16)).join(" ")}): ${fmtSample(after)}`);
    expect(after.channels.pu1.songRow).toBe(row);
    expect(after.channels.pu1.songRow).toBe(model.position().channels[0].songRow);
  }
});

// NOT COVERED HERE: what the cart does once the host transport starts clocking it. That is already
// lsdj-midimap.test.ts ("the midiMap role clocks the cart"), and an attempt to repeat it in this file
// behaved oddly in a way that turned out to be about the harness rather than about the launch path - a
// second LsdjProbe in the same process saw the row jump once at transport-start and then freeze, while
// the steps kept advancing. Worth understanding before anyone builds on it, but it is a probe-lifecycle
// question, not a controller one, so it is recorded rather than papered over with a loose assertion.

// --- the pipeline a real project actually runs -----------------------------------------------------
//
// Everything above hand-writes `mode: "midiMap"`. A real project does not: `lsdj-sync` defaults to
// `midiSync`, and nothing made it agree with the controller - so enabling a Launchpad on a fresh cart
// clocked it for a mode it was not in, sent launches nowhere, and left LSDj sitting on "WAIT". Reported
// from a hardware session. These drive the cart through projectKernelStructure, so what is under test is
// the pipeline a project would really produce.

/** A SystemView carrying the roles a freshly built LSDj cart has, with the STORED (default) sync mode. */
function lsdjView(id: number, mode = "midiSync"): SystemView {
  return {
    id, platform: "gb", core: "sameboy", romPath: "", savPath: "", savSuffix: 0,
    embedded: false, battery: true, focused: true, missing: false,
    settings: { gainDb: 0, reloadOnRomChange: false },
    roles: [{ kind: "sameboy", config: {} }, { kind: "lsdj-sync", config: { mode, tempoDivisor: 1 } }],
  };
}

const projection = (over: Partial<ControllerProjection> = {}): ControllerProjection => ({
  enabled: true, app: "lsdj-midimap", target: "system", systemId: 0, appConfig: {},
  songRowTicks: [], anchor: null, cartSync: "MidiMap", ...over,
});

test("a project whose cart is in the DEFAULT midiSync still launches rows once a controller is on", () => {
  const p = LsdjProbe.create({
    song: SONG,
    structure: (id) => projectKernelStructure([lsdjView(id)], "sendToAll", projection()),
  });
  if (!p) return console.log("# SKIP lsdj-launchpad: aboy ROM not found / unsupported version");

  p.stage(launchMessage(42)!);
  const after = p.render(400);
  console.log(`[lsdj-launchpad] default-mode project, row 42: ${fmtSample(after)}`);
  expect(after.channels.pu1.songRow).toBe(42);
});

test("without a controller the same project ignores the same launch, which is what was wrong", () => {
  // The control: it is the projection's override that makes the row land, not something else about the
  // pipeline. A cart in midiSync reads a NoteOn as nothing at all.
  const p = LsdjProbe.create({
    song: SONG,
    structure: (id) => projectKernelStructure([lsdjView(id)], "sendToAll"),
  });
  if (!p) return;

  p.stage(launchMessage(42)!);
  const after = p.render(400);
  console.log(`[lsdj-launchpad] no controller, row 42: ${fmtSample(after)}`);
  expect(after.channels.pu1.songRow !== 42).toBe(true);
});

test("a cart whose OWN SYNC is not MI.MAP is left alone rather than clocked at", () => {
  // A cart in LSDJ (master) mode drives the link itself, so our bytes collide with its own and LSDj
  // reports TOO BUSY. The projection sends it nothing; the launch therefore does nothing, which is the
  // correct outcome for a cart that is not listening.
  const p = LsdjProbe.create({
    song: SONG,
    structure: (id) => projectKernelStructure([lsdjView(id)], "sendToAll", projection({ cartSync: "Lsdj" })),
  });
  if (!p) return;

  p.stage(launchMessage(42)!);
  const after = p.render(400);
  console.log(`[lsdj-launchpad] cart SYNC=LSDJ, row 42: ${fmtSample(after)}`);
  expect(after.channels.pu1.songRow !== 42).toBe(true);
});

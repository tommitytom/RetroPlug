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

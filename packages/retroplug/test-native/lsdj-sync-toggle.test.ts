// What happens when the player changes SYNC on LSDj's PROJECT screen while a clock is being fed?
//
// Reported from a hardware session driving a real Launchpad: cycling through the SYNC options with a MIDI
// clock running makes LSDj print "TOO BUSY!" and land in a state where it will not sync properly again.
// This measures it. Like lsdj-playback-probe, it is an INSTRUMENT first - it prints what it observes - and
// the assertions only pin what it has established.
//
// Changing the setting has to go through LSDj's own UI: switching SYNC is what reconfigures the link
// hardware, and poking the byte in SRAM behind LSDj's back runs none of that code.

import { test, expect } from "../testing/harness";
import { LsdjProbe, ABOY_ROM, TICK_MS } from "./lsdjPlaybackProbe";
import type { SavInput, SongSettings } from "../src/lsdj/model";
import { createRealBackend } from "../src/realBackend";

const B = { Right: 0, Left: 1, Up: 2, Down: 3, A: 4, B: 5, Select: 6, Start: 7 } as const;

/** SYNC byte values (model.ts SYNC_TO_BYTE). */
const SYNC_MIDIMAP = 8;

/** Put the cursor on the PROJECT screen's SYNC field, leaving the setting as it was found.
 *
 *  Located by watching the setting BYTE rather than by counting presses down the screen: the field order
 *  differs between LSDj versions, and a stale count would silently leave a test editing tempo and
 *  concluding that changing SYNC is harmless. Each miss is undone with Left, so the walk itself does not
 *  reconfigure the cart. */
function focusSyncField(p: LsdjProbe): boolean {
  // This exact gesture sequence is EMPIRICAL, found by search on lsdj9_3_3-arduinoboy rather than derived
  // from any understanding of LSDj's PROJECT screen, and the search was worth recording because the
  // obvious routes all fail: N bare Downs then Right does nothing from any N, and so does N Downs then
  // A+Right. Only the interleaved form below lands on SYNC, so the A+Right is evidently doing something
  // to the cursor rather than editing a value. It leaves a couple of other PROJECT fields nudged, none of
  // which affect row timing under MI.MAP (the host owns the clock, so tempo is inert).
  //
  // The return value is the real guard: it asserts the byte MOVED, so if a future ROM changes the layout
  // this reports a failure to find the field rather than silently testing nothing.
  for (let row = 0; row < 4; row++) {
    p.press(B.Right);
    p.chord(B.A, B.Right);
    p.press(B.Down);
  }
  const start = p.syncMode();
  p.press(B.Right);
  const moved = p.syncMode() !== start;
  if (moved) p.press(B.Left); // put it back; the caller decides what to cycle through
  return moved;
}

/** A three-row song with a distinct chain per row, one phrase each: 96 ticks a row, so a row change is
 *  unambiguous. Note the `workingSong` wrapper - a SavInput is a whole battery, and a bare song here
 *  silently prefaults to an EMPTY one (which is a cart with nothing to play, and a very confusing
 *  afternoon). */
const song = (syncMode: SongSettings["syncMode"] = "MidiMap"): SavInput => ({
  workingSong: {
    formatVersion: 22,
    settings: { syncMode, tempo: 128 },
    rows: [{ chains: [0, 0, 0, 0] }, { chains: [1, 1, 1, 1] }, { chains: [2, 2, 2, 2] }],
    chains: [{ phrases: [0] }, { phrases: [1] }, { phrases: [2] }],
    phrases: [phrase(1), phrase(2), phrase(3)],
    instruments: [{ type: "pulse" }],
  },
});

/** A phrase that sounds on every step, so "is it playing" is audible as well as readable. */
const phrase = (note: number) => ({
  notes: Array.from({ length: 16 }, () => 48 + note),
  instruments: Array.from({ length: 16 }, () => 0),
});

const have = () => createRealBackend().fileExists(ABOY_ROM);

/** Launch a row and clock the cart; return the distinct song rows it visited. A cart that is syncing
 *  properly walks 1,2,0,1,... - one row per 96 ticks. A broken one sits still, or never starts. */
function rowsUnderClock(p: LsdjProbe, launchRow = 1, ticks = 260): (number | null)[] {
  p.launchRaw(launchRow);
  const seen: (number | null)[] = [];
  for (const s of p.runTicks(ticks)) {
    const r = s.channels.pu1.songRow;
    if (seen.length === 0 || seen[seen.length - 1] !== r) seen.push(r);
  }
  return seen;
}

test("MAP: where the SYNC field lives on the PROJECT screen, and what its values are", () => {
  if (!have()) return;
  const p = LsdjProbe.create({ song: song("None") });
  if (!p) return;

  expect(p.gotoScreen("project")).toBe(true);
  const before = p.syncMode();
  console.log(`  PROJECT screen reached; SYNC byte = ${before}`);

  // Walk the cursor over the screen, trying each edit gesture at every stop, and watch the SYNC byte.
  // Self-locating on purpose: no hardcoded field order to go stale across LSDj versions.
  let found: { col: number; row: number; gesture: string } | null = null;
  for (let col = 0; col < 3 && !found; col++) {
    for (let row = 0; row < 10 && !found; row++) {
      const at = p.state()?.cursor;
      for (const [name, edit] of [["Right", () => p.press(B.Right)], ["A+Right", () => p.chord(B.A, B.Right)]] as const) {
        edit();
        const now = p.syncMode();
        if (now !== before) {
          found = { col, row, gesture: name };
          console.log(`  SYNC moved at cursor ${JSON.stringify(at)} via ${name}: ${before} -> ${now}`);
          break;
        }
      }
      if (!found) {
        console.log(`  cursor ${JSON.stringify(at)}: sync still ${p.syncMode()}`);
        p.press(B.Down);
      }
    }
    if (!found) {
      for (let r = 0; r < 10; r++) p.press(B.Up, 25, 25);
      p.press(B.Right);
    }
  }
  console.log(`  sync field = ${JSON.stringify(found)}`);
  expect(found !== null).toBe(true);
});

test("MEASURED: SYNC can be changed while idle or merely clocked, but NOT while the cart is playing", () => {
  if (!have()) return;
  // Three fresh carts rather than one walked through three states, because MI.MAP has no stop (B5/B8):
  // once a cart is playing there is no clean way back to "idle" to test the next condition.
  const result: Record<string, boolean> = {};
  for (const [label, clocked, playing] of [
    ["idle", false, false],
    ["clocked", true, false],
    ["playing", true, true],
  ] as const) {
    const p = LsdjProbe.create({ song: song("MidiMap") });
    if (!p) return;
    if (playing) rowsUnderClock(p, 1, 40);
    if (clocked) p.autoClock(TICK_MS);
    const reached = p.gotoScreen("project");
    result[label] = reached ? focusSyncField(p) : false;
    console.log(`  ${label}: syncEditable=${result[label]} screen=${p.sample().screen} playing=${p.sample().playing}`);
  }
  // The clock alone is harmless - a cart being fed bytes still takes the edit.
  expect(result.idle).toBe(true);
  expect(result.clocked).toBe(true);
  // PLAYING is what blocks it. This is the mechanism behind the report: the player reaches for SYNC while
  // the Launchpad has the cart running, the presses do not take, and they are left on whichever option the
  // first press happened to land on.
  expect(result.playing).toBe(false);
});

test("MEASURED: a cart knocked off MI.MAP mid-flight stops honouring launches, and cannot be put back", () => {
  if (!have()) return;
  const p = LsdjProbe.create({ song: song("MidiMap") });
  if (!p) return;

  expect(p.syncMode()).toBe(SYNC_MIDIMAP);
  const before = rowsUnderClock(p, 1);
  console.log(`  baseline: launched row 1, walked ${JSON.stringify(before)}`);
  expect(before[0]).toBe(1); // the launch landed
  expect(before.length >= 3).toBe(true); // and it is walking

  // Now do what the player did: leave the clock running and go for the SYNC options. autoClock keeps a
  // byte going in underneath every button press and every rendered frame, which is the point - the same
  // walk with the clock stopped is a different experiment.
  p.autoClock(TICK_MS);
  expect(p.gotoScreen("project")).toBe(true);
  focusSyncField(p); // returns false while playing (above); the first press still LANDS
  const visited: (number | null)[] = [p.syncMode()];
  for (let i = 0; i < 6; i++) {
    p.press(B.Left);
    visited.push(p.syncMode());
  }
  console.log(`  SYNC values visited under clock: ${JSON.stringify(visited)}`);

  // Try to get back to MI.MAP. This is the part that fails.
  for (let i = 0; i < 12 && p.syncMode() !== SYNC_MIDIMAP; i++) p.press(B.Right);
  const ended = p.syncMode();
  console.log(`  after trying to return: SYNC = ${ended}`);
  expect(ended !== SYNC_MIDIMAP).toBe(true);

  p.autoClock(null);
  expect(p.gotoScreen("song")).toBe(true);
  const after = rowsUnderClock(p, 1);
  console.log(`  after the toggle: launched row 1, walked ${JSON.stringify(after)}`);
  // The break, stated as the player experiences it: the cart is still making noise and still stepping,
  // so nothing LOOKS wrong - but it is no longer listening to the Launchpad. Pressing a pad does nothing.
  expect(after[0] !== 1).toBe(true);
});

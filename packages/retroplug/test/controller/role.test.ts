// The `launchpad` DSP role, driven through a real kernel - specifically, WHEN it takes the device.
//
// This is the half M5 left open. The session was built on the first block after a structure push and its
// connect() messages were never sent at all, so Programmer mode was never entered. Sending them once at
// build time would not have fixed it either: the real sequence is "start RetroPlug, THEN plug the Launchpad
// in and hit Connect", by which point the session is thousands of blocks old. And re-pushing the structure
// cannot serve as the trigger, because project stage state deliberately survives setSystems.
//
// So the block carries `controllerConnected` and the role acts on its rising edge. These tests pin that
// edge, and pin the repaint that has to come with it: entering Programmer mode BLANKS the device, so a
// reconnect that merely diffed against the old shadow buffer would leave the grid dark.
import { test, expect } from "../../testing/harness";
import { RoleRegistry } from "../../src/systemRoles";
import { DspKernel, type BlockInput } from "../../src/dspKernel";
import { registerControllerRole } from "../../src/controllerRole";
import { PRO_MK3, Palette, enterProgrammerMode } from "../../src/launchpad";

/** The two palette indices the app paints song cells with (lsdjMidiMap CONTENT / PLAYHEAD). */
const DIM = Palette.greenDim;
const BRIGHT = Palette.greenBright;

const HELLO = enterProgrammerMode(PRO_MK3);

/** A press of the top-left pad: song row 0, channel pu1. */
const PRESS_ROW_0 = { frame: 0, data: [0x90, 0x51, 0x7f] };

/** A 4 x 256 tick table with content on rows 0 and 1 only - enough that the app paints something, so a
 *  missing repaint is visible as an absence of LED traffic. */
function rowTicks(): (number | null)[][] {
  return [0, 1, 2, 3].map(() => Array.from({ length: 256 }, (_, r) => (r < 2 ? 96 : null)));
}

const baseDyn = (over: Partial<BlockInput> = {}): BlockInput => ({
  frames: 512, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false,
  midiIn: [], buttons: [], keys: [], serialOut: [], ...over,
});

function kernel(config: Record<string, unknown> = {}): DspKernel {
  const reg = new RoleRegistry();
  registerControllerRole(reg);
  const k = new DspKernel(reg);
  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), ...config });
  return k;
}

/** Re-push the structure, the way a settings edit or a song-table refresh does. */
function push(k: DspKernel, config: Record<string, unknown>): void {
  k.setSystems({
    project: [{ kind: "launchpad", config }],
    systems: [{ id: 1, pipeline: [] }],
  });
}

/** Every message the role emitted for one block. */
function run(k: DspKernel, over: Partial<BlockInput> = {}): number[][] {
  return k.processBlock(baseDyn(over)).controllerOut.map((m) => [...m]);
}

test("with no device attached the role never enters Programmer mode", () => {
  const k = kernel();
  const first = run(k);
  const second = run(k);
  // It still paints (the surface diff runs, so the model and the shadow buffer stay honest), but the mode
  // message - the one that locks the device's front panel - is not among them.
  expect(first.some((m) => sameBytes(m, HELLO))).toBe(false);
  expect(second.some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("a device appearing mid-session is taken on that block, hello first", () => {
  const k = kernel();
  run(k); // many blocks pass with nothing plugged in
  run(k);

  const out = run(k, { controllerConnected: true });
  expect(out.length > 0).toBe(true);
  // FIRST, not merely present: LED bytes sent before the mode switch would be interpreted in Live mode.
  expect(sameBytes(out[0], HELLO)).toBe(true);
});

test("staying connected does not re-send hello every block", () => {
  const k = kernel();
  run(k, { controllerConnected: true });
  const steady = run(k, { controllerConnected: true });
  expect(steady.some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("a reconnect repaints the whole surface, not a diff against a device that was blanked", () => {
  const k = kernel();
  const taken = run(k, { controllerConnected: true });
  const painted = taken.filter((m) => !sameBytes(m, HELLO));
  expect(painted.length > 0).toBe(true);

  run(k, { controllerConnected: true }); // steady state: nothing changes, so nothing is sent
  expect(run(k, { controllerConnected: true }).length).toBe(0);

  run(k, { controllerConnected: false }); // unplugged / disconnected from the menu
  const again = run(k, { controllerConnected: true });
  expect(sameBytes(again[0], HELLO)).toBe(true);
  // The repaint has to be as big as the first one: the device forgot every LED when it re-entered
  // Programmer mode, so a diff-sized update would leave most of the grid dark.
  expect(again.length - 1).toBe(painted.length);
});

test("the predictor keeps running while nothing is attached", () => {
  // Otherwise the first frame after plugging in would show the song where it was when the app started.
  const k = kernel();
  run(k, { transport: true, ppqStart: 0 });
  const out = run(k, { transport: true, ppqStart: 8, controllerConnected: true }); // 8 quarters = 192 ticks
  expect(sameBytes(out[0], HELLO)).toBe(true);
  // Two rows of content at 96 ticks each: 192 ticks in, the playhead has wrapped back to row 0 - and the
  // paint that follows hello proves the model was advancing all along rather than sitting at tick 0.
  expect(out.length > 1).toBe(true);
});

// --- reacting to a re-push -------------------------------------------------------------------------
//
// The session used to be built once and kept forever, which made every later change invisible: a chain
// added to a song row inside LSDj left the grid showing the song as it was, and a menu tweak did nothing
// at all. Toggling "Use in Project" off and on was the only way through, because that drops the stage and
// takes its state with it. Reported from a hardware session, so these are the regression.

// Grid pad (x, y) -> device index. The device counts rows from the BOTTOM, so y=0 (song row 0, first pane)
// is index 81 and y=4 is 41.
const padIndex = (x: number, y: number) => (7 - y) * 10 + x + 11;

/** A running model of what the device is showing, fed block by block - the surface only sends CHANGES, so
 *  a single block's messages are a diff and say nothing about the pads it left alone. */
class Device {
  readonly leds = new Map<number, number>();
  feed(messages: number[][]): void {
    for (const m of messages) {
      if (m[0] === 0xf0) {
        // Bulk LED SysEx: the 8-byte frame around <type, index, colour> triples (the app is palette-only).
        for (let i = 7; i + 2 < m.length - 1; i += 3) this.leds.set(m[i + 1], m[i + 2]);
      } else if ((m[0] & 0xf0) === 0x90 || (m[0] & 0xf0) === 0xb0) {
        this.leds.set(m[1], m[2]);
      }
    }
  }
  lit(x: number, y: number): boolean {
    const c = this.leds.get(padIndex(x, y));
    return c !== undefined && c !== 0;
  }
}

/** The same song with a chain added to row 4 - four more cells of content, as an edit inside LSDj makes. */
function editedRowTicks(): (number | null)[][] {
  const t = rowTicks();
  for (const ch of t) ch[4] = 96;
  return t;
}

test("a song edited on the cart lights up without a toggle", () => {
  const k = kernel();
  const dev = new Device();
  dev.feed(run(k, { controllerConnected: true }));
  expect(run(k, { controllerConnected: true }).length).toBe(0); // settled
  expect(dev.lit(0, 4)).toBe(false); // row 4 is empty in the song we started with

  push(k, { app: "lsdj-midimap", songRowTicks: editedRowTicks() });
  const out = run(k, { controllerConnected: true });
  dev.feed(out);

  expect(dev.lit(0, 4)).toBe(true); // the new chain is on the grid, with no toggle in between
  // No hello: the device never went away, so this is a repaint rather than a re-take.
  expect(out.some((m) => sameBytes(m, HELLO))).toBe(false);
});

test("adopting a new song table does not restart the song", () => {
  // The whole reason the table is swapped in place rather than the model rebuilt: a player editing row 4
  // while row 1 plays is not asking for the song to jump back to the top.
  const k = kernel();
  const dev = new Device();
  dev.feed(run(k, { transport: true, ppqStart: 0, controllerConnected: true, controllerIn: [PRESS_ROW_0] }));
  dev.feed(run(k, { transport: true, ppqStart: 4, controllerConnected: true })); // 96 ticks: row 0 -> row 1
  const playheadBefore = dev.leds.get(padIndex(0, 1));
  expect(playheadBefore !== undefined && playheadBefore !== 0).toBe(true);

  push(k, { app: "lsdj-midimap", songRowTicks: editedRowTicks() });
  dev.feed(run(k, { transport: true, ppqStart: 4, controllerConnected: true }));

  expect(dev.leds.get(padIndex(0, 1))).toBe(playheadBefore); // still on row 1, same brightness
  expect(dev.lit(0, 4)).toBe(true); // and the edit did arrive
});

test("an app knob changes behaviour on the next block, not on the next toggle", () => {
  // `bar` vs `immediate`, distinguished only once something is playing - with nothing playing the app
  // launches at once whatever the setting, so that is not a test of the knob.
  expect(secondLaunchFires({})).toBe(false); // bar (the default): the second press waits for the bar line
  expect(secondLaunchFires({ quantise: "immediate" })).toBe(true);

  function secondLaunchFires(appConfig: Record<string, unknown>): boolean {
    const k = kernel();
    // target midiOut so a launch lands in a sink this test can see; toSystem would put it in a system
    // inbox that an empty pipeline never drains.
    push(k, { app: "lsdj-midimap", target: "midiOut", songRowTicks: rowTicks(), appConfig });
    const on = { transport: true, controllerConnected: true } as const;
    k.processBlock(baseDyn({ ...on, ppqStart: 0, controllerIn: [PRESS_ROW_0] })); // starts it playing
    k.processBlock(baseDyn({ ...on, ppqStart: 1 }));
    // Now something IS playing, and tick 26 is nowhere near a bar line (96).
    const sinks = k.processBlock(baseDyn({ ...on, ppqStart: 1.1, controllerIn: [PRESS_ROW_0] }));
    return sinks.midiOut.length > 0;
  }
});

test("a rebuilt session re-takes the device rather than diffing against a surface it never painted", () => {
  const k = kernel();
  const first = run(k, { controllerConnected: true });
  expect(sameBytes(first[0], HELLO)).toBe(true);

  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), appConfig: { follow: false } });
  const out = run(k, { controllerConnected: true });
  expect(sameBytes(out[0], HELLO)).toBe(true);
  expect(out.length).toBe(first.length); // a full repaint, as on any fresh surface
});

function sameBytes(a: readonly number[] | undefined, b: readonly number[]): boolean {
  return !!a && a.length === b.length && a.every((v, i) => v === b[i]);
}

// --- re-anchoring ----------------------------------------------------------------------------------
//
// Pressing START on LSDj's own song screen starts the cart at whatever row ITS cursor is on. The predictor
// has no way to see that, so the lit playhead carried on pointing at the row it thought was playing until
// the next pad press put both back in step. Reported from a hardware session.

test("an anchor moves the playhead to where the cart actually started", () => {
  const k = kernel();
  const dev = new Device();
  dev.feed(run(k, { transport: true, ppqStart: 0, controllerConnected: true, controllerIn: [PRESS_ROW_0] }));
  expect(dev.lit(0, 0)).toBe(true); // predicted: row 0

  // The player hits START in LSDj with its cursor on row 1.
  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), anchorRows: [1, 1, 1, 1], anchorSeq: 1 });
  dev.feed(run(k, { transport: true, ppqStart: 0.1, controllerConnected: true }));

  expect(dev.leds.get(padIndex(0, 1))).toBe(BRIGHT);
  expect(dev.leds.get(padIndex(0, 0))).toBe(DIM); // the old playhead went back to plain content
});

test("the same anchor is applied once, not on every later push", () => {
  const k = kernel();
  const dev = new Device();
  dev.feed(run(k, { transport: true, ppqStart: 0, controllerConnected: true, controllerIn: [PRESS_ROW_0] }));
  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), anchorRows: [1, 1, 1, 1], anchorSeq: 1 });
  dev.feed(run(k, { transport: true, ppqStart: 0.1, controllerConnected: true }));
  expect(dev.leds.get(padIndex(0, 1))).toBe(BRIGHT);

  // The playhead advances a row on its own, then an UNRELATED structure push arrives carrying the same
  // anchor. Re-applying it would drag the playhead back to row 1 and lose the player's place.
  dev.feed(run(k, { transport: true, ppqStart: 4.1, controllerConnected: true })); // +96 ticks: row 1 -> 0
  expect(dev.leds.get(padIndex(0, 0))).toBe(BRIGHT);
  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), anchorRows: [1, 1, 1, 1], anchorSeq: 1 });
  dev.feed(run(k, { transport: true, ppqStart: 4.2, controllerConnected: true }));
  expect(dev.leds.get(padIndex(0, 0))).toBe(BRIGHT); // still where the song got to
});

test("no anchor on the hardware path leaves dead reckoning exactly as it was", () => {
  // A real Game Boy has no memory to read, so the projection sends seq 0 and empty rows forever.
  const k = kernel();
  const dev = new Device();
  dev.feed(run(k, { transport: true, ppqStart: 0, controllerConnected: true, controllerIn: [PRESS_ROW_0] }));
  push(k, { app: "lsdj-midimap", songRowTicks: rowTicks(), anchorRows: [], anchorSeq: 0 });
  dev.feed(run(k, { transport: true, ppqStart: 0.1, controllerConnected: true }));
  expect(dev.leds.get(padIndex(0, 0))).toBe(BRIGHT);
});

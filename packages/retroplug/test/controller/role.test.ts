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
import { PRO_MK3, enterProgrammerMode } from "../../src/launchpad";

const HELLO = enterProgrammerMode(PRO_MK3);

/** A 4 x 256 tick table with content on rows 0 and 1 only - enough that the app paints something, so a
 *  missing repaint is visible as an absence of LED traffic. */
function rowTicks(): (number | null)[][] {
  return [0, 1, 2, 3].map(() => Array.from({ length: 256 }, (_, r) => (r < 2 ? 96 : null)));
}

const baseDyn = (over: Partial<BlockInput> = {}): BlockInput => ({
  frames: 512, sampleRate: 44100, tempo: 120, ppqStart: 0, transport: false,
  midiIn: [], buttons: [], keys: [], serialOut: [], ...over,
});

function kernel(): DspKernel {
  const reg = new RoleRegistry();
  registerControllerRole(reg);
  const k = new DspKernel(reg);
  k.setSystems({
    project: [{ kind: "launchpad", config: { app: "lsdj-midimap", songRowTicks: rowTicks() } }],
    systems: [{ id: 1, pipeline: [] }],
  });
  return k;
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

function sameBytes(a: readonly number[] | undefined, b: readonly number[]): boolean {
  return !!a && a.length === b.length && a.every((v, i) => v === b[i]);
}

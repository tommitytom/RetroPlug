// Keyboard / gamepad -> joypad routing (useGameInput, useGamepadInput over useHeldRouting + useAxisEdges),
// observed where it leaves the UI: the backend's pressButton RPC. The backend resolves the RPC channel once,
// when the store graph is composed at boot, so wrapping it BEFORE ui.boot() sees every call.

import { test, expect, ui, navTo, Key } from "ui-harness";

type Send = (req: { method: string; params: unknown[] }) => unknown;
const ns = (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as { __rpcSend: Send };
const presses: [number, number, boolean][] = [];
const send = ns.__rpcSend;
ns.__rpcSend = (req) => {
  if (req.method === "pressButton") presses.push(req.params as [number, number, boolean]);
  return send(req);
};
const take = (): [number, number, boolean][] => presses.splice(0);
const TAB = 0x09; // DPF code; tapKey passes non-arrow codes through. Default CycleNext binding.

test("keys and pad buttons press once per hold and release to the system they pressed", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Load mGB")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  take(); // nothing before the grid is in play

  // A bound key (Right -> d-pad right): one press, one release, on the focused system.
  ui.tapKey(Key.Right);
  const key = take();
  expect(key.length).toBe(2);
  expect(key[0][2]).toBe(true);
  expect(key[1]).toEqual([key[0][0], key[0][1], false]);

  // A pad button held twice (SDL re-sending, a second pad mapped the same) presses ONCE.
  ui.gamepadButton("a", true);
  ui.gamepadButton("a", true);
  ui.gamepadButton("a", false);
  const pad = take();
  expect(pad.length).toBe(2);
  expect(pad.map((p) => p[2])).toEqual([true, false]);

  // Two instances: hold A on one, move focus, release - the release goes to the instance it pressed.
  ui.tapKey(Key.Esc);
  ui.pump(10);
  expect(navTo("Duplicate Instance")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(20);
  take();
  ui.gamepadButton("a", true);
  const [[pressedOn]] = take();
  ui.tapKey(TAB);
  ui.pump(5);
  ui.gamepadButton("a", false);
  expect(take()).toEqual([[pressedOn, pad[0][1], false]]);
  ui.gamepadButton("a", true);
  const [[nowOn]] = take();
  expect(nowOn !== pressedOn).toBeTruthy(); // focus really moved, so the check above meant something
  ui.gamepadButton("a", false);
  take();
});

test("the left stick presses the d-pad, flips direction through centre, and releases on centring", () => {
  ui.gamepadAxis("leftx", 1.0);
  const right = take();
  expect(right.length).toBe(1);
  expect(right[0][2]).toBe(true);

  ui.gamepadAxis("leftx", 0.95); // still right: nothing
  expect(take().length).toBe(0);

  ui.gamepadAxis("leftx", -1.0); // flip: release right, press left
  const flip = take();
  expect(flip.length).toBe(2);
  expect(flip[0]).toEqual([right[0][0], right[0][1], false]);
  expect(flip[1][2]).toBe(true);
  expect(flip[1][1] !== right[0][1]).toBeTruthy();

  ui.gamepadAxis("leftx", 0); // centre: release left
  expect(take()).toEqual([[flip[1][0], flip[1][1], false]]);
});

// Reading a config row the way the menu renders it (docs/modular-graph-phase-f4b-design.md section 4.2): the labels a
// cycler shows, the index a stored value reads as, and the value a step applies. The three fallbacks are the shell's
// long-standing behaviour, kept byte-identical by a declarative row so a project written by an older build (or by hand)
// shows what it did: a value of the wrong type reads as the schema default, a choice the list does not hold reads as
// index 0, and an off-grid number reads as the nearest step.
import { test, expect } from "@retroplug/testing";
import { nearestIndex, readConfigRow, rowShownFor, rowShownOn, type RoleRow } from "../contribution";

const CHANNELS: RoleRow = {
  id: "sys-channel",
  role: "test-role",
  key: "channel",
  label: "Channel",
  choices: [
    { value: "left", label: "Left" },
    { value: "right", label: "Right" },
  ],
};
const DIVISOR: RoleRow = {
  id: "sys-divisor",
  role: "test-role",
  key: "divisor",
  label: "Divisor",
  choices: [
    { value: 1, label: "1" },
    { value: 2, label: "2" },
    { value: 4, label: "4" },
  ],
};
const FAST: RoleRow = { id: "sys-fast", role: "test-role", key: "fast", label: "Fast", toggle: true };
const TEMP: RoleRow = {
  id: "sys-temp",
  role: "test-role",
  key: "temp",
  label: "Temp",
  range: { steps: [-1, 0, 1], labels: ["Cool", "Neutral", "Warm"] },
};

test("a choice row reads its stored value, and index 0 for one it does not hold", () => {
  const right = readConfigRow(CHANNELS, "right", "left");
  expect(right.names).toEqual(["Left", "Right"]);
  expect(right.index).toBe(1);
  expect(right.at(0)).toBe("left");
  const unlisted = readConfigRow(CHANNELS, "middle", "left");
  expect(unlisted.index).toBe(0);
});

test("a wrong type reads as the schema default", () => {
  // A string where the schema stores a number: the default's type decides, so a number row given "4" reads its default.
  expect(readConfigRow(DIVISOR, "4", 2).index).toBe(1);
  // A number where the schema stores a boolean or a string: the default again.
  expect(readConfigRow(FAST, 1, true).index).toBe(1);
  expect(readConfigRow(FAST, false, true).index).toBe(0);
  expect(readConfigRow(CHANNELS, 3, "right").index).toBe(1);
});

test("a range row reads the nearest step, and applies that step's value", () => {
  const near = readConfigRow(TEMP, 0.7, 0);
  expect(near.names).toEqual(["Cool", "Neutral", "Warm"]);
  expect(near.index).toBe(2);
  expect(near.at(2)).toBe(1);
  expect(nearestIndex([-1, 0, 1], 0.4)).toBe(1);
  // An off-grid value from a project written by another build still shows a step rather than a blank.
  expect(readConfigRow(TEMP, -0.9, 0).index).toBe(0);
  // A stored value of the wrong type is the default before the nearest step is taken.
  expect(readConfigRow(TEMP, "warm", -1).index).toBe(0);
});

test("a platform gate and a when gate decide whether a row shows", () => {
  expect(rowShownOn(CHANNELS, "gba")).toBe(true);
  const nesOnly: RoleRow = { ...CHANNELS, platforms: ["nes"] };
  expect(rowShownOn(nesOnly, "nes")).toBe(true);
  expect(rowShownOn(nesOnly, "gb")).toBe(false);

  const dmgOnly: RoleRow = {
    ...CHANNELS,
    when: { key: "model", in: ["dmgB", "mgb"] },
  };
  expect(rowShownFor(dmgOnly, "dmgB", "cgbC")).toBe(true);
  expect(rowShownFor(dmgOnly, "cgbC", "cgbC")).toBe(false);
  // A sibling value of the wrong type falls back to its default before the gate reads it.
  expect(rowShownFor(dmgOnly, 7, "dmgB")).toBe(true);
  const notDmg: RoleRow = { ...CHANNELS, when: { key: "model", notIn: ["dmgB", "mgb"] } };
  expect(rowShownFor(notDmg, "cgbC", "cgbC")).toBe(true);
  expect(rowShownFor(notDmg, "dmgB", "cgbC")).toBe(false);
  // No when clause at all always passes.
  expect(rowShownFor(CHANNELS, undefined, undefined)).toBe(true);
});

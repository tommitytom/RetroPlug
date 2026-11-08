// The per-model facts every other file reads: the grid's coordinate flip, the button table, and the family bytes a
// Device Inquiry can and cannot identify a model by.
import { test, expect } from "@retroplug/testing";
import {
  PRO_MK3, PROFILES, GRID_SIZE, familyHex, deviceName, isPad, padIndex, padAt, buttonName, profileForFamily,
  profilesForFamily,
} from "../profile";

test("the grid's coordinates flip exactly once: the manual's three stated anchors", () => {
  // The device counts rows upwards from the bottom, as row * 10 + col + 11; this module presents a top-left origin.
  expect(padIndex({ x: 0, y: 7 })).toBe(11);
  expect(padIndex({ x: 0, y: 0 })).toBe(81);
  expect(padIndex({ x: 7, y: 7 })).toBe(18);
  expect(padAt(11)).toEqual({ x: 0, y: 7 });
  expect(padAt(81)).toEqual({ x: 0, y: 0 });
  expect(padAt(18)).toEqual({ x: 7, y: 7 });
  // Off-grid coordinates and edge-button numbers are not pads, rather than a nonsense index.
  expect(padIndex({ x: -1, y: 0 })).toBe(-1);
  expect(padAt(19)).toBe(null);
  expect(padAt(10)).toBe(null);
  expect(padAt(0)).toBe(null);
  expect(isPad({ x: GRID_SIZE, y: 0 })).toBe(false);
  expect(isPad({ x: 0.5, y: 0 })).toBe(false);
});

test("every named button reverse-looks-up to itself, and an unknown CC is nothing", () => {
  const names = Object.keys(PRO_MK3.buttons);
  expect(names.length).toBe(33);
  for (const name of names) expect(buttonName(PRO_MK3, PRO_MK3.buttons[name])).toBe(name);
  expect(buttonName(PRO_MK3, 1)).toBe(null);
});

test("family bytes find a model but do not identify one: Novation reuses them", () => {
  // The Mini MK3 and the Pro MK3 both answer 13 01, which is why the scan asks the model its own question.
  expect(profilesForFamily([0x13, 0x01])).toEqual([PRO_MK3]);
  expect(profileForFamily([0x13, 0x01])).toBe(PRO_MK3);
  expect(profileForFamily([0x0d, 0x01])).toBe(null); // an unknown family: no profile, but still a Novation
  expect(profilesForFamily([0x0d, 0x01])).toEqual([]);
  expect(familyHex([0x0d, 0x01])).toBe("0d 01");
  expect(deviceName([0x13, 0x01])).toBe(PRO_MK3.name);
  expect(deviceName([0x0d, 0x01])).toBe("Novation device (family 0d 01)");
  // The table is by a short key, and its family bytes are the ones profileForFamily matches.
  expect(PROFILES["pro-mk3"]).toBe(PRO_MK3);
});

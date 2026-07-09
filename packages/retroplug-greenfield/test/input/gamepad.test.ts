// buildGamepadToButton inverts a resolved gamepad binding map (GB-button-name → SDL-button-name[]) into
// the SDL-name → GameboyButton lookup useGamepadInput indexes per controller event. Values mirror native
// InputTypes.hpp (Right=0 … Start=7). Gamepad names are the raw "gamepad-button" bus names (SDL canonical),
// so there is no code table — unlike the keyboard channel's resolveKeyName.
import { test, expect } from "../../testing/harness";
import { defaultBindingMap } from "../../src/bindingMap";
import { buildGamepadToButton, BUTTON_VALUE } from "../../src/keyCodes";

test("buildGamepadToButton: the default bindings invert to the native button values", () => {
  const m = buildGamepadToButton(defaultBindingMap().gamepad);
  expect(m.get("dpright")).toBe(0); // Right
  expect(m.get("dpleft")).toBe(1); // Left
  expect(m.get("dpup")).toBe(2); // Up
  expect(m.get("dpdown")).toBe(3); // Down
  expect(m.get("a")).toBe(4); // A
  expect(m.get("b")).toBe(5); // B
  expect(m.get("back")).toBe(6); // Select
  expect(m.get("start")).toBe(7); // Start
  expect(m.get("guide")).toBe(undefined); // an unbound controller button falls through
});

test("buildGamepadToButton: multi-bind + a later binding for the same name wins", () => {
  const m = buildGamepadToButton({ A: ["a", "y"], B: ["y"] });
  expect(m.get("a")).toBe(BUTTON_VALUE.A); // A keeps its own binding
  expect(m.get("y")).toBe(BUTTON_VALUE.B); // B is declared after A, so "y" resolves to B
});

test("buildGamepadToButton: unknown button names are skipped", () => {
  const m = buildGamepadToButton({ Bogus: ["dpright"], A: ["a"] });
  expect(m.get("dpright")).toBe(undefined); // Bogus GB button dropped
  expect(m.get("a")).toBe(BUTTON_VALUE.A);
  expect(m.size).toBe(1);
});

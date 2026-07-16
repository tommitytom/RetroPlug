// buildGamepadToButton inverts a resolved gamepad binding map (GB-button-name → SDL-button-name[]) into
// the SDL-name → GameboyButton lookup useGamepadInput indexes per controller event. Values mirror native
// InputTypes.hpp (Right=0 … Start=7). Gamepad names are the raw "gamepad-button" bus names (SDL canonical),
// so there is no code table — unlike the keyboard channel's resolveKeyName.
import { test, expect } from "../../testing/harness";
import { defaultBindingMap } from "../../src/bindingMap";
import { buildGamepadToButton, buildGamepadToAction, axisToken, menuNavForButton, menuNavForAxisToken, BUTTON_VALUE } from "../../src/keyCodes";

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

test("buildGamepadToButton: the default maps the left-stick half-axis tokens to the d-pad", () => {
  const m = buildGamepadToButton(defaultBindingMap().gamepad);
  expect(m.get("leftx+")).toBe(0); // Right
  expect(m.get("leftx-")).toBe(1); // Left
  expect(m.get("lefty-")).toBe(2); // Up (SDL: Y negative = up)
  expect(m.get("lefty+")).toBe(3); // Down
});

test("axisToken: sign → half-axis token, hysteresis, and centre release", () => {
  // Past the press threshold in each direction (SDL: X+ right, Y+ down).
  expect(axisToken("lefty", -0.9, "")).toBe("lefty-"); // up
  expect(axisToken("lefty", 0.9, "")).toBe("lefty+"); // down
  expect(axisToken("leftx", -0.6, "")).toBe("leftx-"); // left
  expect(axisToken("leftx", 0.6, "")).toBe("leftx+"); // right
  // Centre / below press with nothing held → no token.
  expect(axisToken("lefty", 0.0, "")).toBe("");
  expect(axisToken("lefty", 0.2, "")).toBe("");
  // Hysteresis: once held, stays held down to the release band (0.4), then drops at centre.
  expect(axisToken("lefty", 0.45, "lefty+")).toBe("lefty+"); // still pushed (≥ 0.4)
  expect(axisToken("lefty", 0.35, "lefty+")).toBe(""); // fell under release → centred
  // A flip through centre releases the old side first (never holds the wrong sign).
  expect(axisToken("lefty", -0.45, "lefty+")).toBe(""); // now negative but not past press → release, don't hold "+"
  expect(axisToken("lefty", -0.9, "lefty+")).toBe("lefty-"); // full flip → opposite token
});

test("menuNavForButton: the fixed d-pad/face buttons map to menu actions", () => {
  expect(menuNavForButton("dpup")).toBe("up");
  expect(menuNavForButton("dpdown")).toBe("down");
  expect(menuNavForButton("dpleft")).toBe("left"); // Left/Right cycle a value, not move focus
  expect(menuNavForButton("dpright")).toBe("right");
  expect(menuNavForButton("a")).toBe("select");
  expect(menuNavForButton("b")).toBe("back");
});

test("menuNavForButton: buttons with no menu role fall through", () => {
  expect(menuNavForButton("start")).toBe(null); // Start is gameplay-only; leftshoulder opens the menu (App)
  expect(menuNavForButton("leftshoulder")).toBe(null);
  expect(menuNavForButton("x")).toBe(null);
  expect(menuNavForButton("")).toBe(null);
});

test("menuNavForAxisToken: only the left stick navigates, per the SDL sign convention", () => {
  expect(menuNavForAxisToken("lefty-")).toBe("up"); // SDL: Y negative = up
  expect(menuNavForAxisToken("lefty+")).toBe("down");
  expect(menuNavForAxisToken("leftx-")).toBe("left");
  expect(menuNavForAxisToken("leftx+")).toBe("right");
  // Right stick + centred axis don't drive the menu.
  expect(menuNavForAxisToken("rightx+")).toBe(null);
  expect(menuNavForAxisToken("righty-")).toBe(null);
  expect(menuNavForAxisToken("")).toBe(null);
});

test("buildGamepadToAction: the default app-action bindings invert to SDL-name → action", () => {
  const m = buildGamepadToAction(defaultBindingMap().gamepadActions);
  expect(m.get("leftshoulder")).toBe("OpenMenu"); // L1
  expect(m.get("rightshoulder")).toBe("CycleNext"); // R1
  expect(m.get("a")).toBe(undefined); // a face button is a GB button, not an action
});

test("buildGamepadToAction: unknown action ids skipped; null-tolerant", () => {
  const m = buildGamepadToAction({ OpenMenu: ["guide"], Bogus: ["x"] });
  expect(m.get("guide")).toBe("OpenMenu");
  expect(m.get("x")).toBe(undefined); // Bogus action id dropped
  expect(m.size).toBe(1);
  expect(buildGamepadToAction(undefined).size).toBe(0); // older profile lacking the section
});

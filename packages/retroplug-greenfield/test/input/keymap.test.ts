// The keycode→button resolver: symbolic key names (as stored in bindings JSON) resolve to DPF codes,
// and buildKeyToButton inverts a resolved keyboard map into the DPF-code → GameboyButton lookup the game
// -input hook indexes per keystroke. Values mirror native InputTypes.hpp (Right=0 … Start=7).
import { test, expect } from "../../testing/harness";
import { defaultBindingMap } from "../../src/bindingMap";
import { resolveKeyName, buildKeyToButton, BUTTON_VALUE } from "../../src/keyCodes";

test("resolveKeyName: named keys, single-char codepoints, unknown → null", () => {
  expect(resolveKeyName("Right")).toBe(0xe037);
  expect(resolveKeyName("Enter")).toBe(0x0d);
  expect(resolveKeyName("Return")).toBe(0x0d); // synonym
  expect(resolveKeyName("Backspace")).toBe(0x08);
  expect(resolveKeyName("ShiftL")).toBe(0xe051);
  expect(resolveKeyName("Z")).toBe(0x5a);
  expect(resolveKeyName("z")).toBe(0x7a); // case preserved
  expect(resolveKeyName("Nonsense")).toBe(null);
});

test("buildKeyToButton: the default bindings invert to the native button values", () => {
  const m = buildKeyToButton(defaultBindingMap().keyboard);
  expect(m.get(0xe037)).toBe(0); // Right
  expect(m.get(0xe035)).toBe(1); // Left
  expect(m.get(0xe036)).toBe(2); // Up
  expect(m.get(0xe038)).toBe(3); // Down
  expect(m.get(0x5a)).toBe(4); // Z → A
  expect(m.get(0x7a)).toBe(4); // z → A (multi-bind)
  expect(m.get(0x58)).toBe(5); // X → B
  expect(m.get(0x78)).toBe(5); // x → B
  expect(m.get(0x0d)).toBe(7); // Enter → Start
  expect(m.get(0xe051)).toBe(6); // ShiftL → Select
  expect(m.get(0xe052)).toBe(6); // ShiftR → Select
  expect(m.get(0x08)).toBe(6); // Backspace → Select
  expect(m.get(0x1b)).toBe(undefined); // Esc is unbound — falls through to the menu handler
});

test("buildKeyToButton: unknown button names + unresolvable keys are skipped", () => {
  const m = buildKeyToButton({ Bogus: ["Right"], A: ["Nonsense", "Z"] });
  expect(m.get(0xe037)).toBe(undefined); // Bogus button dropped
  expect(m.get(0x5a)).toBe(BUTTON_VALUE.A); // the resolvable key still maps
  expect(m.size).toBe(1);
});

// The keycode→button resolver: symbolic key names (as stored in bindings JSON) resolve to DPF codes,
// and buildKeyToButton inverts a resolved keyboard map into the DPF-code → GameboyButton lookup the game
// -input hook indexes per keystroke. Values mirror native InputTypes.hpp (Right=0 … Start=7).
import { test, expect } from "../../testing/harness";
import { defaultBindingMap } from "../../src/bindingMap";
import { resolveKeyName, dpfCodeToKeyName, keyDisplayName, buildKeyToButton, buildKeyToAction, BUTTON_VALUE } from "../../src/keyCodes";

test("resolveKeyName: named keys, single-char codepoints, unknown → null", () => {
  expect(resolveKeyName("Right")).toBe(0xe037);
  expect(resolveKeyName("Enter")).toBe(0x0d);
  expect(resolveKeyName("Return")).toBe(0x0d); // synonym
  expect(resolveKeyName("Backspace")).toBe(0x08);
  expect(resolveKeyName("ShiftL")).toBe(0xe051);
  expect(resolveKeyName("Z")).toBe(0x5a);
  expect(resolveKeyName("z")).toBe(0x7a); // case preserved
  expect(resolveKeyName("Space")).toBe(0x20);
  expect(resolveKeyName(" ")).toBe(0x20); // a profile written before Space had a name
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

test("dpfCodeToKeyName: named codes, printable ASCII, unknown → null", () => {
  expect(dpfCodeToKeyName(0x0d)).toBe("Enter"); // canonical, not "Return"
  expect(dpfCodeToKeyName(0x1b)).toBe("Escape");
  expect(dpfCodeToKeyName(0x08)).toBe("Backspace");
  expect(dpfCodeToKeyName(0x09)).toBe("Tab");
  expect(dpfCodeToKeyName(0xe035)).toBe("Left");
  expect(dpfCodeToKeyName(0xe036)).toBe("Up");
  expect(dpfCodeToKeyName(0xe037)).toBe("Right");
  expect(dpfCodeToKeyName(0xe038)).toBe("Down");
  expect(dpfCodeToKeyName(0xe051)).toBe("ShiftL");
  expect(dpfCodeToKeyName(0xe052)).toBe("ShiftR");
  expect(dpfCodeToKeyName(0x51)).toBe("Q"); // printable ASCII
  expect(dpfCodeToKeyName(0x7a)).toBe("z"); // case preserved
  expect(dpfCodeToKeyName(0x20)).toBe("Space"); // the word, not the invisible glyph
  expect(dpfCodeToKeyName(0x00)).toBe(null); // NUL — below printable range
  expect(dpfCodeToKeyName(0xe099)).toBe(null); // unknown named-band code
});

test("dpfCodeToKeyName ∘ resolveKeyName: a captured key round-trips to its code", () => {
  for (const code of [0x0d, 0x1b, 0x08, 0x09, 0x20, 0xe035, 0xe036, 0xe037, 0xe038, 0xe051, 0xe052, 0x51, 0x7a]) {
    expect(resolveKeyName(dpfCodeToKeyName(code)!)).toBe(code);
  }
});

test("keyDisplayName: a stored raw space still shows as Space; other names pass through", () => {
  expect(keyDisplayName(" ")).toBe("Space"); // the pre-Space profiles the bindings editor has to render
  expect(keyDisplayName("Space")).toBe("Space");
  expect(keyDisplayName("Z")).toBe("Z");
  expect(keyDisplayName("Return")).toBe("Enter"); // canonicalised, like a fresh capture would store it
  expect(keyDisplayName("Nonsense")).toBe("Nonsense"); // unresolvable — shown as-is
});

test("buildKeyToButton: unknown button names + unresolvable keys are skipped", () => {
  const m = buildKeyToButton({ Bogus: ["Right"], A: ["Nonsense", "Z"] });
  expect(m.get(0xe037)).toBe(undefined); // Bogus button dropped
  expect(m.get(0x5a)).toBe(BUTTON_VALUE.A); // the resolvable key still maps
  expect(m.size).toBe(1);
});

test("buildKeyToAction: the default app-action bindings invert to DPF-code → action", () => {
  const m = buildKeyToAction(defaultBindingMap().keyboardActions);
  expect(m.get(0x1b)).toBe("OpenMenu"); // Escape
  expect(m.get(0x09)).toBe("CycleNext"); // Tab
  expect(m.get(0x5a)).toBe(undefined); // Z is a GB button, not an action key
});

test("buildKeyToAction: unknown action ids + unresolvable keys skipped; null-tolerant", () => {
  const m = buildKeyToAction({ OpenMenu: ["Escape"], Bogus: ["Enter"], CyclePrev: ["Nonsense"] });
  expect(m.get(0x1b)).toBe("OpenMenu");
  expect(m.get(0x0d)).toBe(undefined); // Bogus action id dropped
  expect(m.size).toBe(1); // CyclePrev's "Nonsense" doesn't resolve
  expect(buildKeyToAction(undefined).size).toBe(0); // an older profile lacking the section
});

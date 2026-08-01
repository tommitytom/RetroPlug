// The prompt text-input case transform (ui/screens/menu/promptCasing). DPF's "key" bus carries the
// UNSHIFTED code point, so the menu applies Shift / the prompt's casing policy itself: "mixed" (default,
// e.g. project rename) lets Shift uppercase a letter; "upper" (LSDj / risa song names) forces uppercase.
import { test, expect } from "../../testing/harness";
import { applyCasing } from "../../ui/screens/menu/promptCasing";

test("mixed casing (default) respects Shift", () => {
  expect(applyCasing("a", false, "mixed")).toBe("a");
  expect(applyCasing("a", true, "mixed")).toBe("A");
  expect(applyCasing("a", false)).toBe("a"); // undefined casing == mixed
  expect(applyCasing("a", true)).toBe("A");
  // Non-letters pass through unchanged under either Shift state.
  expect(applyCasing("1", true, "mixed")).toBe("1");
  expect(applyCasing("-", true)).toBe("-");
});

test("upper casing forces uppercase regardless of Shift", () => {
  expect(applyCasing("a", false, "upper")).toBe("A");
  expect(applyCasing("a", true, "upper")).toBe("A");
  expect(applyCasing("z", false, "upper")).toBe("Z");
  // Non-letters are untouched.
  expect(applyCasing("7", false, "upper")).toBe("7");
});

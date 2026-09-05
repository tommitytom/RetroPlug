// The harness's own matchers. The point of the newer ones (toBeGreaterThan / toBeLessThan / toBeCloseTo and
// the optional message) is that a FAILURE carries the numbers: `expect(x > y).toBeTruthy()` fails as
// "expected truthy, got false" and the case has to be re-run with a console.log to learn what x was.
import { test, expect } from "../../testing/harness";

const failureOf = (fn: () => void): string => {
  try {
    fn();
  } catch (e) {
    return (e as Error).message;
  }
  return "(did not throw)";
};

test("comparison matchers pass on the right side of the bound and fail with both numbers", () => {
  expect(5).toBeGreaterThan(4);
  expect(5).toBeGreaterThanOrEqual(5);
  expect(3).toBeLessThan(4);
  expect(4).toBeLessThanOrEqual(4);
  expect(failureOf(() => expect(12).toBeGreaterThan(50))).toBe("expected > 50, got 12");
  expect(failureOf(() => expect(4).toBeGreaterThanOrEqual(5))).toBe("expected >= 5, got 4");
  expect(failureOf(() => expect(0.5).toBeLessThan(0.25))).toBe("expected < 0.25, got 0.5");
  expect(failureOf(() => expect(9).toBeLessThanOrEqual(8))).toBe("expected <= 8, got 9");
  // NaN never satisfies a bound, and a non-number is rejected outright rather than coerced.
  expect(failureOf(() => expect(NaN).toBeGreaterThan(0))).toBe("expected > 0, got NaN");
  expect(failureOf(() => expect("7").toBeGreaterThan(0))).toBe('toBeGreaterThan expects a number, got "7"');
});

test("toBeCloseTo is an absolute tolerance and reports how far off the value was", () => {
  expect(440.4).toBeCloseTo(440, 0.5);
  expect(0.1 + 0.2).toBeCloseTo(0.3); // the default 1e-9 absorbs float noise
  expect(failureOf(() => expect(447).toBeCloseTo(440, 5))).toBe("expected 440 +/- 5, got 447 (off by 7)");
});

test("an optional message is prefixed to every matcher's failure", () => {
  expect(failureOf(() => expect(false, "pulse1 sounding").toBeTruthy())).toBe("pulse1 sounding: expected truthy, got false");
  expect(failureOf(() => expect(213, "period after the array").toBe(427))).toBe("period after the array: expected 427, got 213");
  expect(failureOf(() => expect(1, "frames").toBeGreaterThan(20))).toBe("frames: expected > 20, got 1");
  expect(failureOf(() => expect(() => {}, "boom").toThrow())).toBe("boom: expected function to throw, but it did not");
});

test("the original five still behave, with actual/expected in the failure", () => {
  expect(failureOf(() => expect(1).toBe(2))).toBe("expected 2, got 1");
  expect(failureOf(() => expect([1]).toEqual([2]))).toBe("expected [2], got [1]");
  expect(failureOf(() => expect(0).toBeTruthy())).toBe("expected truthy, got 0");
  expect(failureOf(() => expect("x").toBeFalsy())).toBe('expected falsy, got "x"');
  expect(() => { throw new Error("bad byte"); }).toThrow("bad byte");
});

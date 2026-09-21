// The skip directive. A case that cannot run must be reported as SKIPPED, never as a pass — for a long
// time these suites logged "# SKIP" to stdout and returned, so the case printed `ok` and counted as
// coverage that had never executed. Nine of the ten assertions here pin the mechanism that replaced it.
//
// `skip()` throws, and that choice is load-bearing: several test files delegate an entire case to a
// shared helper, so a sentinel return value would end the helper while the case carried on to its
// assertions. These tests pin the propagation paths that a return value would break, and the one
// `catch` inside the harness itself that would otherwise swallow a skip and report a green tick.
import { test, expect, skip } from "../../testing/harness";

/** What the harness itself does: match the brand structurally, never by `instanceof`. */
const isSkipShape = (e: unknown): boolean =>
  typeof e === "object" && e !== null && (e as { rpSkip?: unknown }).rpSkip === true;

const caught = (fn: () => void): unknown => {
  try {
    fn();
  } catch (e) {
    return e;
  }
  return undefined;
};

test("skip throws a branded error carrying its reason", () => {
  const e = caught(() => skip("no ROM at /nowhere.nes"));
  expect(isSkipShape(e), "the brand the harness matches on").toBeTruthy();
  expect((e as { rpSkipReason?: string }).rpSkipReason).toBe("no ROM at /nowhere.nes");
  // It is still an Error, so an unrelated catch that formats e.message gets something readable.
  expect(e instanceof Error).toBeTruthy();
  expect((e as Error).message).toBe("SKIP: no ROM at /nowhere.nes");
});

test("a skip propagates out of a helper, so a case that delegates its whole body is not reported as a pass", () => {
  // The shape of app-cores.test.ts: one helper, several cases, the fixture check inside the helper.
  const bootsAndRenders = (rom: string): void => {
    if (rom === "") skip("ROM not found");
    throw new Error("unreachable: the skip should have left the helper");
  };
  const e = caught(() => bootsAndRenders(""));
  expect(isSkipShape(e), "the skip escaped the helper").toBeTruthy();
});

test("a skip propagates out of a loop, past the assertions that follow it", () => {
  // The shape of cartridge-accuracy.test.ts: the fixture check is mid-body inside a for, with expects
  // after the loop. A `return` there exits the case silently; a throw cannot be mistaken for success.
  let reachedAfterLoop = false;
  const e = caught(() => {
    for (const mode of ["chip", "n8"]) {
      if (mode === "n8") skip("no ROM for the second mode");
    }
    reachedAfterLoop = true;
  });
  expect(isSkipShape(e)).toBeTruthy();
  expect(reachedAfterLoop, "the code after the loop must not have run").toBeFalsy();
});

test("toThrow re-throws a skip instead of counting it as the throw it was waiting for", () => {
  // Without the guard this is the nastiest regression the directive could introduce: `toThrow`'s catch
  // is what makes the matcher work, and it would turn an un-runnable case into a GREEN assertion.
  const e = caught(() => expect(() => skip("fixture absent")).toThrow());
  expect(isSkipShape(e), "toThrow swallowed the skip and passed").toBeTruthy();
  // A real throw is still matched normally — the guard must not break the matcher it protects.
  expect(() => {
    throw new Error("bad byte");
  }).toThrow("bad byte");
});

test("skip returns never, so `return skip(...)` type-checks in a void body", () => {
  // Eleven call sites in lsdj-playback-probe.test.ts are written `return skip(...)`; this pins that
  // `never` stays assignable to a void return, which is what let them convert verbatim.
  const viaReturn = (why: string): void => {
    if (why) return skip(why);
  };
  expect(isSkipShape(caught(() => viaReturn("unsupported version")))).toBeTruthy();
});

// createSignal, the writable Readable a package owns and the shell watches (the view contract's `visible` and a
// package's own debug flag): a get, a set, and a subscription told only when the value moves.
import { test, expect } from "@retroplug/testing";

import { createSignal } from "../menu/subscribable";

test("a signal reads back what it was set to", () => {
  const flag = createSignal(false);
  expect(flag.get()).toBe(false);
  flag.set(true);
  expect(flag.get()).toBe(true);
});

test("a signal notifies every subscriber, and only when the value moves", () => {
  const flag = createSignal(false);
  let a = 0;
  let b = 0;
  const offA = flag.subscribe(() => (a += 1));
  flag.subscribe(() => (b += 1));
  flag.set(true);
  expect(`${a}/${b}`).toBe("1/1");
  // Object.is: the same value is not a change, so a set to what is already there costs no repaint.
  flag.set(true);
  expect(`${a}/${b}`).toBe("1/1");
  offA();
  flag.set(false);
  expect(`${a}/${b}`).toBe("1/2");
});

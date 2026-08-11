// Decoding what the device sends back. The interesting cases are the two quirks a caller should never
// have to think about: the device sends Note On with velocity 0 for note off, and the same number means
// a different control depending on whether it arrived as a note or a CC.
import { test, expect } from "../../testing/harness";
import { PRO_MK3, decodeMessage, decodeMessages } from "../../src/launchpad";

const dec = (bytes: number[]) => decodeMessage(PRO_MK3, bytes);

test("a pad press decodes to a down event with its grid coordinate", () => {
  const ev = dec([0x90, 11, 100])!; // the lower-left pad
  expect(ev.kind).toBe("down");
  expect(ev.velocity).toBe(100);
  expect(ev.pad).toEqual({ x: 0, y: 7 }); // top-left origin, so the bottom row is y=7
  expect(ev.button).toBe(null);
});

test("both note-off forms decode as up — including the one the device actually sends", () => {
  expect(dec([0x80, 11, 0])!.kind).toBe("up"); // real Note Off
  expect(dec([0x90, 11, 0])!.kind).toBe("up"); // Note On velocity 0, which is what a Launchpad sends
});

test("aftertouch decodes as pressure, keeping the pad it belongs to", () => {
  const ev = dec([0xa0, 88, 64])!;
  expect(ev.kind).toBe("pressure");
  expect(ev.velocity).toBe(64);
  expect(ev.pad).toEqual({ x: 7, y: 0 });
});

test("an edge button decodes by name, with no pad coordinate", () => {
  const down = dec([0xb0, PRO_MK3.buttons.session, 127])!;
  expect(down.kind).toBe("down");
  expect(down.button).toBe("session");
  expect(down.pad).toBe(null);

  expect(dec([0xb0, PRO_MK3.buttons.session, 0])!.kind).toBe("up");
});

test("traffic that is not surface input is ignored rather than mistaken for a press", () => {
  expect(dec([0xf8])).toBe(null); // MIDI clock
  expect(dec([0xb0, 7, 100])).toBe(null); // a CC that is not one of our buttons (volume)
  expect(dec([0xf0, 0x00, 0x20, 0x29])).toBe(null); // a SysEx reply
  expect(dec([0x90, 11])).toBe(null); // truncated
  expect(dec([])).toBe(null);
});

test("channel is ignored on input — the device uses it for lighting, not for identifying a control", () => {
  expect(dec([0x91, 11, 100])!.pad).toEqual({ x: 0, y: 7 });
  expect(dec([0x9f, 11, 100])!.pad).toEqual({ x: 0, y: 7 });
});

test("a batch decodes in order, dropping what it does not understand", () => {
  const events = decodeMessages(PRO_MK3, [
    [0x90, 11, 100],
    [0xf8], // clock, dropped
    [0x90, 11, 0],
  ]);
  expect(events.length).toBe(2);
  expect(events[0].kind).toBe("down");
  expect(events[1].kind).toBe("up");
});

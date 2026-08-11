// The diffing surface: does a repaint-every-frame caller actually produce almost no MIDI?
//
// That is the property the whole design rests on. An app should be able to describe the entire surface
// declaratively each frame; the surface's job is to notice that nearly nothing changed and stay quiet.
import { test, expect } from "../../testing/harness";
import { Surface, BULK_THRESHOLD, PRO_MK3, Palette, palette, rgb, type Led } from "../../src/launchpad";

const RED: Led = { mode: "static", colour: palette(Palette.red) };
const GREEN: Led = { mode: "static", colour: palette(Palette.green) };

test("the first flush paints nothing when nothing was set", () => {
  // Everything starts off, and the device is assumed off too, so an untouched surface is silent.
  expect(new Surface().flush().dirty).toBe(0);
});

test("one changed pad emits exactly one short message", () => {
  const s = new Surface();
  s.setPad(0, 7, RED); // the lower-left pad
  const r = s.flush();
  expect(r.dirty).toBe(1);
  expect(r.messages.length).toBe(1);
  expect(r.messages[0]).toEqual([0x90, 11, Palette.red]);
});

test("a flush with no further changes is completely silent", () => {
  const s = new Surface();
  s.setPad(0, 0, RED);
  expect(s.flush().dirty).toBe(1);

  s.setPad(0, 0, RED); // set to the same value again — the declarative repaint case
  const second = s.flush();
  expect(second.dirty).toBe(0);
  expect(second.messages.length).toBe(0);
});

test("only the pad that actually changed is re-sent", () => {
  const s = new Surface();
  for (let x = 0; x < 8; x++) s.setPad(x, 0, RED);
  s.flush();

  s.setPad(3, 0, GREEN); // one pad differs; the other seven are re-set to what they already are
  for (let x = 0; x < 8; x++) if (x !== 3) s.setPad(x, 0, RED);
  const r = s.flush();
  expect(r.dirty).toBe(1);
  expect(r.messages).toEqual([[0x90, 84, Palette.green]]); // x=3, y=0 -> 81+3
});

test("a big change switches to one bulk SysEx instead of a burst of messages", () => {
  const s = new Surface();
  for (let i = 0; i <= BULK_THRESHOLD; i++) s.setPad(i % 8, Math.floor(i / 8), RED);
  const r = s.flush();

  expect(r.dirty).toBe(BULK_THRESHOLD + 1);
  expect(r.messages.length).toBe(1); // one message, not BULK_THRESHOLD+1 of them
  expect(r.messages[0][0]).toBe(0xf0);
  expect(r.messages[0][r.messages[0].length - 1]).toBe(0xf7);
});

test("a change at the threshold stays in short form", () => {
  const s = new Surface();
  for (let i = 0; i < BULK_THRESHOLD; i++) s.setPad(i, 0, RED);
  const r = s.flush();
  expect(r.messages.length).toBe(BULK_THRESHOLD);
  expect(r.messages.every((m) => m.length === 3)).toBeTruthy();
});

test("any RGB forces the bulk form even for a single pad", () => {
  const s = new Surface();
  s.setPad(0, 0, { mode: "static", colour: rgb(127, 64, 0) });
  const r = s.flush();
  expect(r.dirty).toBe(1);
  expect(r.messages.length).toBe(1);
  expect(r.messages[0][0]).toBe(0xf0); // SysEx, because velocity cannot carry an RGB triple
  expect(r.messages[0].slice(7, 12)).toEqual([0x03, 81, 127, 64, 0]);
});

test("repainting the whole surface fits in a single bulk message", () => {
  // 64 pads + 33 edge buttons is under the device's 106-spec limit, so a full repaint is one write.
  const s = new Surface();
  for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) s.setPad(x, y, RED);
  const r = s.flush();
  expect(r.dirty).toBe(64);
  expect(r.messages.length).toBe(1);
});

test("clear() blanks what was lit, and then goes quiet", () => {
  const s = new Surface();
  for (let x = 0; x < 8; x++) s.setPad(x, 0, RED);
  s.flush();

  s.clear();
  expect(s.flush().dirty).toBe(8); // the eight lit pads are blanked
  s.clear();
  expect(s.flush().dirty).toBe(0); // already blank
});

test("invalidate() forces a full repaint including the LEDs that are off", () => {
  const s = new Surface();
  s.setPad(0, 0, RED);
  s.flush();

  // Entering Programmer mode blanks the surface, so our baseline is stale: a plain diff would send
  // nothing and leave the device dark. invalidate() is how a host recovers from that.
  s.invalidate();
  const r = s.flush();
  expect(r.dirty > 64).toBeTruthy(); // every pad AND every button, lit or not
  expect(r.messages.length > 0).toBeTruthy();
});

test("named buttons are addressed by CC, and unknown names are ignored", () => {
  const s = new Surface();
  s.setButton("logo", RED);
  expect(s.flush().messages).toEqual([[0xb0, PRO_MK3.buttons.logo, Palette.red]]);

  s.setButton("nonexistent", RED);
  expect(s.flush().dirty).toBe(0);
});

test("out-of-range writes are ignored rather than lighting something unrelated", () => {
  const s = new Surface();
  s.setPad(-1, 0, RED);
  s.setPad(0, 8, RED);
  s.set(255, RED);
  expect(s.flush().dirty).toBe(0);
});

test("an LED the device could not render is refused rather than mis-encoded", () => {
  const s = new Surface();
  s.setPad(0, 0, { mode: "pulse", colour: rgb(127, 0, 0) }); // pulse carries a palette index only
  expect(s.peek(81)).toEqual({ mode: "off" });
  expect(s.flush().dirty).toBe(0);
});

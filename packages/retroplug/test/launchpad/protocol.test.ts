// The Launchpad protocol encoder, checked against the Programmer's Reference Manual's OWN worked
// examples. The manual states these as exact hex, so they are the strongest oracle available: if our
// encoder reproduces them byte-for-byte we are speaking the documented protocol, not our reading of it.
import { test, expect } from "../../testing/harness";
import {
  PRO_MK3, GRID_SIZE, isPad, padIndex, padAt, buttonName, controlKind,
  Palette, palette, rgb, needsSysex, isRenderable,
  lightMessage, colourSpec, ledLightingSysex,
  enterProgrammerMode, exitToLiveMode, selectLayout, setStandaloneMode,
  deviceInquiry, parseInquiryReply,
  type Led,
} from "../../src/launchpad";

const hex = (bytes: readonly number[]): string => bytes.map((b) => b.toString(16).padStart(2, "0")).join(" ");

// --- grid addressing -----------------------------------------------------------------------------
// The manual states three anchors in prose (the rest of the layout is an image): note 11 is the lower
// LEFT pad, 81 the upper left, 18 the lower right. Our API is top-left origin, so y is flipped.

test("the manual's three stated grid anchors map correctly", () => {
  expect(padIndex({ x: 0, y: 7 })).toBe(11); // lower left
  expect(padIndex({ x: 0, y: 0 })).toBe(81); // upper left
  expect(padIndex({ x: 7, y: 7 })).toBe(18); // lower right
  expect(padIndex({ x: 7, y: 0 })).toBe(88); // upper right, by extension
});

test("pad index round-trips for all 64 pads", () => {
  for (let y = 0; y < GRID_SIZE; y++) {
    for (let x = 0; x < GRID_SIZE; x++) {
      const back = padAt(padIndex({ x, y }));
      expect(back !== null && back.x === x && back.y === y).toBeTruthy();
    }
  }
});

test("off-grid coordinates and non-pad indices are rejected rather than invented", () => {
  expect(padIndex({ x: -1, y: 0 })).toBe(-1);
  expect(padIndex({ x: 8, y: 0 })).toBe(-1);
  expect(padIndex({ x: 0, y: 8 })).toBe(-1);
  expect(padIndex({ x: 0.5, y: 0 })).toBe(-1);
  expect(isPad({ x: 7, y: 7 })).toBe(true);

  expect(padAt(99)).toBe(null); // the logo
  expect(padAt(91)).toBe(null); // a top-row button
  expect(padAt(10)).toBe(null); // left column (col 0)
  expect(padAt(19)).toBe(null); // right column (col 9)
  expect(padAt(0)).toBe(null);
  expect(padAt(200)).toBe(null);
});

test("grid pads are addressed by note, everything else by CC", () => {
  expect(controlKind(11)).toBe("note");
  expect(controlKind(88)).toBe("note");
  expect(controlKind(99)).toBe("cc"); // logo
  expect(controlKind(91)).toBe("cc"); // top row
  expect(buttonName(PRO_MK3, 99)).toBe("logo");
  expect(buttonName(PRO_MK3, 12)).toBe(null); // a grid pad is not a named button
});

// --- lighting, short form ------------------------------------------------------------------------
// Manual examples 1-4, verbatim.

test("GOLDEN: static red on the lower-left pad is 90 0B 05", () => {
  const msg = lightMessage(padIndex({ x: 0, y: 7 }), { mode: "static", colour: palette(Palette.red) })!;
  expect(hex(msg)).toBe("90 0b 05");
});

test("GOLDEN: flashing green on the upper-left pad is 91 51 13", () => {
  const led: Led = { mode: "flash", colour: palette(Palette.green), alt: palette(Palette.off) };
  expect(hex(lightMessage(padIndex({ x: 0, y: 0 }), led)!)).toBe("91 51 13");
});

test("GOLDEN: pulsing blue on the lower-right pad is 92 12 2D", () => {
  const led: Led = { mode: "pulse", colour: palette(Palette.blue) };
  expect(hex(lightMessage(padIndex({ x: 7, y: 7 }), led)!)).toBe("92 12 2d");
});

test("GOLDEN: blanking the lower-right pad is 90 12 00", () => {
  expect(hex(lightMessage(padIndex({ x: 7, y: 7 }), { mode: "off" })!)).toBe("90 12 00");
});

test("an edge button lights with a CC status, not a note", () => {
  const msg = lightMessage(PRO_MK3.buttons.logo, { mode: "static", colour: palette(Palette.red) })!;
  expect(hex(msg)).toBe("b0 63 05"); // 0x63 = 99
});

// --- lighting, bulk SysEx ------------------------------------------------------------------------

test("GOLDEN: the manual's three-entry bulk LED message", () => {
  // "sets up the bottom left pad to static yellow, the pad next to it to flashing green (between dim
  // and bright green), and the pad next to that pulsing turquoise"
  const entries = [
    { index: 11, led: { mode: "static", colour: palette(Palette.yellow) } as Led },
    { index: 12, led: { mode: "flash", colour: palette(Palette.greenDim), alt: palette(Palette.greenBright) } as Led },
    { index: 13, led: { mode: "pulse", colour: palette(Palette.turquoise) } as Led },
  ];
  const msgs = ledLightingSysex(PRO_MK3, entries);
  expect(msgs.length).toBe(1);
  expect(hex(msgs[0])).toBe("f0 00 20 29 02 0e 03 00 0b 0d 01 0c 15 17 02 0d 25 f7");
});

test("a bulk message splits at the device's 106-spec limit", () => {
  const entries = Array.from({ length: 250 }, (_, i) => ({ index: 11 + (i % 64), led: { mode: "static", colour: palette(1) } as Led }));
  const msgs = ledLightingSysex(PRO_MK3, entries);
  expect(msgs.length).toBe(3); // 106 + 106 + 38
  for (const m of msgs) {
    expect(m[0]).toBe(0xf0);
    expect(m[m.length - 1]).toBe(0xf7);
  }
});

test("RGB encodes as lighting type 3 with 7-bit components", () => {
  expect(colourSpec(11, { mode: "static", colour: rgb(127, 0, 64) })).toEqual([0x03, 11, 127, 0, 64]);
  expect(rgb(999, -5, 20.6)).toEqual({ kind: "rgb", r: 127, g: 0, b: 21 }); // clamped + rounded
});

test("RGB has no short form, and is refused for flash and pulse", () => {
  const rgbStatic: Led = { mode: "static", colour: rgb(127, 0, 0) };
  expect(needsSysex(rgbStatic)).toBe(true);
  expect(lightMessage(11, rgbStatic)).toBe(null); // must go via SysEx
  expect(isRenderable(rgbStatic)).toBe(true);

  // Lighting types 1 and 2 carry palette indices only — an RGB flash/pulse is not expressible, so it is
  // rejected rather than quietly encoded as some other colour.
  expect(isRenderable({ mode: "pulse", colour: rgb(127, 0, 0) })).toBe(false);
  expect(isRenderable({ mode: "flash", colour: palette(1), alt: rgb(0, 127, 0) })).toBe(false);
  expect(isRenderable({ mode: "flash", colour: palette(1), alt: palette(2) })).toBe(true);
});

test("palette indices are clamped to the legal 7-bit range", () => {
  expect(palette(200)).toEqual({ kind: "palette", index: 127 });
  expect(palette(-3)).toEqual({ kind: "palette", index: 0 });
});

// --- mode and identification ---------------------------------------------------------------------

test("GOLDEN: mode and layout control messages", () => {
  expect(hex(enterProgrammerMode(PRO_MK3))).toBe("f0 00 20 29 02 0e 0e 01 f7");
  expect(hex(exitToLiveMode(PRO_MK3))).toBe("f0 00 20 29 02 0e 0e 00 f7");
  expect(hex(setStandaloneMode(PRO_MK3))).toBe("f0 00 20 29 02 0e 10 00 f7");
  expect(hex(selectLayout(PRO_MK3, PRO_MK3.programmerLayout))).toBe("f0 00 20 29 02 0e 00 11 00 00 f7");
});

test("GOLDEN: device inquiry, and parsing a Pro MK3's reply", () => {
  expect(hex(deviceInquiry())).toBe("f0 7e 7f 06 01 f7");

  // The manual's application-mode reply, with a made-up version field.
  const reply = [0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0xf7];
  const parsed = parseInquiryReply(reply)!;
  expect(parsed.family).toEqual([0x13, 0x01]); // the Pro MK3 in application mode
  expect(parsed.version).toEqual([0x01, 0x02, 0x03, 0x04]);

  expect(parseInquiryReply([0xf0, 0x7e, 0x00, 0x06, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0, 0, 0, 0, 0, 0, 0xf7])).toBe(null); // not Novation
  expect(parseInquiryReply([0xf0, 0x7e])).toBe(null); // too short
});

test("the profile carries the model facts the rest of the module reads", () => {
  expect(PRO_MK3.sysexId).toBe(0x0e); // Mini MK3 is 0x0D, Launchpad X 0x0C
  expect(PRO_MK3.maxColourSpecs).toBe(106);
  expect(PRO_MK3.portHint).toBe("LPProMK3 MIDI"); // NOT the DAW port — programmer traffic lives here
});

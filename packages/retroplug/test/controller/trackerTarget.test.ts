// The launch encoder, checked against the decoder that actually ships.
//
// The strongest available oracle here is not a hex literal: it is `midiMapRow` from dspRoles.ts, the
// function a real cart's row bytes are produced by. If every row survives encode -> decode we are
// speaking the protocol the role consumes, rather than a second opinion about it that happens to look
// similar.
import { test, expect } from "../../testing/harness";
import { lsdjMidiMapTarget, launchMessage, nullTarget, LSDJ_MAX_ROW } from "../../src/controller";
import { midiMapRow } from "../../src/dspRoles";

const collect = () => {
  const sent: number[][] = [];
  return { sent, target: lsdjMidiMapTarget((d) => sent.push(d)) };
};

test("every launchable row survives a round trip through the role's own decoder", () => {
  for (let row = 0; row <= LSDJ_MAX_ROW; row++) {
    const m = launchMessage(row)!;
    expect(midiMapRow(m[0] & 0x0f, m[1])).toBe(row);
  }
});

test("rows split across the two MIDI channels the way Arduinoboy does", () => {
  expect(launchMessage(0)).toEqual([0x90, 0, 100]); // ch1
  expect(launchMessage(127)).toEqual([0x90, 127, 100]);
  expect(launchMessage(128)).toEqual([0x91, 0, 100]); // ch2 carries row - 128
  expect(launchMessage(253)).toEqual([0x91, 125, 100]);
});

test("a release is a NoteOff on the same channel as its launch", () => {
  expect(launchMessage(5, false)).toEqual([0x80, 5, 0]);
  expect(launchMessage(200, false)).toEqual([0x81, 72, 0]);
});

test("rows 254 and 255 are refused - they are the protocol's own sentinels", () => {
  // 0xFE is the NoteOff handshake and 0xFF the clock, so those rows cannot be addressed at all (B7).
  expect(launchMessage(254)).toBe(null);
  expect(launchMessage(255)).toBe(null);
  expect(LSDJ_MAX_ROW).toBe(253);
});

test("an out-of-range row sends nothing rather than launching a different one", () => {
  const { sent, target } = collect();
  target.launch(-1);
  target.launch(254);
  target.launch(1000);
  target.launch(3.5);
  expect(sent.length).toBe(0); // silence is a far smaller surprise than launching the wrong section
});

test("the target forwards launches and releases to its sink", () => {
  const { sent, target } = collect();
  target.launch(7);
  target.release(7);
  expect(sent).toEqual([[0x90, 7, 100], [0x80, 7, 0]]);
  expect(target.maxRow).toBe(LSDJ_MAX_ROW);
});

test("the null target swallows everything without throwing", () => {
  nullTarget.launch(5);
  nullTarget.release(5);
  expect(nullTarget.maxRow).toBe(LSDJ_MAX_ROW);
});

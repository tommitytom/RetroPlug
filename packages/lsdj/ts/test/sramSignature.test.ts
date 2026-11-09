// LSDj's meaningful-change signature over a battery (docs/modular-graph-phase-f4a-design.md section 4.3): the shell's
// loose-`.sav` auto-save asks each tracker integration for one and falls back to a whole-image hash, so this is what an
// LSDj image answers with, and null is how it hands any other image on.
import { test, expect } from "@retroplug/testing";
import { savFrom } from "../index";
import { lsdjSramSignature } from "../lsdjSramSignature";

const WORK_HOURS = 0x3fb2; // a ticking work-clock byte: LSDj rewrites it every frame; NOT modeled
const TEMPO = 0x3fb4; // a meaningful, modeled byte

test("lsdjSramSignature ignores the ticking work-clock but catches a real edit", () => {
  const orig = savFrom({ workingSong: { settings: { tempo: 150 } } });
  expect(orig.length).toBe(0x20000);

  // A frame tick bumps the work clock (and its checksum) but changes nothing meaningful.
  const ticked = orig.slice();
  ticked[WORK_HOURS] = (ticked[WORK_HOURS] + 7) & 0xff;
  ticked[0x3fb9] = (ticked[0x3fb9] + 7) & 0xff; // totalTimeChecksum
  expect(lsdjSramSignature(ticked)).toBe(lsdjSramSignature(orig)); // same signature
  expect(ticked[WORK_HOURS] !== orig[WORK_HOURS]).toBe(true); // but the raw bytes differ

  // A tempo change IS meaningful (a modeled field) → the signature changes.
  const edited = orig.slice();
  edited[TEMPO] = 90;
  expect(lsdjSramSignature(edited) === lsdjSramSignature(orig)).toBeFalsy();
});

test("lsdjSramSignature answers null for an image that is not a full LSDj one", () => {
  expect(lsdjSramSignature(new Uint8Array([1, 2, 3, 4]))).toBe(null); // far too short
  // A full-size image without the 'jk' SRAM-init magic is some other cart's: the next integration, or the hash, takes it.
  expect(lsdjSramSignature(new Uint8Array(0x20000))).toBe(null);
});

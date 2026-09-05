// The three envelope fields on a REAL NES core. `envelopeVolume` is the $4000 low nibble as written, which
// in hardware-envelope mode is the DECAY PERIOD - so a ROM wanting the fastest decay writes 0 and the field
// reads 0 while the note is plainly audible (BlipToaster's HARNESS-NOTES 3.3). `envelopeLevel` is the
// envelope unit's live decay counter and `envelopeOutput` what the mixer actually uses. This pins the
// identity between them on a sounding note and after its release, and that the noise channel carries the
// same three fields.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

/** The mixer's effective level, derived the way the hardware does it. */
const effective = (c: ApuState["pulse1"] | ApuState["noise"]) =>
  c.lengthCounter > 0 ? (c.constantVolume ? c.envelopeVolume : c.envelopeLevel) : 0;

test("envelopeOutput is the level the mixer uses, on a held pulse note and after its release", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  let held: ApuState | null = null, released: ApuState | null = null;
  const tl = new Timeline()
    .noteOn(200, 60, { channel: 1, velocity: 127 })
    .at(500, (sess) => (held = sess.backend.getApuState(id)))
    .noteOff(600, 60, { channel: 1 })
    .at(900, (sess) => (released = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 1000, warmupMs: 1000 });
  s.project.systems.removeSystem(id);

  const p = held!.pulse1;
  expect(p.period, "pulse1 period while held").toBeGreaterThan(0);
  expect(p.envelopeOutput, "pulse1 effective level while held").toBeGreaterThan(0);
  expect(p.envelopeOutput, "pulse1 output identity").toBe(effective(p));
  expect(p.envelopeLevel).toBeLessThanOrEqual(15);
  expect(typeof p.envelopeLoop).toBe("boolean");

  // Released: whatever the ROM does (a zero-volume write, or letting the length counter run out), the
  // mixer level is 0 and the identity still holds.
  const r = released!.pulse1;
  expect(r.envelopeOutput, "pulse1 effective level after note-off").toBe(0);
  expect(r.envelopeOutput).toBe(effective(r));

  // The noise channel carries the same three fields and the same identity, idle or not.
  const n = held!.noise;
  expect(n.envelopeOutput).toBe(effective(n));
  expect(n.envelopeLevel).toBeLessThanOrEqual(15);
});

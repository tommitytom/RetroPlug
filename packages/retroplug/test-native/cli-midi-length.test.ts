// A MIDI run longer than 4 bytes reaches a REAL NES core intact. Before this, the command ring carried a
// message inline in 4 bytes and stageMidiIn refused anything longer - and Timeline.midi ignored that refusal,
// so a 6-byte array or a 5-byte SysEx vanished with no error (BlipToaster's HARNESS-NOTES 1.1). Now the ring
// carries a long run as an owning payload and the Engine hands it to the core's raw byte path (the N8 FIFO).
//
// The proof uses two ch1 note-ons staged as ONE 6-byte array: delivered, the SECOND note-on wins and pulse1
// sits at note 64's period; dropped whole, pulse1 stays silent. (ch1 -> pulse1 is proven by cli-observe.)
// A second case sends a 5-byte SysEx immediately followed by a note in the same run - the note still plays,
// so the whole run arrived in order and the ROM's parser carried on past the F7.
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import type { ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

function pulse1After(build: (tl: Timeline) => void): ApuState["pulse1"] {
  const s = bootSession();
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");
  let apu: ApuState | null = null;
  const tl = new Timeline();
  build(tl);
  tl.at(600, (sess) => (apu = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 700, warmupMs: 1000 });
  s.project.systems.removeSystem(id);
  return apu!.pulse1;
}

test("a 6-byte run (two note-ons in one array) is delivered whole: the last note-on wins on pulse1", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  // Reference: note 64 alone, staged as an ordinary 3-byte message.
  const ref = pulse1After((tl) => tl.midi(200, [0x90, 64, 0x7f]));
  expect(ref.period).toBeGreaterThan(0);
  // The same note-on as the tail of a 6-byte run behind a note 60 on.
  const run = pulse1After((tl) => tl.midi(200, [0x90, 60, 0x7f, 0x90, 64, 0x7f]));
  expect(run.period).toBe(ref.period);
  expect(run.envelopeVolume).toBeGreaterThan(0);
});

test("a 5-byte SysEx followed by a note in the same run arrives in order: the note plays", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const ref = pulse1After((tl) => tl.midi(200, [0x90, 64, 0x7f]));
  const run = pulse1After((tl) => tl.midi(200, [0xf0, 0x7d, 0x42, 0x02, 0xf7, 0x90, 64, 0x7f]));
  expect(run.period).toBe(ref.period);
});

test("stageMidiIn refuses only an empty message, and Timeline turns a refusal into an error", () => {
  const s = bootSession();
  expect(s.audio.stageMidiIn([])).toBe(false);
  expect(s.audio.stageMidiIn([0xf0, 0x7d, 0x42, 0x01, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 0xf7])).toBe(true);
  expect(() => new Timeline().midi(0, [])).toThrow("non-empty");
});

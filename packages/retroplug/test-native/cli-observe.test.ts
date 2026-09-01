// The live-core debug reads (getApuState / getCpuRegisters / readCpu / readMemory) against a REAL Mesen
// NES core, driven through the CLI session + Timeline. Proves the spec/09 observe surface end-to-end: a
// scheduled ch1 MIDI note sounds on pulse1 at ~C4, the CPU exposes a pc register, a side-effect-free peek
// reads the $40F1 MIDI FIFO, and a whole RAM region reads back. (The pure Timeline.build ordering is in
// the mock test/cli/timeline.test.ts.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { MemoryRegion, type ApuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/bliptoaster.nes";

test("debug reads observe a real NES: APU pulse1, CPU pc, FIFO peek, RAM region", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Capture the APU mid-note via Timeline.at (render advances to 400ms, then the callback reads state).
  let apu: ApuState | null = null;
  const tl = new Timeline()
    .note(200, 60, { channel: 1, durationMs: 400 })
    .at(400, (sess) => (apu = sess.backend.getApuState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1000 });

  // APU: the ch1 note sounds on pulse1 near C4 (261.6 Hz); pulse2 silent.
  expect(apu != null).toBeTruthy();
  expect(apu!.pulse1.period > 0 && apu!.pulse1.envelopeVolume > 0).toBeTruthy();
  expect(apu!.pulse1.frequency > 200 && apu!.pulse1.frequency < 320).toBeTruthy();
  expect(apu!.pulse2.envelopeVolume === 0).toBeTruthy();

  // CPU registers include a 16-bit pc; a side-effect-free peek returns a byte; a whole region reads back.
  const pc = s.backend.getCpuRegisters(id).find((r) => r.name === "pc");
  expect(pc != null && pc.bits === 16).toBeTruthy();
  expect(typeof s.backend.readCpu(id, 0x40f1) === "number").toBeTruthy();
  expect((s.backend.readMemory(id, MemoryRegion.Ram)?.length ?? 0) === 2048).toBeTruthy(); // 2 KB NES RAM
});

// getPpuState against a REAL Mesen NES core, driven through the CLI session + Timeline. Rendering audio
// advances the PPU, so after a warmup+render the snapshot reports a non-zero frame count and a dot
// position within the valid NES scanline/cycle ranges. Also covers the 32-byte palette RAM
// ($3F00-$3F1F). (The tilemap/sprite viewers are still out of scope.)
import { test, expect } from "../testing/harness";
import { bootSession } from "../cli/session";
import { Timeline, renderTimeline } from "../cli/timeline";
import { type PpuState } from "../src/backend";

declare const __REPO_RESOURCES_DIR__: string;
const NES = __REPO_RESOURCES_DIR__ + "/roms/n8-midi.nes";

test("getPpuState reads a real NES PPU: frameCount advances, scanline/cycle in range", () => {
  const s = bootSession();
  if (!s.backend.fileExists(NES)) {
    console.log("# SKIP: no NES rom");
    return;
  }
  const id = s.project.systems.addSystem(NES);
  if (id == null) throw new Error("addSystem failed");

  // Advance the emulator a few frames, then snapshot the PPU state at the end of the window.
  let ppu: PpuState | null = null;
  const tl = new Timeline().at(400, (sess) => (ppu = sess.backend.getPpuState(id)));
  renderTimeline(s, tl, { durationMs: 800, warmupMs: 1000 });

  expect(ppu != null).toBeTruthy();
  // The PPU has rendered frames, so frameCount is non-zero.
  expect(ppu!.frameCount > 0).toBeTruthy();
  // Dot position within valid NES ranges: scanline -1..260, cycle 0..340.
  expect(ppu!.scanline >= -1 && ppu!.scanline <= 260).toBeTruthy();
  expect(ppu!.cycle <= 340).toBeTruthy();
  // The reconstructed register bytes are single bytes.
  expect(ppu!.control >= 0 && ppu!.control <= 0xff).toBeTruthy();
  expect(ppu!.mask >= 0 && ppu!.mask <= 0xff).toBeTruthy();
  expect(ppu!.status >= 0 && ppu!.status <= 0xff).toBeTruthy();
  expect(typeof ppu!.writeToggle === "boolean").toBeTruthy();
  // The 32-byte palette RAM is exposed; entry 0 (the universal background) is a 6-bit NES index.
  expect(ppu!.paletteRam.length).toBe(32);
  expect(ppu!.paletteRam[0] <= 0x3f).toBeTruthy();
});

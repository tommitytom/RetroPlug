// Capture surface: framebuffer pixels, PNG screenshot, and mixed audio.

import { test, expect, emu } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";

test("getFrame returns a 160x144 published, non-uniform frame after boot", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  const f = emu.getFrame(sys);
  expect(f.width).toBe(160);
  expect(f.height).toBe(144);
  expect(f.published).toBeTruthy();
  expect(f.pixels.length).toBe(160 * 144 * 4);

  // A booted LSDJ screen is not a single flat colour.
  let distinct = false;
  for (let i = 4; i < f.pixels.length; i += 4) {
    if (f.pixels[i] !== f.pixels[0] || f.pixels[i + 1] !== f.pixels[1] ||
        f.pixels[i + 2] !== f.pixels[2]) { distinct = true; break; }
  }
  expect(distinct).toBeTruthy();
});

test("screenshot writes a PNG", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  expect(emu.screenshot(sys, "/tmp/harness_capture.png")).toBeTruthy();
});

test("getAudio returns interleaved stereo samples", () => {
  const sys = emu.loadRom(LSDJ);
  emu.runMs(2500);
  const audio = emu.getAudio(100); // 100ms
  // 100ms * 44100 * 2 channels = 8820 samples.
  expect(audio.length).toBe(Math.round(0.1 * 44100) * 2);
});

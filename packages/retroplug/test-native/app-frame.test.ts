// The video-frame path, end to end through the native backend: construct a real system via the STORES
// (loadMgb → constructSystem), advance the core with renderAudio, then read its framebuffer back over
// the new getFrame RPC. Proves the per-system FrameBufferTriple escapes to TS as raw XRGB8888 pixels —
// the data an EmulatorTile blits into an LVGL Canvas. (Frames are produced by the core rendering vblanks
// during processBlock, independent of the DSP kernel, so no kernel is wired here.)
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { RecentStore } from "../src/recentStore";
import { ProjectStore } from "../src/projectStore";
import { buildAppRegistry } from "../src/appHost";

test("getFrame returns a live per-system framebuffer once the core has rendered", () => {
  const be = createRealBackend();
  const registry = buildAppRegistry();
  const recent = new RecentStore(be);
  const project = new ProjectStore(be, recent, registry);
  const audio = createAudioDriver();

  const id = project.systems.loadMgb()!;
  expect(typeof id).toBe("number");

  // Before the core is advanced the triple buffer has published nothing: dimensions are known, but the
  // frame is not published (and carries no pixels).
  const cold = be.getFrame(id);
  expect(cold != null).toBeTruthy();
  expect(cold!.width).toBe(160);
  expect(cold!.height).toBe(144);
  expect(cold!.published).toBeFalsy();
  expect(cold!.pixels.length).toBe(0);

  // Advance the emulator (GB boot → mGB screen renders vblanks → the triple buffer publishes).
  audio.renderAudio(500);

  const frame = be.getFrame(id);
  expect(frame != null).toBeTruthy();
  expect(frame!.width).toBe(160);
  expect(frame!.height).toBe(144);
  expect(frame!.published).toBeTruthy();
  expect(frame!.pixels.length).toBe(160 * 144 * 4); // XRGB8888, 4 bytes/pixel

  // A real rendered screen isn't a single flat colour (boot logo / mGB UI has variation).
  const px = frame!.pixels;
  let varied = false;
  for (let i = 4; i < px.length; i += 4) {
    if (px[i] !== px[0] || px[i + 1] !== px[1] || px[i + 2] !== px[2]) {
      varied = true;
      break;
    }
  }
  expect(varied).toBeTruthy();

  // An unknown id is null (no such system).
  expect(be.getFrame(999999)).toBe(null);
});

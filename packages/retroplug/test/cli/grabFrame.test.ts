// The pure parts of `retroplug-cli grab-frame`: the argument defaults, the ffmpeg command line, and the
// black-frame verdict that tells the capture card's warm-up apart from a real screen.
import { test, expect } from "../../testing/harness";
import { parseGrabArgs, ffmpegArgs, frameStats, isBlack } from "../../cli/sessions/grab-frame";

test("parseGrabArgs: the lab card's defaults, and every flag", () => {
  expect(parseGrabArgs(["/tmp/nes.png"])).toEqual({
    out: "/tmp/nes.png", device: "/dev/video0", size: "720x576", standard: "PAL", frames: 30, retries: 2, failOnBlack: false,
  });
  const o = parseGrabArgs(["--device", "/dev/video1", "--frames", "5", "--retries", "0", "--standard", "NTSC", "--size", "720x480", "--fail-on-black", "out.png"]);
  expect(o).toEqual({ out: "out.png", device: "/dev/video1", size: "720x480", standard: "NTSC", frames: 5, retries: 0, failOnBlack: true });
  expect(parseGrabArgs([]).out).toBe("");
  expect(() => parseGrabArgs(["--frames", "0", "x.png"])).toThrow("--frames");
});

test("ffmpegArgs keeps only the last of the run (-frames:v N -update 1) and requests the standard", () => {
  expect(ffmpegArgs({ out: "/tmp/nes.png", device: "/dev/video0", size: "720x576", standard: "PAL", frames: 30 })).toEqual([
    "ffmpeg", "-y", "-loglevel", "error",
    "-f", "v4l2", "-standard", "PAL", "-input_format", "yuyv422", "-video_size", "720x576", "-i", "/dev/video0",
    "-frames:v", "30", "-update", "1", "/tmp/nes.png",
  ]);
});

test("frameStats / isBlack: a flat frame at the card's black level is black, a screen with content is not", () => {
  const w = 8, h = 4;
  const flat = new Uint8Array(w * h * 4).fill(16); // every pixel luma 16, what the card returns while warming up
  const fs = frameStats(flat, w, h);
  expect(fs.blackFraction).toBe(1);
  expect(fs.meanLuma).toBeCloseTo(16, 0.01);
  expect(isBlack(fs)).toBeTruthy();

  // A mostly-black game screen with a few lit pixels: NOT flat black.
  const game = new Uint8Array(w * h * 4).fill(16);
  for (const px of [3, 11, 20]) game.set([200, 200, 200, 255], px * 4);
  const gs = frameStats(game, w, h);
  expect(gs.blackFraction, "black fraction with 3 lit pixels of 32").toBeCloseTo(29 / 32, 1e-6);
  expect(isBlack(gs)).toBeFalsy();

  expect(isBlack(frameStats(new Uint8Array(0), 0, 0))).toBeTruthy(); // an empty image is nothing to look at
});

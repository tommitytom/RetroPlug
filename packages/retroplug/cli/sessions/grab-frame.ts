// `retroplug-cli grab-frame` - one frame of the NES video off the USB capture card, as a PNG, with the
// card's warm-up handled.
//
// The capture card's first frames after the device is opened (and for a while after a power-cycle) come
// back flat black - every pixel at luma 16 - with no error, which is indistinguishable from a console
// showing nothing. Three "black screen" boots in one hardware session were this artefact. So this runs
// ffmpeg for a run of frames keeping only the last (`-frames:v N -update 1`), then DECODES the PNG it wrote
// and measures it: a flat-black result is retried with a longer run, and reported as such rather than
// handed back as if it were the screen. ffmpeg is spawned (it is on the lab image); this owns the
// arguments, the retry and the verdict.
import type { CliTool } from "../tools";
import type { Session } from "../session";
import { keepAlive, exitProcess } from "../session";

declare const tjs: {
  spawn(args: string[], options?: { stdin?: string; stdout?: string; stderr?: string }): {
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

export interface GrabOpts {
  out: string;
  device: string;
  size: string;
  standard: string;
  frames: number;
  retries: number;
  failOnBlack: boolean;
}

const flag = (args: string[], name: string): string | undefined => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : undefined;
};

/** Parse `grab-frame`'s arguments. Pure. The defaults are the lab's card: PAL, 720x576, 30 frames. */
export function parseGrabArgs(args: string[]): GrabOpts {
  const VALUE_FLAGS = new Set(["--device", "--size", "--standard", "--frames", "--retries"]);
  let out = "";
  for (let i = 0; i < args.length; i++) {
    if (args[i].startsWith("--")) {
      if (VALUE_FLAGS.has(args[i])) i++;
      continue;
    }
    out = args[i];
    break;
  }
  const frames = Number(flag(args, "--frames") ?? 30);
  const retries = Number(flag(args, "--retries") ?? 2);
  if (!Number.isInteger(frames) || frames < 1) throw new Error("--frames: expected a positive integer");
  if (!Number.isInteger(retries) || retries < 0) throw new Error("--retries: expected a non-negative integer");
  return {
    out,
    device: flag(args, "--device") ?? "/dev/video0",
    size: flag(args, "--size") ?? "720x576",
    standard: flag(args, "--standard") ?? "PAL",
    frames,
    retries,
    failOnBlack: args.includes("--fail-on-black"),
  };
}

/** The ffmpeg command line: grab `frames` frames and keep overwriting `out`, so only the last survives. */
export function ffmpegArgs(o: Pick<GrabOpts, "out" | "device" | "size" | "standard" | "frames">): string[] {
  return [
    "ffmpeg", "-y", "-loglevel", "error",
    "-f", "v4l2", "-standard", o.standard, "-input_format", "yuyv422", "-video_size", o.size, "-i", o.device,
    "-frames:v", String(o.frames), "-update", "1", o.out,
  ];
}

/** Mean luma (0-255) and the fraction of pixels at or under the card's black level (luma 16, with a little
 *  room for noise). A real screen - even a mostly-black game with a few characters - has a smaller black
 *  fraction than a flat frame; the N8 menu and BlipToaster's monitor are far from it. */
export function frameStats(rgba: Uint8Array, width: number, height: number): { meanLuma: number; blackFraction: number } {
  const n = width * height;
  if (n === 0) return { meanLuma: 0, blackFraction: 1 };
  let sum = 0, black = 0;
  for (let i = 0; i < n; i++) {
    const y = 0.299 * rgba[i * 4] + 0.587 * rgba[i * 4 + 1] + 0.114 * rgba[i * 4 + 2];
    sum += y;
    if (y <= 24) black++;
  }
  return { meanLuma: sum / n, blackFraction: black / n };
}

/** Flat black: (almost) every pixel at the black level. */
export const isBlack = (st: { blackFraction: number }): boolean => st.blackFraction > 0.98;

async function grab(s: Session, o: GrabOpts): Promise<number> {
  let frames = o.frames;
  for (let attempt = 0; attempt <= o.retries; attempt++) {
    const argv = ffmpegArgs({ ...o, frames });
    let status: { exit_status: number; term_signal: string | null };
    try {
      status = await tjs.spawn(argv, { stdin: "ignore" }).wait();
    } catch (e) {
      console.error(`grab-frame: could not run ffmpeg (${(e as Error).message ?? e}); is it installed?`);
      return 1;
    }
    if (status.term_signal || status.exit_status !== 0) {
      console.error(`grab-frame: ffmpeg failed (exit ${status.term_signal ?? status.exit_status}); is ${o.device} the capture card?`);
      return 1;
    }
    const png = s.backend.readFile(o.out);
    const img = png ? s.backend.pngDecode(png) : null;
    if (!img) {
      console.error(`grab-frame: ffmpeg exited 0 but ${o.out} is not a readable PNG`);
      return 1;
    }
    const st = frameStats(img.rgba, img.width, img.height);
    if (!isBlack(st)) {
      console.log(`wrote ${o.out} (${img.width}x${img.height}, mean luma ${st.meanLuma.toFixed(1)}, ${(st.blackFraction * 100).toFixed(0)}% black, after ${frames} frames)`);
      return 0;
    }
    if (attempt < o.retries) {
      frames *= 2;
      console.log(`frame ${attempt + 1} is flat black (the card's warm-up, or nothing on screen); retrying with ${frames} frames`);
    } else {
      console.log(
        `WARNING: ${o.out} is flat black after ${frames} frames (mean luma ${st.meanLuma.toFixed(1)}). ` +
          "Either the console shows nothing, or the card is still warming up (it also does this for a while after a power-cycle).",
      );
      return o.failOnBlack ? 1 : 0;
    }
  }
  return 1;
}

export const grabFrameTool: CliTool = {
  name: "grab-frame",
  summary: "grab one NES video frame off the USB capture card as a PNG (warm-up frames discarded)",
  help: [
    "usage: retroplug-cli grab-frame <out.png> [--device /dev/video0] [--size 720x576] [--standard PAL]",
    "                                [--frames 30] [--retries 2] [--fail-on-black]",
    "",
    "  Runs ffmpeg for a run of frames, keeps the LAST one, decodes it and measures it. The capture",
    "  card's first frames after it is opened (and for a while after a power-cycle) are flat black with",
    "  no error - indistinguishable from a console showing nothing - so a black result is retried with",
    "  a longer run and, if it stays black, reported as such instead of handed back as the screen.",
    "",
    "options:",
    "  --device <node>    the V4L2 capture node (default /dev/video0)",
    "  --size WxH         the capture size (default 720x576, the card's PAL mode; NTSC is 720x480)",
    "  --standard STD     the video standard to request (default PAL)",
    "  --frames N         frames to grab before keeping one (default 30 - ~1.2 s, past the warm-up)",
    "  --retries N        how many times to double N on a black frame (default 2)",
    "  --fail-on-black    exit 1 when the frame is still black after the retries (default: exit 0 + warn)",
    "",
    "example:",
    "  retroplug-cli grab-frame /tmp/nes.png && <read /tmp/nes.png>",
  ].join("\n"),
  longRunning: true, // waits on the ffmpeg child
  run(s: Session, args: string[]): void {
    const o = parseGrabArgs(args);
    if (!o.out) {
      console.error("retroplug-cli grab-frame: missing <out.png>\n\n" + grabFrameTool.help);
      exitProcess(2);
      return;
    }
    keepAlive();
    void grab(s, o).then(exitProcess);
  },
};

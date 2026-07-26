// Shared render library — the request/context contracts. Kept free of any host wiring so both the CLI
// `render` command and the background/UI render worker construct the same shapes. The CLI's `Session`
// structurally satisfies `RenderContext`; the worker builds an equivalent context over its own control plane.

import type { Backend } from "../backend";
import type { ProjectStore } from "../projectStore";
import type { DspRuntimeClient } from "../dspRuntime";
import type { AudioDriver } from "../audioDriver";

export type SplitMode = "mix" | "channels" | "pins";

export type Platform = "gb" | "nes" | "gba" | "other";

/** Target-exists policy for a render: clobber the file, or write to the next free "<name>_N". */
export type OnExists = "overwrite" | "rename";

/** A fully-resolved render request. The CLI's parseRenderArgs produces one; the worker builds one from a
 *  job spec. Paths (`rom`/`sav`/`state`/`out`) are whatever the host can open — real files for the CLI,
 *  host-provided paths for the worker. */
export interface RenderOpts {
  rom: string;
  sav?: string;
  state?: string;
  out?: string;
  durationMs?: number; // explicit fixed duration (ms); undefined = auto (LSDj: render to the HFF stop) / default
  maxDurationMs: number; // safety cap for LSDj length auto-detect (no-HFF fallback), ms
  sampleRate?: number; // host render rate (Hz); undefined = engine default (44100). Must be set pre-build.
  split: SplitMode;
  onExists?: OnExists; // when the target file exists: "overwrite" (default) or "rename" to the next free name
  bpm?: number;
  transport: boolean;
  start: boolean; // auto-start playback on boot (press Start); default true
  // LSDj song selection (GB only). A sav holds up to 32 named projects but only plays its working song;
  // these promote a chosen project to the working song before boot. song / songIndex are exclusive.
  song?: string; // by name (case-insensitive, ≤8 chars)
  songIndex?: number; // by slot 0–31
  listSongs: boolean; // (CLI) print the sav's song names and exit — not a render; handled by the CLI wrapper
}

/** The subset of a booted control plane the render orchestration drives. `Session` (cli/session.ts)
 *  satisfies this structurally; the worker constructs the same fields over its own backend. */
export interface RenderContext {
  backend: Backend; // readFile / readCpu + the WavBackend writers (writeFile / appendFile / writeFileAt)
  project: ProjectStore;
  dsp: DspRuntimeClient;
  audio: AudioDriver;
}

/** Host callbacks. All optional so the CLI passes none (logging defaults to console, no progress/cancel).
 *  The worker wires onProgress/isCancelled to native thunks and routes log/warn to the host. */
export interface RenderHooks {
  onProgress?(fraction: number): void; // 0..1, called per rendered chunk (auto-detect reports elapsed/cap)
  isCancelled?(): boolean; // polled per chunk; when true the render aborts with RenderCancelled
  log?(msg: string): void; // informational output (defaults to console.log)
  warn?(msg: string): void; // warnings (defaults to console.warn)
}

/** What a completed render produced. */
export interface RenderResult {
  outputs: string[]; // the WAV file paths written (1 for mix, N for split)
  lengthMs?: number; // detected song length (auto-detect only)
  frames?: number; // frames committed between the start and stop markers (auto-detect only)
  hff?: boolean; // an HFF stop was detected (vs. hitting the max-duration cap)
}

/** Thrown by the render loop when hooks.isCancelled() returns true. The worker distinguishes it to report
 *  a cancelled (not failed) job and clean up any partial output. */
export class RenderCancelled extends Error {
  constructor() {
    super("render: cancelled");
    this.name = "RenderCancelled";
  }
}

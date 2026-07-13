// Flag parser for the `retroplug-cli render` subcommand — kept pure (no Backend, no globals) so it is
// unit-testable in the mock suite. render.ts imports parseRenderArgs and drives the render off the result.
//
//   render <rom> [--sav f] [--state f] [--out f] [--ms n] [--split mix|channels|pins|mono]
//                [--bpm n] [--transport] [--no-start] [--song name | --song-index n] [--list-songs]

export type SplitMode = "mix" | "channels" | "pins" | "mono";

const SPLIT_MODES: readonly SplitMode[] = ["mix", "channels", "pins", "mono"];

export interface RenderOpts {
  rom: string;
  sav?: string;
  state?: string;
  out?: string;
  ms?: number; // explicit fixed duration; undefined = auto (LSDj: render to the HFF stop) / 8000 default
  maxMs: number; // safety cap for LSDj length auto-detect (no-HFF fallback)
  sampleRate?: number; // host render rate (Hz); undefined = engine default (44100). Must be set pre-build.
  split: SplitMode;
  bpm?: number;
  transport: boolean;
  start: boolean; // auto-start playback on boot (press Start); default true
  // LSDj song selection (GB only). A sav holds up to 32 named projects but only plays its working song;
  // these promote a chosen project to the working song before boot. song / songIndex are exclusive.
  song?: string; // by name (case-insensitive, ≤8 chars)
  songIndex?: number; // by slot 0–31
  listSongs: boolean; // print the sav's song names and exit
}

export const RENDER_USAGE =
  "usage: render <rom> [--sav f] [--state f] [--out f] [--ms n] [--max-ms n] [--sample-rate hz] " +
  "[--split mix|channels|pins|mono] [--bpm n] [--transport] [--no-start] " +
  "[--song name | --song-index n] [--list-songs]";

function intValue(flag: string, raw: string | undefined): number {
  const n = Number(raw);
  if (raw === undefined || !Number.isFinite(n) || !Number.isInteger(n) || n <= 0)
    throw new Error(`render: ${flag} needs a positive integer (got ${raw ?? "nothing"})`);
  return n;
}

/** Parse a render argv (the tokens AFTER the `render` subcommand). Throws on bad usage. */
export function parseRenderArgs(argv: string[]): RenderOpts {
  let rom: string | undefined;
  let sav: string | undefined;
  let state: string | undefined;
  let out: string | undefined;
  let ms: number | undefined;
  let maxMs = 300000; // 5 min default cap for LSDj length auto-detect
  let sampleRate: number | undefined;
  let split: SplitMode = "mix";
  let bpm: number | undefined;
  let transport = false;
  let start = true;
  let song: string | undefined;
  let songIndex: number | undefined;
  let listSongs = false;

  // A flag's value is the next token; missing → error via undefined feeding the validators.
  const next = (i: number, flag: string): string => {
    const v = argv[i + 1];
    if (v === undefined) throw new Error(`render: ${flag} needs a value`);
    return v;
  };

  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    switch (a) {
      case "--sav": sav = next(i, a); i++; break;
      case "--state": state = next(i, a); i++; break;
      case "--out": out = next(i, a); i++; break;
      case "--ms": ms = intValue(a, next(i, a)); i++; break;
      case "--max-ms": maxMs = intValue(a, next(i, a)); i++; break;
      case "--sample-rate": sampleRate = intValue(a, next(i, a)); i++; break;
      case "--bpm": bpm = intValue(a, next(i, a)); i++; break;
      case "--split": {
        const v = next(i, a); i++;
        if (!SPLIT_MODES.includes(v as SplitMode))
          throw new Error(`render: --split must be one of ${SPLIT_MODES.join("|")} (got ${v})`);
        split = v as SplitMode;
        break;
      }
      case "--song": song = next(i, a); i++; break;
      case "--song-index": {
        const raw = next(i, a); i++;
        const n = Number(raw);
        if (!Number.isInteger(n) || n < 0 || n > 31)
          throw new Error(`render: --song-index must be 0–31 (got ${raw})`);
        songIndex = n;
        break;
      }
      case "--list-songs": listSongs = true; break;
      case "--transport": transport = true; break;
      case "--start": start = true; break;
      case "--no-start": start = false; break;
      default:
        if (a.startsWith("--")) throw new Error(`render: unknown flag ${a}\n${RENDER_USAGE}`);
        if (rom !== undefined) throw new Error(`render: unexpected extra argument ${a}\n${RENDER_USAGE}`);
        rom = a;
    }
  }

  if (!rom) throw new Error(`render: missing <rom>\n${RENDER_USAGE}`);
  if (song !== undefined && songIndex !== undefined)
    throw new Error("render: --song and --song-index are mutually exclusive");
  return { rom, sav, state, out, ms, maxMs, sampleRate, split, bpm, transport, start, song, songIndex, listSongs };
}

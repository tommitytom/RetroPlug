// Flag parser for the `retroplug-cli render` subcommand — kept pure (no Backend, no globals) so it is
// unit-testable in the mock suite. render.ts imports parseRenderArgs and drives the render off the result.
//
//   render <rom> [--sav f] [--state f] [--out f] [--duration t] [--split mix|channels|pins]
//                [--bpm n] [--transport] [--no-start] [--song name | --song-index n] [--list-songs]

// The render request contract (RenderOpts) + SplitMode live in the shared render library; parseRenderArgs
// is the CLI-only front that produces a RenderOpts from argv. Re-exported so existing importers/tests keep
// resolving them from here.
import { type RenderOpts, type SplitMode } from "../src/render/types";
export type { RenderOpts, SplitMode };

const SPLIT_MODES: readonly SplitMode[] = ["mix", "channels", "pins"];

/** One-line summary for the top-level command index (CliTool.summary). */
export const RENDER_SUMMARY = "Render a ROM/SAV to a WAV file (full mix or per-channel stems)";

/** Short synopsis + pointer, appended to parse-error messages. */
const RENDER_HINT = "run 'retroplug-cli render --help' for the full options";

/** The detailed `render --help` text (CliTool.help). Explains every flag, its default, and its constraints. */
export const RENDER_HELP = `usage: retroplug-cli render <rom> [options]

Render a Game Boy (.gb/.gbc), NES (.nes) or GBA (.gba) ROM to a WAV file. The ROM is booted and its audio
is written to disk. For a saved LSDj / risa song, Start is pressed so it begins playing (use --no-start to
capture raw boot audio); mGB needs no such press, as it plays from incoming MIDI. With a loaded LSDj (Game
Boy) or risa (NES) sav the song length is auto-detected (rendered up to the HFF stop) unless you pin a fixed
length with --duration.

Durations accept a unit: 500ms, 3s, 2m (decimals ok, e.g. 1.5s); a bare number is milliseconds.

Arguments:
  <rom>                  Path to the ROM. A sibling <rom>.sav is loaded automatically if present.

Options:
  --sav <file>           Battery save (.sav) to load. Default: <rom>.sav next to the ROM, if it exists.
  --state <file>         Savestate to restore after boot (instead of a fresh boot).
  --out <file>           Output path. Default: the working/selected song's name for an LSDj/risa cart, else
                         the ROM name; <name>.wav for a mix, <name>_<channel>.wav for --split.
  --duration <time>      Fixed render length (e.g. 3s, 500ms, 2m). Turns OFF song-length auto-detection.
                         Default: auto for a loaded LSDj / risa sav, otherwise 5m.
  --max-duration <time>  Safety cap for song-length auto-detect when no HFF stop is found. Default: 5m.
  --sample-rate <hz>     Output sample rate. Default: 44100. Higher rates resample the console's audio up
                         (larger WAV, same song); must be set before the ROM boots (it always is here).
  --split <mode>         What to write (default: mix):
                           mix       one WAV of the final mix (Game Boy: stereo; NES: mono)
                           channels  one WAV per sound channel (Game Boy: 4 stereo stems;
                                     NES: 5 mono core channels: square1/2, triangle, noise, dmc)
                           pins      NES analog output pins: pulse, tnd, expansion (3 mono WAVs)
  --bpm <n>              Host tempo (BPM) for tempo-synced playback. Use with --transport.
  --transport            Run the host transport (play), so tempo-synced ROMs advance. Default: off.
  --no-start             Do NOT press Start on boot; render the raw boot/menu audio.
  --song <name>          LSDj / risa: promote a saved song to the working song by name (case-insensitive).
  --song-index <0-31>    LSDj / risa: promote a saved song by its slot number instead of by name.
  --list-songs           LSDj / risa: print the sav's saved song names and exit (renders nothing).
  -h, --help             Show this help and exit.

Examples:
  retroplug-cli render song.gbc                          # LSDj: auto-length stereo mix -> song.wav
  retroplug-cli render song.gbc --split channels         # 4 per-channel stereo stems
  retroplug-cli render song.gbc --song INTRO --out intro.wav
  retroplug-cli render game.nes --split channels --duration 5s   # NES core channels, fixed 5s
  retroplug-cli render song.gbc --sample-rate 96000      # render at 96 kHz`;

function intValue(flag: string, raw: string | undefined): number {
  const n = Number(raw);
  if (raw === undefined || !Number.isFinite(n) || !Number.isInteger(n) || n <= 0)
    throw new Error(`render: ${flag} needs a positive integer (got ${raw ?? "nothing"})`);
  return n;
}

const DURATION_UNITS_MS: Record<string, number> = { ms: 1, s: 1000, m: 60000 };

/** Parse a human duration ("500ms", "3s", "1.5m", or a bare number = ms) to a positive integer of ms. */
function parseDuration(flag: string, raw: string): number {
  const m = /^(\d+(?:\.\d+)?)(ms|s|m)?$/.exec(raw.trim());
  const value = m ? Number(m[1]) * DURATION_UNITS_MS[m[2] ?? "ms"] : NaN;
  if (!Number.isFinite(value) || value <= 0)
    throw new Error(`render: ${flag} needs a time like 3s / 500ms / 2m (got ${raw})`);
  return Math.round(value);
}

/** Parse a render argv (the tokens AFTER the `render` subcommand). Throws on bad usage. */
export function parseRenderArgs(argv: string[]): RenderOpts {
  let rom: string | undefined;
  let sav: string | undefined;
  let state: string | undefined;
  let out: string | undefined;
  let durationMs: number | undefined;
  let maxDurationMs = 300000; // 5 min default cap for LSDj length auto-detect
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
      case "--duration": durationMs = parseDuration(a, next(i, a)); i++; break;
      case "--max-duration": maxDurationMs = parseDuration(a, next(i, a)); i++; break;
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
        if (a.startsWith("--")) throw new Error(`render: unknown flag ${a} — ${RENDER_HINT}`);
        if (rom !== undefined) throw new Error(`render: unexpected extra argument ${a} — ${RENDER_HINT}`);
        rom = a;
    }
  }

  if (!rom) throw new Error(`render: missing <rom> — ${RENDER_HINT}`);
  if (song !== undefined && songIndex !== undefined)
    throw new Error("render: --song and --song-index are mutually exclusive");
  return { rom, sav, state, out, durationMs, maxDurationMs, sampleRate, split, bpm, transport, start, song, songIndex, listSongs };
}

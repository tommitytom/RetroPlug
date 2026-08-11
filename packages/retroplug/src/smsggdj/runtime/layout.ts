// Resolve an smsggdj version to its work-RAM layout. WLA assigns RAM addresses from RAMSECTION
// ordering, so adding a variable anywhere earlier shifts everything after it - the layout is authored
// per version from that build's own labels (symbols.generated.ts, via scripts/gen-smsggdj-symbols.mjs).
// A version with no snapshot resolves to null, and the tracker integration then reports the build
// unsupported rather than writing to addresses it is guessing at. Same shape as risa's
// ../../risa/runtime/layout.ts, and for the same reason.
import { SMSGGDJ_SYMBOLS } from "./symbols.generated";
import { SMDJ4_BLOCK_LEN } from "../codec/sav";
import type { SmsggdjLayout } from "./types";

// Versions whose work-RAM layout is byte-identical to another's, so they can share a snapshot instead
// of needing that build's label file. Verified by comparing the linker's symbol output for both:
//
//   0.46 -> 0.45   wave_ram c000, phrase_pool c100, echo_mode db6d, song_edited ddc0, prj_slot dd58,
//                  song_name dea4 - identical in both, because the v0.46 cur_slot change adds ROM code
//                  and no RAM variables.
//
// An alias is a claim about two builds, so it is only ever added after that comparison, never on the
// assumption that a small change cannot move anything.
const VERSION_ALIASES: Record<string, string> = { "0.46": "0.45" };

/** `version` is the ROM's real version (what the splash and identifySmsggdjVersion report);
 *  `symbolsKey` is the snapshot the addresses come from - the same, unless resolved via an alias. */
function layoutFrom(symbolsKey: string, version: string = symbolsKey): SmsggdjLayout {
  const s = SMSGGDJ_SYMBOLS[symbolsKey];
  return {
    version,
    song: s.wave_ram,
    // The block length is the FORMAT's (SMDJ4's 6,912 bytes), not a symbol: wave_ram_len is just the
    // 256 bytes of wave data that lead it. The layout only certifies where the block starts.
    songLen: SMDJ4_BLOCK_LEN,
    name: s.song_name,
    nameLen: s.song_name_len,
    echo: s.echo_mode,
    echoLen: s.echo_len,
    edited: s.song_edited,
    slot: s.prj_slot,
  };
}

/** The layout for `version` (e.g. "0.45"), or null when that version has neither a committed symbol
 *  snapshot nor an alias to one. An aliased version keeps its own label but borrows the addresses. */
export function resolveSmsggdjLayout(version: string | null): SmsggdjLayout | null {
  if (!version) return null;
  if (version in SMSGGDJ_SYMBOLS) return layoutFrom(version);
  const alias = VERSION_ALIASES[version];
  if (alias && alias in SMSGGDJ_SYMBOLS) return layoutFrom(alias, version);
  return null;
}

/** Every smsggdj version the host can drive - a committed symbol snapshot or an alias to one. */
export function supportedSmsggdjVersions(): string[] {
  return [...Object.keys(SMSGGDJ_SYMBOLS), ...Object.keys(VERSION_ALIASES)];
}

/** Where `song_name` sits, for a caller that has work RAM but NOT the ROM - `SongCatalog.workingName`,
 *  which is handed an image and a memory snapshot and no way to ask which build produced them.
 *
 *  Answers only when EVERY supported version agrees, which they do today (0.45 and 0.46 both put it at
 *  +$1EA4). The moment a build moves it this returns null and the caller falls back to the save's own
 *  cur_slot - degraded, but never reading a stranger's bytes and calling them a song name. Closing that
 *  proprly means threading the ROM through `workingName`, and it is not worth doing before a build
 *  actually diverges. */
export function commonSongNameOffset(): { offset: number; length: number } | null {
  const all = Object.values(SMSGGDJ_SYMBOLS);
  if (!all.length) return null;
  const { song_name, song_name_len } = all[0];
  return all.every((s) => s.song_name === song_name && s.song_name_len === song_name_len)
    ? { offset: song_name, length: song_name_len }
    : null;
}

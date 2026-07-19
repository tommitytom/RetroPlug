// LSDj runtime-state model: the typed values a reader extracts from a live Game Boy WRAM snapshot
// (region rp::MemoryType::Ram, base 0xC000). This is the transient PLAYBACK/UI state — is-playing,
// per-channel song/chain/phrase position, active screen, cursor, tempo — NOT the saved song (that is
// the sav codec in ../codec). All WRAM addresses + screen constants are provenance-tracked in offsets.ts.

// The 14 LSDj screens, indexed by the CURRENT_SCREEN byte (0xC402) as defined by LSDisJ's _*_SCREEN
// EQUs (src/lsdj-9.2.L.inc). Index 0 (pre-song / uninitialised) has no LSDisJ constant.
export const ScreenNames = [
  "boot", // 0x00 — pre-song / uninitialised
  "phrase", // 0x01
  "groove", // 0x02
  "chain", // 0x03
  "song", // 0x04
  "table", // 0x05
  "instrument", // 0x06
  "crash", // 0x07
  "waveEditor", // 0x08
  "project", // 0x09
  "synth", // 0x0a
  "talk", // 0x0b
  "word", // 0x0c
  "file", // 0x0d
  "help", // 0x0e
] as const;
export type Screen = (typeof ScreenNames)[number];

// The four Game Boy sound channels, in the byte order LSDj stores per-channel arrays (PU1, PU2, WAV,
// NOI at ARE_CHANNELS_PLAYING+0..3, etc.).
export const CHANNELS = ["pu1", "pu2", "wav", "noi"] as const;
export type ChannelName = (typeof CHANNELS)[number];

// A parsed LSDj ROM version from the cartridge title "LSDj-vX.Y.Z(+build)". Letter patch levels
// (9.2.A..9.2.N) map to numeric `patch` 10.. (A=10) so versions order correctly; `patchLabel` keeps
// the raw component ("2", "L") and `build` carries a suffix like "aboy" (arduinoboy) for stock/aboy.
export interface LsdjVersion {
  major: number;
  minor: number;
  patch: number;
  patchLabel: string;
  build: string | null;
  raw: string; // the full uppercase title, e.g. "LSDJ-V9.4.2"
}

// A resolved WRAM offset layout for one ROM version. Every offset is WRAM-relative (addr - 0xC000).
// The positional block (active..songRows) is stable across a wide version band; `tempo`/`currentScreen`/
// `cursors` drift per version and are null when the version isn't covered → the reader degrades those
// fields to null / "unknown" rather than guessing.
export interface CursorOffset {
  col: number;
  row: number;
}
export interface OffsetLayout {
  active: number; // ARE_CHANNELS_PLAYING (4 bytes, per channel; 1 = playing)
  phrases: number | null; // PLAYING_PHRASES (which phrase # per channel); null when unknown for a version
  phraseRows: number; // PLAYING_PHRASE_ROWS (row within the phrase, per channel)
  chains: number | null; // PLAYING_CHAINS (which chain # per channel); null when unknown
  chainRows: number; // PLAYING_CHAIN_ROWS
  songRows: number; // PLAYING_SONG_ROWS (song-order row, per channel)
  tempo: number | null; // TEMPO (1 byte, BPM in the normal 40..255 range)
  currentScreen: number | null; // CURRENT_SCREEN (1 byte) — drifts per version
  cursors: Partial<Record<Screen, CursorOffset>> | null; // per-screen cursor {col,row}; drifts
}

// Per-channel runtime playback state. Position fields are null when the channel isn't playing (LSDj
// parks a stopped channel's registers at 0xFF).
export interface LsdjChannelState {
  playing: boolean;
  phrase: number | null;
  phraseRow: number | null;
  chain: number | null;
  chainRow: number | null;
  songRow: number | null;
}

// The full decoded runtime state. `supported` is false (and every field degraded) when no layout
// resolved for the ROM's version.
export interface LsdjState {
  supported: boolean;
  version: LsdjVersion | null;
  playing: boolean; // any channel playing
  channels: Record<ChannelName, LsdjChannelState>;
  songRow: number | null; // song position = max valid per-channel song row (GBPresenter convention)
  screen: Screen | "unknown";
  cursor: CursorOffset | null; // cursor on the active screen, when known
  tempo: number | null;
}

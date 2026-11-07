// risa runtime-state model: the transient PLAYBACK/UI state a reader extracts from a live NES internal
// RAM snapshot (rp::MemoryType::Ram, $0000-$07FF) — is-playing, tempo, per-track song/chain/phrase
// position, active screen/cursor — NOT the saved catalog (that is the sav codec in ../codec). risa keeps
// all of this in the 2 KB internal RAM (src/seq.h notes the vars are extern "for access from test/UI
// code"), so unlike LSDj's banked GB WRAM the reader needs no bank juggling. Addresses come from the
// build's cc65/VICE labels (symbols.generated.ts).

// The 5 risa tracks, one per NES APU channel, in index order (src/apu.h APU_PULSE1..APU_DMC).
export const RISA_TRACKS = ["pulse1", "pulse2", "triangle", "noise", "dmc"] as const;
export type RisaTrack = (typeof RISA_TRACKS)[number];

// The 7 risa editor screens, indexed by ui_current_screen (src/ui_common.h SCREEN_*).
export const RISA_SCREENS = ["phrase", "chain", "song", "instrument", "groove", "table", "settings"] as const;
export type RisaScreen = (typeof RISA_SCREENS)[number];

// seq_mode (src/seq.h SEQ_MODE_*): the master playback mode.
export const RISA_MODES = ["stopped", "song", "chain", "phrase", "preview"] as const;
export type RisaMode = (typeof RISA_MODES)[number];

// Per-track runtime playback position. Fields are null when the track isn't producing sound (risa parks
// stopped-track indices at 0xFF).
export interface RisaTrackState {
  active: boolean; // this track is producing sound (seq_active bit)
  songRow: number | null; // song-order row (last-visited, per seq_get_song_row)
  chainId: number | null; // playing chain number
  chainRow: number | null; // row within the chain
  phraseId: number | null; // playing phrase number
  phraseRow: number | null; // row within the phrase
  note: number | null; // current APU note
  instrument: number | null; // last-triggered instrument
}

// The full decoded runtime state. `supported` is false (every field degraded) when no symbol layout
// resolved for the ROM's version.
export interface RisaState {
  supported: boolean;
  version: string | null;
  playing: boolean; // seq_mode != stopped and any track active
  mode: RisaMode | "unknown";
  bpm: number | null; // live BPM 40..295, or null; `fourX` is the fixed 4x-per-subframe mode
  fourX: boolean; // seq_current_bpm == 296 (TEMPO_MODE_4X)
  screen: RisaScreen | "unknown";
  cursor: { row: number; col: number } | null;
  uiTrack: number | null; // the track column the editor cursor is on
  kitActive: number | null; // active DPCM kit bank index
  tracks: RisaTrackState[]; // one per RISA_TRACKS
}

// A resolved internal-RAM offset layout for one risa version — the subset of symbols.generated.ts the
// reader consumes (all addresses in $0000-$07FF).
export interface RisaLayout {
  version: string;
  seqMode: number;
  seqActive: number;
  bpm: number; // u16
  songRow: number; // [5]
  songLastRow: number; // [5]
  chainId: number; // [5]
  chainRow: number; // [5]
  phraseId: number; // [5]
  phraseLastRow: number; // [5]
  note: number; // [5]
  lastInst: number; // [5]
  currentScreen: number;
  cursorRow: number;
  cursorCol: number;
  uiTrack: number;
  kitActive: number;
}

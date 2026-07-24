// The risa runtime-state reader: decode a live NES internal-RAM snapshot (rp::MemoryType::Ram,
// $0000-$07FF) into typed RisaState. Pure — no backend/emulator dependency — so it runs identically in
// the CLI/harness (fed by readMemory) and the plugin overlay (fed by the per-block readRam seam).
// Resolve a layout once from the ROM version; call decodeRisaState(ram, layout) per frame.
import { RISA_MODES, RISA_SCREENS, RISA_TRACKS, type RisaLayout, type RisaScreen, type RisaMode, type RisaState, type RisaTrackState } from "./types";

const TRACK_COUNT = RISA_TRACKS.length; // 5
const TEMPO_MODE_4X = 296; // seq.h TEMPO_MODE_4X (seq_current_bpm sentinel for the fixed 4x mode)

// A position/index byte: risa parks stopped tracks + empty cells at 0xFF → null.
function pos(b: number | undefined): number | null {
  return b === undefined || b === 0xff ? null : b;
}

/** Map a ui_current_screen byte to a screen name, or "unknown" when out of range. */
export function screenFromByte(b: number | undefined): RisaScreen | "unknown" {
  return b !== undefined && b < RISA_SCREENS.length ? RISA_SCREENS[b] : "unknown";
}

function emptyTrack(): RisaTrackState {
  return { active: false, songRow: null, chainId: null, chainRow: null, phraseId: null, phraseRow: null, note: null, instrument: null };
}

function unsupported(version: string | null): RisaState {
  return {
    supported: false,
    version,
    playing: false,
    mode: "unknown",
    bpm: null,
    fourX: false,
    screen: "unknown",
    cursor: null,
    uiTrack: null,
    kitActive: null,
    tracks: RISA_TRACKS.map(emptyTrack),
  };
}

/** Decode an internal-RAM snapshot into RisaState for a resolved layout. A null layout (unsupported ROM
 *  version) or an undersized buffer yields a fully-degraded `supported: false` state. */
export function decodeRisaState(ram: Uint8Array, layout: RisaLayout | null): RisaState {
  if (!layout) return unsupported(null);
  // Every symbol lives in the 2 KB internal RAM; if the snapshot doesn't cover it, degrade rather than
  // read past the end.
  const maxAddr = Math.max(layout.currentScreen, layout.lastInst + TRACK_COUNT, layout.bpm + 1);
  if (ram.length <= maxAddr) return unsupported(layout.version);

  const b = (off: number): number | undefined => ram[off];
  const seqMode = b(layout.seqMode) ?? 0;
  const seqActive = b(layout.seqActive) ?? 0;

  const tracks: RisaTrackState[] = [];
  for (let t = 0; t < TRACK_COUNT; t++) {
    // seq_get_song_row: prefer the last-visited row, falling back to the live row when it's 0xFF.
    const last = b(layout.songLastRow + t);
    const live = b(layout.songRow + t);
    const songRow = pos(last === 0xff ? live : last);
    tracks.push({
      active: ((seqActive >> t) & 1) === 1,
      songRow,
      chainId: pos(b(layout.chainId + t)),
      chainRow: pos(b(layout.chainRow + t)),
      phraseId: pos(b(layout.phraseId + t)),
      phraseRow: pos(b(layout.phraseLastRow + t)),
      note: pos(b(layout.note + t)),
      instrument: pos(b(layout.lastInst + t)),
    });
  }

  const rawBpm = (b(layout.bpm) ?? 0) | ((b(layout.bpm + 1) ?? 0) << 8);
  const fourX = rawBpm === TEMPO_MODE_4X;
  const bpm = !fourX && rawBpm >= 40 && rawBpm <= 295 ? rawBpm : null;
  const mode: RisaMode | "unknown" = seqMode < RISA_MODES.length ? RISA_MODES[seqMode] : "unknown";
  const screen = screenFromByte(b(layout.currentScreen));

  return {
    supported: true,
    version: layout.version,
    playing: seqMode !== 0, // SEQ_MODE_STOPPED
    mode,
    bpm,
    fourX,
    screen,
    cursor: { row: b(layout.cursorRow) ?? 0, col: b(layout.cursorCol) ?? 0 },
    uiTrack: pos(b(layout.uiTrack)),
    kitActive: pos(b(layout.kitActive)),
    tracks,
  };
}

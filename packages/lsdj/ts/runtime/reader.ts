// The LSDj runtime-WRAM reader: decode a live GB WRAM snapshot (region rp::MemoryType::Ram, base
// 0xC000) into typed LsdjState. Pure — no backend/emulator dependency — so it runs identically in the
// offline detector, the CLI/harness (fed by readMemory), and the plugin overlay (fed by the per-frame
// WRAM seam). Construct a LsdjReader once per system from the ROM header; call read(wram) per frame.
import type { ChannelName, LsdjChannelState, LsdjState, LsdjVersion, OffsetLayout, Screen } from "./types";
import { CHANNELS, ScreenNames } from "./types";
import { identifyLsdj } from "./identify";
import { resolveLayout } from "./layout";

// A per-channel position byte: valid LSDj rows/indices are 0..0x7f; LSDj parks a not-playing channel's
// registers at 0xFF, so anything > 0x7f reads as "not playing" → null.
function pos(b: number | undefined): number | null {
  return b === undefined || b > 0x7f ? null : b;
}

/** Map a CURRENT_SCREEN byte to a Screen name, or "unknown" when out of range / unavailable. */
export function screenFromByte(b: number | undefined): Screen | "unknown" {
  return b !== undefined && b < ScreenNames.length ? ScreenNames[b] : "unknown";
}

function emptyChannel(): LsdjChannelState {
  return { playing: false, phrase: null, phraseRow: null, chain: null, chainRow: null, songRow: null };
}

/** Decode WRAM into LsdjState given a resolved layout (the supported path). */
export function decodeLsdjState(wram: Uint8Array, layout: OffsetLayout, version: LsdjVersion | null): LsdjState {
  const b = (off: number): number | undefined => wram[off];
  const channels = {} as Record<ChannelName, LsdjChannelState>;
  let anyPlaying = false;
  let songRow: number | null = null;

  CHANNELS.forEach((name, i) => {
    const playing = b(layout.active + i) === 0x01;
    anyPlaying ||= playing;
    // NOT pos(): a song row is a full byte (LSDj has 256 of them), so the >0x7f "parked at 0xFF" rule
    // that suits chain/phrase indices would report every row from 128 up as "not playing". The channel's
    // own active flag already answers that question, so use it and read the row raw. Found by
    // test-native/lsdj-launchpad, which launches rows either side of the 128 boundary.
    const chSongRow = playing ? (b(layout.songRows + i) ?? null) : null;
    if (chSongRow != null && (songRow == null || chSongRow > songRow)) songRow = chSongRow; // GBPresenter: max valid row
    channels[name] = {
      playing,
      phrase: layout.phrases != null ? pos(b(layout.phrases + i)) : null,
      phraseRow: pos(b(layout.phraseRows + i)),
      chain: layout.chains != null ? pos(b(layout.chains + i)) : null,
      chainRow: pos(b(layout.chainRows + i)),
      songRow: chSongRow,
    };
  });

  const screen = layout.currentScreen != null ? screenFromByte(b(layout.currentScreen)) : "unknown";
  let cursor: { col: number; row: number } | null = null;
  if (layout.cursors) {
    // Screen known → that screen's cursor. Screen unknown (legacy versions, no drift shift) → fall back
    // to the SONG cursor, the one the old tool tracked and the one we always have from the ported table.
    const c = screen !== "unknown" ? layout.cursors[screen] : layout.cursors.song;
    if (c) cursor = { col: b(c.col) ?? 0, row: b(c.row) ?? 0 };
  }
  const tempo = layout.tempo != null ? (b(layout.tempo) ?? null) : null;

  return { supported: true, version, playing: anyPlaying, channels, songRow, screen, cursor, tempo };
}

function unsupportedState(version: LsdjVersion | null): LsdjState {
  return {
    supported: false,
    version,
    playing: false,
    channels: { pu1: emptyChannel(), pu2: emptyChannel(), wav: emptyChannel(), noi: emptyChannel() },
    songRow: null,
    screen: "unknown",
    cursor: null,
    tempo: null,
  };
}

/** A reader bound to one ROM's resolved layout. `supported` is false when the version isn't covered. */
export class LsdjReader {
  readonly version: LsdjVersion | null;
  readonly layout: OffsetLayout | null;
  readonly supported: boolean;

  constructor(version: LsdjVersion | null) {
    this.version = version;
    this.layout = resolveLayout(version);
    this.supported = this.layout != null;
  }

  /** Build a reader from a ROM header prefix (0x134 title parse + layout resolution). */
  static fromHeader(header: Uint8Array): LsdjReader {
    return new LsdjReader(identifyLsdj(header));
  }

  read(wram: Uint8Array): LsdjState {
    return this.layout ? decodeLsdjState(wram, this.layout, this.version) : unsupportedState(this.version);
  }
}

/** One-shot convenience: identify + resolve + read in a single call (per-frame, prefer LsdjReader). */
export function readLsdjState(header: Uint8Array, wram: Uint8Array): LsdjState {
  return LsdjReader.fromHeader(header).read(wram);
}

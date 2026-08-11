// The HD player's data pump: everything the renderer needs for one system, kept fresh at the right rate.
//
// Three inputs, on three very different clocks:
//   - The ROM assets (font, palette, kit sample names) change only when the cartridge does, so the
//     LsdjRom view is built once per system and the kit names are memoised on first use.
//   - The runtime state (playback positions, screen, cursor) changes every frame, and reading it is cheap
//     (backend.readRam → LsdjReader), so it is sampled per frame like LsdjOverlay does.
//   - The working song changes only when the user EDITS, but reading it is expensive: backend.readSram
//     copies the whole 128 KiB image and decodeSong builds ~700 objects. So it is polled a few times a
//     second and only re-decoded when the working-song region actually changed.
//
// The result is exposed as `renderFrame()`, which returns the pixel buffer when something moved and null
// when it didn't - so the caller can skip pushing an unchanged surface into LVGL.

import { useMemo } from "react";

import { useStores, useSystems } from "../../stores/useStores";
import { LsdjReader } from "../../../src/lsdj/runtime";
import { decodeSong, type Song } from "../../../src/lsdj";
import { LsdjRom } from "../../../src/lsdj/rom";
import { applyOverridesToRom, readOverrides } from "../../../src/lsdjAssetsRole";
import { LsdjHdCanvas, HD_COLS, HD_ROWS, renderMode2, type KitSampleNameLookup } from "../../../src/lsdj/hd";
import type { HostBackend } from "../../../src/backend";

/** The working song is the first 0x8000 bytes of the sav image (codec/sav.ts's kWorkingSong..kSongBytes). */
const WORKING_SONG_BYTES = 0x8000;

/** How often to re-check the working song for edits, in frames. At 60fps this is ~4 checks a second  - 
 *  an edit shows up within ~250ms, which is imperceptible, and the 128 KiB SRAM read stays off the
 *  per-frame path. */
const SONG_POLL_FRAMES = 15;

/** A cheap change signature over the working-song bytes. Not a cryptographic hash - it only has to catch
 *  an edit, and any missed change is corrected on the next real one. */
function songSignature(sav: Uint8Array): number {
  const n = Math.min(sav.length, WORKING_SONG_BYTES);
  let h = 0x811c9dc5;
  for (let i = 0; i < n; i++) {
    h ^= sav[i];
    h = (h * 0x01000193) >>> 0;
  }
  return h;
}

class HdSession {
  readonly canvas = new LsdjHdCanvas(HD_COLS, HD_ROWS);
  song: Song | null = null;

  private songSig = -1;
  private pollCountdown = 0;
  private appliedFont = -1;
  private appliedPalette = -1;
  private readonly kitNames = new Map<number, string>();

  constructor(
    private readonly backend: HostBackend,
    private readonly systemId: number,
    private readonly reader: LsdjReader,
    private readonly rom: LsdjRom,
  ) {}

  /** Kit sample names, memoised - LsdjRom.kit(k).sampleName(s) re-reads the cartridge on every call, and
   *  a phrase column asks for up to 32 of them per frame. */
  readonly kitSampleName: KitSampleNameLookup = (kit, sample) => {
    const key = kit * 256 + sample;
    let name = this.kitNames.get(key);
    if (name === undefined) {
      name = this.rom.kit(kit).sampleName(sample);
      this.kitNames.set(key, name);
    }
    return name;
  };

  /** Re-read the working song if it may have changed. Throttled; a no-op on most frames. */
  private pollSong(): void {
    if (this.pollCountdown-- > 0) return;
    this.pollCountdown = SONG_POLL_FRAMES;

    const sav = this.backend.readSram(this.systemId);
    if (!sav || sav.length < WORKING_SONG_BYTES) return;

    const sig = songSignature(sav);
    if (sig === this.songSig) return;
    this.songSig = sig;

    try {
      this.song = decodeSong(sav.subarray(0, WORKING_SONG_BYTES));
    } catch {
      this.song = null; // a half-initialised cart - try again on the next poll
      this.songSig = -1;
      return;
    }

    // The song carries which font and palette LSDj is displaying with. The +1 is the original player's:
    // the song's stored font index is offset by one from the ROM's font slots.
    const font = (this.song.settings.font + 1) % 3;
    const palette = this.song.settings.colorPalette;
    if (font !== this.appliedFont) {
      const view = this.rom.fonts()[font];
      if (view) this.canvas.setFont(view.toObject().tiles);
      this.appliedFont = font;
    }
    if (palette !== this.appliedPalette) {
      const view = this.rom.palettes()[palette];
      if (view) this.canvas.setPalette(view.toObject().colorSets);
      this.appliedPalette = palette;
    }
  }

  /** Draw one frame. Returns the pixel buffer if anything changed, else null. */
  renderFrame(): Uint32Array | null {
    this.pollSong();
    if (!this.song || !this.canvas.ready) return null;

    const wram = this.backend.readRam(this.systemId);
    if (!wram) return null;

    renderMode2(this.canvas, this.song, this.reader.read(wram), this.kitSampleName);
    return this.canvas.flush() > 0 ? this.canvas.getPixels() : null;
  }
}

/** Build the HD session for `systemId`, or null when the system isn't a supported LSDj cart. */
function buildSession(
  backend: HostBackend,
  systemId: number,
  romPath: string,
  overrideConfig: Record<string, unknown> | undefined,
): HdSession | null {
  const header = backend.readFilePrefix(romPath, 0x150);
  if (!header) return null;
  const reader = LsdjReader.fromHeader(header);
  if (!reader.supported) return null;

  const base = backend.readFile(romPath);
  if (!base) return null;
  // Fold in the lsdj-assets overrides so the HD view reads the SAME effective ROM the emulator was
  // constructed with - otherwise a replaced font or palette would show on the cart but not here.
  const overrides = readOverrides(overrideConfig);
  const effective = overrides.length ? applyOverridesToRom(base, overrides, backend) : base;
  const rom = LsdjRom.fromBytes(effective);
  if (!rom.isLsdj) return null;

  return new HdSession(backend, systemId, reader, rom);
}

export interface LsdjHdSession {
  /** Draw the next frame; null when nothing moved (so the caller skips the LVGL push). */
  renderFrame(): Uint32Array | null;
  readonly canvas: LsdjHdCanvas;
}

/**
 * The HD render session for one system, or null when it isn't a supported LSDj cart (the caller shows a
 * message instead). `enabled` gates the build, so a closed screen costs nothing.
 */
export function useLsdjHdSession(systemId: number, enabled = true): LsdjHdSession | null {
  const { backend } = useStores();
  const systems = useSystems();
  const sys = systems.find((s) => s.id === systemId);
  const romPath = sys?.romPath ?? "";
  const isLsdj = !!sys?.roles?.some((r) => r.kind === "lsdj-sync");
  const overrideConfig = sys?.roles?.find((r) => r.kind === "lsdj-assets")?.config;

  return useMemo(() => {
    if (!enabled || !isLsdj || !romPath) return null;
    return buildSession(backend, systemId, romPath, overrideConfig);
  }, [enabled, isLsdj, romPath, systemId, backend, overrideConfig]);
}

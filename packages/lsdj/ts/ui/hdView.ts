// The HD player as a window view (docs/modular-graph-phase-f4c-design.md section 6.1): the song order, all four
// channels' chains and all four channels' phrases on one full-window screen, drawn in the cartridge's own font and
// palette. The shell renders the pixels; this module only says how to open the session and what to show when there is
// nothing to read.
//
// Three inputs, on three very different clocks:
//   - The ROM assets (font, palette, kit sample names) change only when the cartridge does, so the
//     LsdjRom view is built once per session and the kit names are memoised on first use.
//   - The runtime state (playback positions, screen, cursor) changes every frame, and reading it is cheap
//     (host.systems.readRam -> LsdjReader), so it is sampled per frame like the overlay does.
//   - The working song changes only when the user EDITS, but reading it is expensive: host.systems.readSram
//     copies the whole 128 KiB image and decodeSong builds ~700 objects. So it is polled a few times a
//     second and only re-decoded when the working-song region actually changed.
//
// `frame()` returns the pixel buffer when something moved and null when it didn't, so the shell can skip pushing an
// unchanged surface into LVGL.
import type { Host } from "@retroplug/core/host";
import { isPlaced, roleConfig } from "@retroplug/core/systemRoles";
import type { PixelFrame, WindowView } from "@retroplug/ui/contribution";
import { HD_COLS, HD_ROWS, LsdjHdCanvas, renderMode2, type KitSampleNameLookup } from "../hd";
import { decodeSong } from "../codec/song";
import type { Song } from "../model";
import { LsdjRom } from "../rom";
import { LsdjReader } from "../runtime";
import { applyOverridesToRom, readOverrides } from "../lsdjAssetsRole";

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

/** One system's HD data pump. The shell opens it, reads `frame()` per frame and closes it when the window goes; it
 *  owns no React state. Exported so the cadence is tested over a fake host, with no display. */
export class HdSession {
  readonly canvas = new LsdjHdCanvas(HD_COLS, HD_ROWS);
  song: Song | null = null;

  private songSig = -1;
  private pollCountdown = 0;
  private appliedFont = -1;
  private appliedPalette = -1;
  private readonly kitNames = new Map<number, string>();

  constructor(
    private readonly host: Host<"systems">,
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

    const sav = this.host.systems.readSram(this.systemId);
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

  /** The next frame, with the size the canvas lays it out in; null when nothing moved. What the shell's pixels
   *  renderer reads, so the session is its own PixelSession. */
  frame(): PixelFrame | null {
    const pixels = this.renderFrame();
    return pixels ? { pixels, width: this.canvas.width, height: this.canvas.height } : null;
  }

  /** Draw one frame. Returns the pixel buffer if anything changed, else null. */
  renderFrame(): Uint32Array | null {
    this.pollSong();
    if (!this.song || !this.canvas.ready) return null;

    const wram = this.host.systems.readRam(this.systemId);
    if (!wram) return null;

    renderMode2(this.canvas, this.song, this.reader.read(wram), this.kitSampleName);
    return this.canvas.flush() > 0 ? this.canvas.getPixels() : null;
  }
}

/** Build the HD session for `systemId`, or null when the system isn't a supported LSDj cart. */
function buildSession(
  host: Host<"files" | "png" | "systems">,
  systemId: number,
  romPath: string,
  overrideConfig: Record<string, unknown> | undefined,
): HdSession | null {
  const header = host.files.readPrefix(romPath, 0x150);
  if (!header) return null;
  const reader = LsdjReader.fromHeader(header);
  if (!reader.supported) return null;

  const base = host.files.readFile(romPath);
  if (!base) return null;
  // Fold in the lsdj-assets overrides so the HD view reads the SAME effective ROM the emulator was
  // constructed with - otherwise a replaced font or palette would show on the cart but not here.
  const overrides = readOverrides(overrideConfig);
  const effective = overrides.length ? applyOverridesToRom(base, overrides, host) : base;
  const rom = LsdjRom.fromBytes(effective);
  if (!rom.isLsdj) return null;

  return new HdSession(host, systemId, reader, rom);
}

/** The playable window view: it applies to a system whose graph places lsdj-sync (not one that merely stores it, an
 *  mGB cart after a ROM swap), and its `open` refuses any other system or one with no ROM path itself, since the shell
 *  checks `when` only at `views.open` and a renderer opens a session too. `satisfies` rather than an annotation, so
 *  `open` keeps the host groups it declares (what a pack copied from this one sees) instead of widening to the
 *  contract's whole UI host. */
export const hdView = {
  id: "lsdj-hd",
  label: "HD Player",
  placement: "window",
  when: (sys) => isPlaced(sys, "lsdj-sync"),
  body: {
    kind: "pixels",
    unsupported: "HD view needs a supported LSDj cartridge",
    open: (host: Host<"files" | "png" | "systems">, systemId: number) => {
      const sys = host.systems.view().find((s) => s.id === systemId);
      if (!sys || !isPlaced(sys, "lsdj-sync") || !sys.romPath) return null;
      return buildSession(host, systemId, sys.romPath, roleConfig(sys.roles, "lsdj-assets"));
    },
  },
} satisfies WindowView;

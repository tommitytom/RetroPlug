// The risa debug overlay: the risa twin of LSDj's readout (docs/modular-graph-phase-f4c-design section 6.2). It shows
// the visible screen and cursor, play/mode, tempo, the ROM version, and each of the five tracks' playing phrase.row,
// over every risa tile whose package flag is on.
//
// The sampler is the old hook's body as plain code: the internal-RAM layout is resolved once from the ROM (the "RISA V"
// marker is deep in the PRG, so the whole file is read), then every frame pulls the RAM snapshot (host.systems.readRam)
// and decodes it. Null means the cart is not supported; undefined means this frame's RAM was unreadable.
import type { Host } from "@retroplug/core/host";
import { isPlaced } from "@retroplug/core/systemRoles";
import { hex2 } from "@retroplug/ui/format";
import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
import * as runtime from "../runtime";
import type { RisaState, RisaTrackState } from "../runtime";
import { risaDebug } from "./debugOverlay";

const TRACK_LABELS = ["1", "2", "T", "N", "D"]; // pulse1, pulse2, triangle, noise, dmc

/** A per-track cell: the playing phrase.row (hex), or "--" when the track is parked. */
function trackCell(label: string, t: RisaTrackState): string {
  return t.active ? `${label} ${hex2(t.phraseId)}.${hex2(t.phraseRow)}` : `${label} --`;
}

/** The three lines the strip shows, exactly as the old overlay drew them. */
function linesOf(state: RisaState): string[] {
  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  const tempo = state.fourX ? "4X" : state.bpm != null ? `${state.bpm}bpm` : "--bpm";
  return [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? state.mode.toUpperCase() : "STOP"}  ${tempo}  v${state.version ?? "?"}`,
    state.tracks.map((t, i) => trackCell(TRACK_LABELS[i], t)).join("  "),
  ];
}

/** The risa debug overlay: a readout over every risa tile, shown while the package's flag is on (backtick by
 *  default, through the `risa.debug-overlay` action). The default dedupe (the lines and the flag) is exactly what the
 *  strip draws, so no narrower signature is needed and the displayed output is identical. */
export const overlayView = {
  id: "risa-overlay",
  label: "risa Debug Overlay",
  placement: "tile",
  visible: risaDebug,
  when: (sys) => isPlaced(sys, "risa"),
  body: {
    kind: "readout",
    open: (host: Host<"files" | "systems">, systemId: number) => {
      const sys = host.systems.view().find((s) => s.id === systemId);
      const romPath = sys?.romPath ?? "";
      if (!sys || !romPath || !isPlaced(sys, "risa")) return null;
      const rom = host.files.readFile(romPath);
      if (!rom) return null;
      const layout = runtime.resolveRisaLayout(runtime.identifyRisaVersion(rom));
      if (!layout) return null;
      return {
        sample: () => {
          const ram = host.systems.readRam(systemId);
          if (!ram) return undefined;
          const state = runtime.decodeRisaState(ram, layout);
          if (!state.supported) return null;
          const frame: ReadoutFrame = { lines: linesOf(state), playing: state.playing };
          return frame;
        },
      };
    },
  },
} satisfies TileView;

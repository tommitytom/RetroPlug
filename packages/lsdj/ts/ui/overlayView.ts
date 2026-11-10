// A compact live readout of an LSDj instance's runtime state, layered over its emulator tile: the visible screen and
// its cursor position, play/stop, song position, tempo, the ROM version, and each channel's playing phrase.row. It is
// a readout view, so the shell draws the strip and this module only opens the sampler (docs/modular-graph-phase-f4c
// design section 6.1).
//
// The sampler is the old hook's body as plain code: the reader is built once from the ROM header, then every frame
// pulls the per-block WRAM snapshot (host.systems.readRam, safe while the core plays) and decodes it. Null means the
// cart is not supported, so nothing shows; undefined means this frame's RAM was unreadable, so the strip keeps what
// shows.
import type { Host } from "@retroplug/core/host";
import { hasRole } from "@retroplug/core/systemRoles";
import { hex2 } from "@retroplug/ui/format";
import type { ReadoutFrame, TileView } from "@retroplug/ui/contribution";
import { LsdjReader, type LsdjState } from "../runtime";
import { lsdjDebug } from "./debugOverlay";

/** "9.4.2" / "9.2.L-aboy" from the parsed version (patchLabel keeps the raw component, e.g. "2" or "L"). */
function versionStr(v: LsdjState["version"]): string {
  if (!v) return "?";
  return `${v.major}.${v.minor}.${v.patchLabel}${v.build ? "-" + v.build : ""}`;
}

/** A per-channel cell: the playing phrase.row (hex), or "--" when the channel is parked. */
function channelCell(label: string, c: LsdjState["channels"]["pu1"]): string {
  return c.playing ? `${label} ${hex2(c.phrase)}.${hex2(c.phraseRow)}` : `${label} --`;
}

/** The three lines the strip shows, exactly as the old overlay drew them. */
function linesOf(state: LsdjState): string[] {
  const { channels: ch } = state;
  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  return [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? "PLAY" : "STOP"} S${hex2(state.songRow)}` +
      `  ${state.tempo != null ? `${state.tempo}bpm` : "--bpm"}  v${versionStr(state.version)}`,
    `${channelCell("1", ch.pu1)}  ${channelCell("2", ch.pu2)}  ` +
      `${channelCell("W", ch.wav)}  ${channelCell("N", ch.noi)}`,
  ];
}

/** The LSDj debug overlay: a readout over every LSDj tile, shown while the package's flag is on (backtick by
 *  default, through the `lsdj.debug-overlay` action). The default dedupe (the lines and the flag) is exactly what the
 *  strip draws, so the old hook's narrower signature is not needed and the displayed output is identical. */
export const overlayView = {
  id: "lsdj-overlay",
  label: "LSDj Debug Overlay",
  placement: "tile",
  visible: lsdjDebug,
  when: (sys) => hasRole(sys.roles, "lsdj-sync"),
  body: {
    kind: "readout",
    open: (host: Host<"files" | "systems">, systemId: number) => {
      const sys = host.systems.view().find((s) => s.id === systemId);
      const romPath = sys?.romPath ?? "";
      if (!sys || !romPath || !hasRole(sys.roles, "lsdj-sync")) return null;
      const header = host.files.readPrefix(romPath, 0x150);
      if (!header) return null;
      const reader = LsdjReader.fromHeader(header);
      if (!reader.supported) return null;
      return {
        sample: () => {
          const wram = host.systems.readRam(systemId);
          if (!wram) return undefined;
          const state = reader.read(wram);
          if (!state.supported) return null;
          const frame: ReadoutFrame = { lines: linesOf(state), playing: state.playing };
          return frame;
        },
      };
    },
  },
} satisfies TileView;

// A compact live readout of an LSDj instance's runtime state, layered over its emulator tile — a small
// debug/insight panel of everything the WRAM reader decodes per frame (useLsdjRuntime → backend.readRam
// → LsdjReader): the visible screen + its cursor position, play/stop, song position, tempo, the ROM
// version, and each channel's playing phrase.row. Renders nothing for a non-LSDj / unsupported ROM, so
// EmulatorTile can mount it unconditionally.
import { Text } from "lvgljs-ui";

import { Box } from "../../lvgl/Box";
import { tagTestId } from "../../lvgl/StableSlot";
import { useLsdjRuntime } from "./useLsdjRuntime";
import { useLsdjDebugVisible } from "./lsdjDebug";
import type { LsdjChannelState, LsdjState } from "../../../src/lsdj/runtime";

const LV_ALIGN_BOTTOM_MID = 5;

const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));

// "9.4.2" / "9.2.L-aboy" from the parsed version (patchLabel keeps the raw component, e.g. "2" or "L").
function versionStr(v: LsdjState["version"]): string {
  if (!v) return "?";
  return `${v.major}.${v.minor}.${v.patchLabel}${v.build ? "-" + v.build : ""}`;
}

// A per-channel cell: the playing phrase.row (hex), or "--" when the channel is parked.
function channelCell(label: string, c: LsdjChannelState): string {
  return c.playing ? `${label} ${hex2(c.phrase)}.${hex2(c.phraseRow)}` : `${label} --`;
}

export function LsdjOverlay({ systemId, width, testId }: { systemId: number; width: number; testId?: string }) {
  // A dev debugging aid, hidden by default — toggled with the backtick key (App). When off, useLsdjRuntime
  // is disabled so it does no per-frame WRAM work.
  const visible = useLsdjDebugVisible();
  const state = useLsdjRuntime(systemId, visible);
  if (!visible || !state || !state.supported) return null;

  const { channels: ch } = state;
  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  // Line 1: the visible screen + its cursor position (the headline). Line 2: transport + song row + tempo
  // + version. Line 3: each channel's playing phrase.row.
  const lines = [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? "PLAY" : "STOP"} S${hex2(state.songRow)}` +
      `  ${state.tempo != null ? `${state.tempo}bpm` : "--bpm"}  v${versionStr(state.version)}`,
    `${channelCell("1", ch.pu1)}  ${channelCell("2", ch.pu2)}  ${channelCell("W", ch.wav)}  ${channelCell("N", ch.noi)}`,
  ];

  return (
    <Box
      innerRef={testId ? tagTestId(testId) : undefined}
      align={{ type: LV_ALIGN_BOTTOM_MID, pos: [0, 0] }}
      style={{ width, height: 44, "background-color": "#000000", "background-opacity": 0.6 }}
    >
      <Text style={{ "text-color": state.playing ? "#8bc34a" : "#bbbbbb", "font-size": 11, width: "100%", height: 44 }}>
        {lines.join("\n")}
      </Text>
    </Box>
  );
}

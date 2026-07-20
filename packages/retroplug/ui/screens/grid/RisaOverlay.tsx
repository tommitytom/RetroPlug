// A compact live readout of a risa instance's runtime state, layered over its emulator tile — the risa
// twin of LsdjOverlay: everything the RAM reader decodes per frame (useRisaRuntime → backend.readRam →
// decodeRisaState): the visible screen + cursor, play/mode, tempo, ROM version, and each of the 5 tracks'
// playing phrase.row. Renders nothing for a non-risa / unsupported ROM, so EmulatorTile mounts it
// unconditionally. Shares the backtick debug toggle with LsdjOverlay (one dev switch, per-system readout).
import { Text } from "lvgljs-ui";

import { Box } from "../../lvgl/Box";
import { tagTestId } from "../../lvgl/StableSlot";
import { useRisaRuntime } from "./useRisaRuntime";
import { useLsdjDebugVisible } from "./lsdjDebug";
import type { RisaState, RisaTrackState } from "../../../src/risa/runtime";

const LV_ALIGN_BOTTOM_MID = 5;
const TRACK_LABELS = ["1", "2", "T", "N", "D"]; // pulse1, pulse2, triangle, noise, dmc

const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));

// A per-track cell: the playing phrase.row (hex), or "--" when the track is parked.
function trackCell(label: string, t: RisaTrackState): string {
  return t.active ? `${label} ${hex2(t.phraseId)}.${hex2(t.phraseRow)}` : `${label} --`;
}

export function RisaOverlay({ systemId, width, testId }: { systemId: number; width: number; testId?: string }) {
  // A dev debugging aid, hidden by default — toggled with the backtick key (App). When off, useRisaRuntime
  // is disabled so it does no per-frame RAM work.
  const visible = useLsdjDebugVisible();
  const state = useRisaRuntime(systemId, visible);
  if (!visible || !state || !state.supported) return null;

  const cursor = state.cursor ? `${state.cursor.col},${state.cursor.row}` : "--";
  const tempo = state.fourX ? "4X" : state.bpm != null ? `${state.bpm}bpm` : "--bpm";
  // Line 1: the visible screen + its cursor. Line 2: transport (mode) + tempo + version. Line 3: each of
  // the 5 tracks' playing phrase.row.
  const lines: string[] = [
    `${state.screen.toUpperCase()} @${cursor}`,
    `${state.playing ? state.mode.toUpperCase() : "STOP"}  ${tempo}  v${state.version ?? "?"}`,
    state.tracks.map((t, i) => trackCell(TRACK_LABELS[i], t)).join("  "),
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

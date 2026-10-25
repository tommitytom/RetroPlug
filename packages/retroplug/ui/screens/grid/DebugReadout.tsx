// The shell both tracker debug overlays draw into (LsdjOverlay, RisaOverlay): a translucent strip along the
// tile's bottom edge carrying a few lines of live readout. Each overlay only decides WHAT the lines say.
import { EAlignType, Text } from "lvgljs-ui";

import { Box } from "../../lvgl/Box";
import { tagTestId } from "../../lvgl/StableSlot";

const HEIGHT = 44;

/** A byte as two uppercase hex digits, "--" when unknown. */
export const hex2 = (n: number | null): string => (n == null ? "--" : n.toString(16).toUpperCase().padStart(2, "0"));

export function DebugReadout({
  lines,
  playing,
  width,
  testId,
}: {
  lines: string[];
  /** Green while the cart plays, grey when stopped. */
  playing: boolean;
  width: number;
  testId?: string;
}) {
  return (
    <Box
      innerRef={testId ? tagTestId(testId) : undefined}
      align={{ type: EAlignType.ALIGN_BOTTOM_MID, pos: [0, 0] }}
      style={{ width, height: HEIGHT, "background-color": "#000000", "background-opacity": 0.6 }}
    >
      <Text style={{ "text-color": playing ? "#8bc34a" : "#bbbbbb", "font-size": 11, width: "100%", height: HEIGHT }}>
        {lines.join("\n")}
      </Text>
    </Box>
  );
}

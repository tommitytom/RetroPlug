// A placeholder's tile (docs/downloadable-packs-m3-design.md section 8): the tile's frame and focus border, so the grid
// does not move, and the card's text where the screen would be. No frame is pulled: nothing runs. Clicking it focuses
// it, so its menu (Remove only) opens on it.
import { EAlignType, Text } from "lvgljs-ui";
import { Box, tagTestId } from "@retroplug/ui-react";
import type { SystemView } from "../../../src/systemsStore";
import { placeholderLines } from "./placeholderText";

export function PlaceholderTile({
  view,
  focused,
  single,
  width,
  height,
  onFocus,
  testId,
  dimTestId,
}: {
  view: SystemView;
  focused: boolean;
  single?: boolean;
  width: number;
  height: number;
  onFocus?: () => void;
  testId: string;
  dimTestId?: string;
}) {
  const lines = placeholderLines(view);
  const font = Math.max(8, Math.round(height / 16));
  const showBorder = focused && !single;
  const dim = !focused && !single;
  return (
    <Box innerRef={tagTestId(testId)} onClick={onFocus} style={{ width, height, "background-color": "#101010" }}>
      <Box
        style={{
          width,
          height,
          "background-opacity": 0,
          overflow: "hidden",
          display: "flex",
          "flex-direction": "column",
          "justify-content": "center",
          "align-items": "center",
        }}
      >
        {lines.map((line, i) => (
          <Text
            key={`line-${i}`}
            style={{
              "text-color": i === 0 ? "#ffffff" : "#c8c8c8",
              "font-size": font,
              width: width - 8,
              "text-align": "center",
            }}
          >
            {line}
          </Text>
        ))}
      </Box>
      {dim && (
        <Box
          innerRef={dimTestId ? tagTestId(dimTestId) : undefined}
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-color": "#000000", "background-opacity": 0.5 }}
        />
      )}
      {showBorder && (
        <Box
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-opacity": 0, "border-width": 2, "border-color": "#4a86e8" }}
        />
      )}
    </Box>
  );
}

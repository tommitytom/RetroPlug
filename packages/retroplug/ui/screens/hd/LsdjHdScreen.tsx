// The LSDj HD player screen - a full-window view of one LSDj instance showing the song order, all four
// channels' chains and all four channels' phrases at once, drawn in the cartridge's own font and palette.
// Ported from the old C++ LsdjHdPlayer; the renderer itself lives in src/lsdj/hd.
//
// It sits alongside SystemGrid in App: the cart keeps playing underneath and keys still reach its joypad
// (App leaves useGameInput active), so this is a display over the running system, not a modal. Esc closes
// it, via App's universal-back handler.
//
// The per-frame push is guarded: the session only returns a buffer when a tile actually changed, because
// LVGL's setBuffer copies the whole 1.8 MB surface and forces a full invalidate.

import { useRef } from "react";
import { EAlignType, Text } from "lvgljs-ui";

import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { Box } from "../../lvgl/Box";
import { PixelCanvas } from "../../lvgl/PixelCanvas";
import type { CanvasHandle } from "../../lvgl/lv";
import { tagTestId } from "../../lvgl/StableSlot";
import { useLsdjHdSession } from "./useLsdjHdSession";

export function LsdjHdScreen({
  systemId,
  width,
  height,
  testId,
}: {
  systemId: number;
  width: number;
  height: number;
  testId?: string;
}) {
  const canvasRef = useRef<CanvasHandle | null>(null);
  const session = useLsdjHdSession(systemId);

  useNativeEvent("frame", () => {
    if (!session) return;
    const pixels = session.renderFrame();
    if (!pixels) return; // nothing moved - skip the copy + invalidate
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.setBuffer(pixels.buffer as ArrayBuffer, session.canvas.width, session.canvas.height);
  });

  return (
    <Box
      innerRef={testId ? tagTestId(testId) : undefined}
      style={{ width, height, "background-color": "#000000" }}
    >
      {session ? (
        <PixelCanvas canvasRef={canvasRef} width={width} height={height} />
      ) : (
        // Not an LSDj cart, or a ROM version the WRAM reader has no offset layout for - the view has
        // nothing to draw, so say so rather than showing a black rectangle.
        <Box
          innerRef={tagTestId("lsdj-hd-unsupported")}
          align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height: 40 }}
        >
          <Text style={{ "text-color": "#ffffff", "font-size": 16, width, height: 40 }}>
            HD view needs a supported LSDj cartridge
          </Text>
        </Box>
      )}
    </Box>
  );
}

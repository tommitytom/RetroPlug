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
import { Canvas, Text } from "lvgljs-ui";

import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { Box } from "../../lvgl/Box";
import { tagTestId } from "../../lvgl/StableSlot";
import { useLsdjHdSession } from "./useLsdjHdSession";

// lv_image.h's inner-align enum. CONTAIN scales the image to the largest size that FITS the widget,
// keeping aspect (letterboxing); COVER keeps aspect but FILLS the widget, cropping the overflow. They sit
// next to each other and are easy to swap by accident: CENTER is 9, then _AUTO_TRANSFORM 10, STRETCH 11,
// TILE 12, CONTAIN 13, COVER 14. Using 14 here is what cropped the sides on a narrow window and the top
// and bottom on a tall one.
const LV_IMAGE_ALIGN_CONTAIN = 13;
const LV_ALIGN_CENTER = 0x09;

// lvgljs-ui's Canvas type doesn't expose a ref prop; cast to reach setBuffer (as EmulatorTile does).
const CanvasAny = Canvas as any;

interface CanvasHandle {
  setBuffer(buffer: ArrayBuffer, width: number, height: number): void;
}

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
        <CanvasAny
          ref={canvasRef}
          nearestNeighbor={true}
          innerAlign={LV_IMAGE_ALIGN_CONTAIN}
          style={{ width, height, "border-width": 0 }}
        />
      ) : (
        // Not an LSDj cart, or a ROM version the WRAM reader has no offset layout for - the view has
        // nothing to draw, so say so rather than showing a black rectangle.
        <Box
          innerRef={tagTestId("lsdj-hd-unsupported")}
          align={{ type: LV_ALIGN_CENTER, pos: [0, 0] }}
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

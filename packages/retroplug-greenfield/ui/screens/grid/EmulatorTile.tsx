// One emulator tile: the running Game Boy screen for a system, blitted into an LVGL Canvas.
//
// On every "frame" render tick it pulls the system's framebuffer (backend.getFrame — synchronous in
// greenfield, so no async drop-frame guard) and pushes it into the Canvas via setBuffer. Clicking the
// tile focuses its system (LVGL bubbles the child click up to the root View's onClick). An unfocused
// tile is dimmed by a translucent black overlay — the lvgl-js way, since opacity on a Canvas is dropped
// (only Image widgets route it) and opacity units are 0..1 floats (0.5 = LV_OPA_50).

import { useRef } from "react";
import { View, Canvas } from "lvgljs-ui";

import { useStores } from "../../stores/useStores";
import { useNativeEvent } from "../../lvgl/useNativeEvent";
import { tagTestId } from "../../lvgl/StableSlot";

const LV_IMAGE_ALIGN_CONTAIN = 14; // aspect-preserving nearest-neighbour scale
const LV_ALIGN_CENTER = 0x09;

// lvgljs-ui's Canvas / View types don't expose a ref prop; cast to reach setBuffer / the dim testId.
const CanvasAny = Canvas as any;
const OverlayView = View as any;

interface CanvasHandle {
  setBuffer(buffer: ArrayBuffer, width: number, height: number): void;
}

export function EmulatorTile({
  systemId,
  focused,
  width,
  height,
  onFocus,
  dimTestId,
}: {
  systemId: number;
  focused: boolean;
  width: number;
  height: number;
  onFocus?: () => void;
  /** Test tag for the dim overlay — present only while the tile is unfocused (a live focus signal). */
  dimTestId?: string;
}) {
  const { backend } = useStores();
  const canvasRef = useRef<CanvasHandle | null>(null);

  useNativeEvent("frame", () => {
    const frame = backend.getFrame(systemId);
    if (!frame || !frame.published) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    // .slice().buffer: the pixels may be a subarray view of the RPC wire buffer; copy to a fresh
    // ArrayBuffer of exactly the frame bytes (setBuffer wants a whole-buffer object).
    canvas.setBuffer(frame.pixels.slice().buffer, frame.width, frame.height);
  });

  return (
    <View
      onClick={onFocus}
      style={{
        width,
        height,
        "background-color": "#000000",
        // Accent border on the focused tile — the visible focus cue (the dim is imperceptible on a
        // near-black GB screen). Unfocused tiles are borderless + dimmed.
        "border-width": focused ? 2 : 0,
        "border-color": "#4a86e8",
        overflow: "hidden",
      }}
    >
      <CanvasAny ref={canvasRef} nearestNeighbor={true} innerAlign={LV_IMAGE_ALIGN_CONTAIN} style={{ width, height, "border-width": 0 }} />
      {!focused && (
        <OverlayView
          ref={dimTestId ? tagTestId(dimTestId) : undefined}
          align={{ type: LV_ALIGN_CENTER, pos: [0, 0] }}
          style={{ width, height, "background-color": "#000000", "background-opacity": 0.5, "border-width": 0 }}
        />
      )}
    </View>
  );
}

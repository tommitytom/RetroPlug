// The pixels renderer (docs/modular-graph-phase-f4c-design.md section 5.3): a view that draws its own frame, blitted
// into an LVGL Canvas the way the emulator tile and the LSDj HD player do. The shell draws it; a package only says how
// to open the session and what to show when there is nothing to read (the view's own `unsupported` text).
import { useRef } from "react";
import { EAlignType, Text } from "lvgljs-ui";

import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, ViewDef } from "@retroplug/ui/contribution";
import { Box, PixelCanvas, tagTestId, useNativeEvent, type CanvasHandle } from "@retroplug/ui-react";
import { useViewSession } from "./useViewSession";
import { isPixelSession } from "./viewSession";

export function PixelView({
  view,
  host,
  system,
  width,
  height,
  placement,
}: {
  view: ViewDef;
  host: UiHost;
  system: SystemInfo | null;
  width: number;
  height: number;
  /** Where the view sits: only a window view shows the body's `unsupported` text, since a tile view whose open was
   *  refused draws nothing (the contract says so, and the overlays' own null render was that). */
  placement: "window" | "tile";
}) {
  const canvasRef = useRef<CanvasHandle | null>(null);
  const { session, read } = useViewSession(view, host, system, true);

  useNativeEvent("frame", () => {
    // Do not ask the session for a frame until there is a canvas to put it in: the session's `frame()` is a
    // flush-and-diff (the HD canvas reports only the tiles that changed since its last flush), so reading one into
    // nothing would report it painted and never draw it. The canvas mounts with the session, one effect after the
    // first frame tick at the latest.
    const canvas = canvasRef.current;
    if (!canvas) return;
    // The controller's session, not this render's: a frame after a hide or a reopen must not read a closed one.
    const frame = read((open) => (isPixelSession(open) ? open.frame() : null));
    if (!frame) return; // nothing moved: skip the copy and the invalidate
    const pixels = frame.pixels;
    // The HD canvas hands its own array every frame, so that one is passed as it is; a frame that is a view onto a
    // larger buffer (the emulator's RPC wire buffer) is copied, since setBuffer wants a whole-buffer object.
    const whole = pixels.byteOffset === 0 && pixels.byteLength === pixels.buffer.byteLength;
    canvas.setBuffer((whole ? pixels.buffer : pixels.slice().buffer) as ArrayBuffer, frame.width, frame.height);
  });

  // Not opened yet: nothing to draw. That is a frame or two at most, and it must not read as "unsupported", which
  // would show the message on a cart the view reads perfectly well.
  if (session === undefined) return null;
  if (session !== null) {
    if (!isPixelSession(session)) return null;
    return <PixelCanvas canvasRef={canvasRef} width={width} height={height} />;
  }
  const unsupported = view.body.kind === "pixels" ? view.body.unsupported : undefined;
  if (placement !== "window" || !unsupported) return null;
  return (
    <Box
      innerRef={tagTestId(`${view.id}-unsupported`)}
      align={{ type: EAlignType.ALIGN_CENTER, pos: [0, 0] }}
      style={{ width, height: 40 }}
    >
      <Text style={{ "text-color": "#ffffff", "font-size": 16, width, height: 40 }}>{unsupported}</Text>
    </Box>
  );
}

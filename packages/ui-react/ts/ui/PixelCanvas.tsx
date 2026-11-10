// PixelCanvas: a Canvas showing a pixel buffer scaled to fit: nearest-neighbour (crisp pixels) and
// CONTAIN-aligned (keeps aspect, letterboxes rather than crops). The emulator tile and the LSDj HD screen both
// push frames into one of these through `canvasRef.current.setBuffer(...)`.

import type { Ref } from "react";

import { CanvasEx, LV_IMAGE_ALIGN_CONTAIN, type CanvasHandle } from "./lv";

export function PixelCanvas({ canvasRef, width, height }: { canvasRef: Ref<CanvasHandle | null>; width: number; height: number }) {
  return <CanvasEx ref={canvasRef} nearestNeighbor={true} innerAlign={LV_IMAGE_ALIGN_CONTAIN} style={{ width, height, "border-width": 0 }} />;
}

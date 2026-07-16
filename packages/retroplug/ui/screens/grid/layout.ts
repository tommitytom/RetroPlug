// Pure grid layout math for the emulator grid, ported from the legacy shell (packages/ui/src/layout.ts +
// SystemGrid.shapeFor). GB is 160×144 native; integer zoom (1..6) scales the tile linearly and LVGL's
// nearest-neighbour upscales the framebuffer into it. `layout` matches ProjectSettings.layout
// ("auto" / "row" / "column" / "grid"; see settingsEnums).

import { SystemLayout, LAYOUT_VALUES } from "../../../src/settingsEnums";
export { SystemLayout, LAYOUT_VALUES };

export const GB_NATIVE_W = 160;
export const GB_NATIVE_H = 144;

export interface GridShape {
  cols: number;
  rows: number;
}

export const MIN_ZOOM = 1;
export const MAX_ZOOM = 6;

/** The effective zoom for a project: its own zoom when a valid 1..6, else the user default; clamped to 1..6.
 *  The single source of truth for "what zoom is this project shown at" — used by the window-size fit (App)
 *  and the pre-map initial-size helper (main), which must agree or the window would resize on first frame. */
export function resolveZoom(projectZoom: number, defaultZoom: number): number {
  const z = projectZoom >= MIN_ZOOM && projectZoom <= MAX_ZOOM ? projectZoom : defaultZoom;
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z));
}

export function tileWidth(zoom: number): number {
  return GB_NATIVE_W * zoom;
}

export function tileHeight(zoom: number): number {
  return GB_NATIVE_H * zoom;
}

// 1=center, 2=row, 3–4=2×2, 5–9=3×3, 10–16=4×4; beyond that, square it. (legacy autoShape.)
export function autoShape(count: number): GridShape {
  if (count <= 1) return { cols: 1, rows: 1 };
  if (count === 2) return { cols: 2, rows: 1 };
  if (count <= 4) return { cols: 2, rows: 2 };
  if (count <= 9) return { cols: 3, rows: 3 };
  if (count <= 16) return { cols: 4, rows: 4 };
  const cols = Math.ceil(Math.sqrt(count));
  return { cols, rows: Math.ceil(count / cols) };
}

export function shapeFor(layout: SystemLayout, count: number): GridShape {
  switch (layout) {
    case SystemLayout.Row:
      return { cols: count, rows: 1 };
    case SystemLayout.Column:
      return { cols: 1, rows: count };
    default:
      return autoShape(count); // Auto + Grid
  }
}

/** The pixel size of the grid content area for a system count + layout + zoom. */
export function gridContentSize(
  count: number,
  layout: SystemLayout,
  zoom: number,
): { width: number; height: number; shape: GridShape } {
  const shape = shapeFor(layout, Math.max(count, 1));
  return { width: shape.cols * tileWidth(zoom), height: shape.rows * tileHeight(zoom), shape };
}

/** The grid-local rect of the tile at `index` (row/column derived from the shape). */
export function getTileBounds(
  index: number,
  count: number,
  layout: SystemLayout,
  zoom: number,
): { x: number; y: number; w: number; h: number } {
  const shape = shapeFor(layout, Math.max(count, 1));
  const col = index % shape.cols;
  const row = Math.floor(index / shape.cols);
  const w = tileWidth(zoom);
  const h = tileHeight(zoom);
  return { x: col * w, y: row * h, w, h };
}

/** The largest integer zoom in `[MIN_ZOOM, cap]` whose grid fits `area`, so the whole grid stays
 *  visible even when a tiling WM clamps the window below the size App asked for. The displayed zoom —
 *  SystemGrid renders at this, and `hitTestTile` maps drop coordinates through it. */
export function fitZoom(count: number, layout: SystemLayout, cap: number, area: { width: number; height: number }): number {
  const shape = shapeFor(layout, Math.max(count, 1));
  for (let z = Math.min(cap, MAX_ZOOM); z > MIN_ZOOM; z--) {
    if (shape.cols * tileWidth(z) <= area.width && shape.rows * tileHeight(z) <= area.height) return z;
  }
  return MIN_ZOOM;
}

/** Map a window-pixel coordinate to the grid tile index under it, or `null` when it misses the grid
 *  (outside the content area, or an empty cell in a partially-filled last row). Replicates SystemGrid's
 *  layout exactly — the `fitZoom`-capped displayed zoom + the grid CENTERED in the window (SystemGrid's
 *  align/justify-center) — so a dropped file lands on the tile it was visibly dropped onto. `projectZoom`
 *  is the project's `settings.zoom`; `defaultZoom` the user default (used when the project zoom is out of
 *  range), matching SystemGrid's `resolvedZoom`. */
export function hitTestTile(
  x: number,
  y: number,
  count: number,
  layout: SystemLayout,
  projectZoom: number,
  defaultZoom: number,
  windowSize: { width: number; height: number },
): number | null {
  if (count <= 0) return null;
  const cap = projectZoom >= MIN_ZOOM && projectZoom <= MAX_ZOOM ? projectZoom : defaultZoom;
  const zoom = fitZoom(count, layout, cap, windowSize);
  const shape = shapeFor(layout, count);
  const tw = tileWidth(zoom);
  const th = tileHeight(zoom);
  const gridW = shape.cols * tw;
  const gridH = shape.rows * th;
  // The grid is centered in the window (SystemGrid's outer Box) — offset the hit point into grid space.
  const gx = x - (windowSize.width - gridW) / 2;
  const gy = y - (windowSize.height - gridH) / 2;
  if (gx < 0 || gy < 0 || gx >= gridW || gy >= gridH) return null;
  const index = Math.floor(gy / th) * shape.cols + Math.floor(gx / tw);
  return index >= 0 && index < count ? index : null;
}

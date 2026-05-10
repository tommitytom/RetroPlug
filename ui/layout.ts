// Shared layout constants for the React shell. Tile pixel dimensions are
// Game Boy native (160x144) at the integer ZOOM. Promoting ZOOM to a
// ProjectSettings field is a future task — for now it's a single source
// of truth for the TS layer.

export const ZOOM = 3;
export const GB_NATIVE_W = 160;
export const GB_NATIVE_H = 144;
export const TILE_W = GB_NATIVE_W * ZOOM;
export const TILE_H = GB_NATIVE_H * ZOOM;

export interface GridShape {
    cols: number;
    rows: number;
}

// 1=center, 2=row, 3-4=2x2, 5-9=3x3, 10-16=4x4. Beyond that we square it.
// Mirrors the heuristic in SystemGrid::shapeFor (Auto branch).
export function autoShape(count: number): GridShape {
    if (count <= 1)  return { cols: 1, rows: 1 };
    if (count === 2) return { cols: 2, rows: 1 };
    if (count <= 4)  return { cols: 2, rows: 2 };
    if (count <= 9)  return { cols: 3, rows: 3 };
    if (count <= 16) return { cols: 4, rows: 4 };
    const cols = Math.ceil(Math.sqrt(count));
    return { cols, rows: Math.ceil(count / cols) };
}

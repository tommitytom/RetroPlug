// Shared layout constants for the React shell. The integer zoom factor
// is now a runtime value (1..6) sourced from ProjectSettings.zoom with a
// user-config fallback (PluginRpcService::getZoom resolves the inheritance).
// PluginUI owns the state and threads it down as a `zoom` prop; helpers
// below compute pixel dimensions from a zoom argument.

export const GB_NATIVE_W = 160;
export const GB_NATIVE_H = 144;

// Fallback used before the first RPC resolves. Matches the historical
// constant and also serves as the user-config default in C++.
export const DEFAULT_ZOOM = 3;

export function tileWidth(zoom: number): number {
    return GB_NATIVE_W * zoom;
}

export function tileHeight(zoom: number): number {
    return GB_NATIVE_H * zoom;
}

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

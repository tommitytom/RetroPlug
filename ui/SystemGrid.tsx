import { View } from "lvgljs-ui";

import { EmulatorTile } from "./EmulatorTile";
import { autoShape, GridShape, tileWidth, tileHeight } from "./layout";
import type { SystemEntry } from "./plugin/client";

// Tile arrangement options. Mirrors C++ SystemLayout enum
// (src/project/ProjectConfig.hpp).
export enum SystemLayout {
    Auto   = 0,
    Row    = 1,
    Column = 2,
    Grid   = 3,
}

// Re-exported from plugin/client so callers can `import { SystemEntry }
// from "./SystemGrid"` as before — but the underlying type comes from the
// generated RPC schema. The lsdjSyncMode / lsdjTempoDivisor fields are
// present only when the system has an LSDJ sync role attached.
export type { SystemEntry };

interface SystemGridProps {
    systems:    SystemEntry[];
    focusedId:  number;
    layout?:    SystemLayout;
    // Integer zoom 1..6. Drives every tile / grid dimension below.
    zoom:       number;
}

function shapeFor(layout: SystemLayout, count: number): GridShape {
    switch (layout) {
        case SystemLayout.Row:    return { cols: count, rows: 1 };
        case SystemLayout.Column: return { cols: 1, rows: count };
        case SystemLayout.Grid:   return autoShape(count);
        case SystemLayout.Auto:
        default:                  return autoShape(count);
    }
}

// Compute the pixel size of the content area for a given system count,
// layout, and zoom. Exported so PluginUI can pass it to plugin.setWindowSize.
export function gridContentSize(systems: SystemEntry[],
                                layout: SystemLayout = SystemLayout.Auto,
                                zoom: number)
    : { width: number; height: number; shape: GridShape } {
    const shape = shapeFor(layout, Math.max(systems.length, 1));
    return {
        width:  shape.cols * tileWidth(zoom),
        height: shape.rows * tileHeight(zoom),
        shape,
    };
}

// Tile screen rect (in grid-local coordinates) for the system at `index`.
// Returns coordinates inside the grid's content box; the grid itself is
// flex-centered by its parent, so callers that need window-coordinates
// must add the grid's centering offset.
export function getTileBounds(index: number,
                              count: number,
                              layout: SystemLayout = SystemLayout.Auto,
                              zoom: number)
    : { x: number; y: number; w: number; h: number } {
    const shape = shapeFor(layout, Math.max(count, 1));
    const col = index % shape.cols;
    const row = Math.floor(index / shape.cols);
    const w = tileWidth(zoom);
    const h = tileHeight(zoom);
    return { x: col * w, y: row * h, w, h };
}

/**
 * Multi-instance tile container. Renders the tiles edge-to-edge in a
 * fixed-pixel-size box and centers that box inside whatever space the
 * window provides. No padding, no border, no rounded corners — and no
 * empty-state placeholder; when there are no systems, the React tree
 * is just a black background and the menu (rendered above by
 * PluginUI) covers the whole window.
 */
export function SystemGrid({ systems, focusedId, layout = SystemLayout.Auto, zoom }: SystemGridProps) {
    if (systems.length === 0) return null;

    const { width, height, shape } = gridContentSize(systems, layout, zoom);
    const tw = tileWidth(zoom);
    const th = tileHeight(zoom);
    const rows: SystemEntry[][] = [];
    for (let r = 0; r < shape.rows; r++) {
        rows.push(systems.slice(r * shape.cols, (r + 1) * shape.cols));
    }

    // Centered by the parent's flex layout (PluginUI root). This component
    // just lays out tiles in a fixed-pixel-size grid; positioning is the
    // parent's job.
    return (
        <View
            style={{
                width:  width,
                height: height,
                "background-color": "#000000",
                "background-opacity": 255,
                "border-width": 0,
                "border-opacity": 0,
                "border-radius": 0,
                "padding-left":  0,
                "padding-right": 0,
                "padding-top":   0,
                "padding-bottom":0,
                display: "flex",
                "flex-direction": "column",
                "row-spacing": 0,
                "column-spacing": 0,
                overflow: "hidden",
            }}
        >
            {rows.map((row, ri) => (
                <View
                    key={`row-${ri}`}
                    style={{
                        width:  shape.cols * tw,
                        height: th,
                        "background-opacity": 0,
                        "border-width": 0,
                        "border-opacity": 0,
                        "border-radius": 0,
                        "padding-left":  0,
                        "padding-right": 0,
                        "padding-top":   0,
                        "padding-bottom":0,
                        display: "flex",
                        "flex-direction": "row",
                        "row-spacing": 0,
                        "column-spacing": 0,
                        overflow: "hidden",
                    }}
                >
                    {row.map((sys) => (
                        <EmulatorTile
                            key={`tile-${sys.id}`}
                            systemId={sys.id}
                            focused={sys.id === focusedId || systems.length === 1}
                            zoom={zoom}
                        />
                    ))}
                </View>
            ))}
        </View>
    );
}

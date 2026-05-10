import { Text, View } from "lvgljs-ui";

import { EmulatorTile } from "./EmulatorTile";

// Tile arrangement options. Mirrors C++ SystemLayout enum
// (src/project/ProjectConfig.hpp).
export enum SystemLayout {
    Auto   = 0,
    Row    = 1,
    Column = 2,
    Grid   = 3,
}

export interface SystemEntry {
    id: number;
    kind?: string;
    gainDb?: number;
    linkGroupId?: number;
}

interface SystemGridProps {
    systems:    SystemEntry[];
    focusedId:  number;
    layout?:    SystemLayout;
}

interface GridShape {
    cols: number;
    rows: number;
}

// Auto-layout heuristic. Matches the legacy "looks reasonable for N" rule:
// 1=center, 2=row, 3-4=2x2, 5-9=3x3, 10-16=4x4. For N>16 fall back to a
// square-ish grid.
function autoShape(count: number): GridShape {
    if (count <= 1) return { cols: 1, rows: 1 };
    if (count === 2) return { cols: 2, rows: 1 };
    if (count <= 4) return { cols: 2, rows: 2 };
    if (count <= 9) return { cols: 3, rows: 3 };
    if (count <= 16) return { cols: 4, rows: 4 };
    const cols = Math.ceil(Math.sqrt(count));
    return { cols, rows: Math.ceil(count / cols) };
}

function shapeFor(layout: SystemLayout, count: number): GridShape {
    switch (layout) {
        case SystemLayout.Row:    return { cols: count, rows: 1 };
        case SystemLayout.Column: return { cols: 1, rows: count };
        case SystemLayout.Grid:   return autoShape(count);
        case SystemLayout.Auto:
        default:
            return autoShape(count);
    }
}

/**
 * Multi-instance tile container. Arranges N <EmulatorTile/>s in a grid
 * driven by `layout`. Renders an "Esc → Load ROM" placeholder when empty.
 */
export function SystemGrid({ systems, focusedId, layout = SystemLayout.Auto }: SystemGridProps) {
    if (systems.length === 0) {
        return (
            <View
                style={{
                    width: "100%",
                    height: "100%",
                    "background-opacity": 0,
                    "border-opacity": 0,
                    display: "flex",
                    "align-items": "center",
                    "justify-content": "center",
                }}
            >
                <Text style={{ "text-color": "#888", "font-size": 18 }}>
                    No instances - press Esc, Load ROM
                </Text>
            </View>
        );
    }

    const shape = shapeFor(layout, systems.length);
    const rows: SystemEntry[][] = [];
    for (let r = 0; r < shape.rows; r++) {
        rows.push(systems.slice(r * shape.cols, (r + 1) * shape.cols));
    }

    return (
        <View
            style={{
                width: "100%",
                height: "100%",
                "background-opacity": 0,
                "border-opacity": 0,
                display: "flex",
                "flex-direction": "column",
            }}
        >
            {rows.map((row, ri) => (
                <View
                    key={`row-${ri}`}
                    style={{
                        width: "100%",
                        height: `${100 / shape.rows}%`,
                        "background-opacity": 0,
                        "border-opacity": 0,
                        display: "flex",
                        "flex-direction": "row",
                    }}
                >
                    {row.map((sys) => (
                        <View
                            key={`tile-${sys.id}`}
                            style={{
                                width: `${100 / shape.cols}%`,
                                height: "100%",
                                "background-opacity": 0,
                                "border-opacity": 0,
                            }}
                        >
                            <EmulatorTile systemId={sys.id} focused={sys.id === focusedId} />
                        </View>
                    ))}
                </View>
            ))}
        </View>
    );
}

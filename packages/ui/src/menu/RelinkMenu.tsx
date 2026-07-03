import { useCallback } from "react";
import { off, on } from "lvgljs";

import { tileWidth, tileHeight } from "../layout";
import { Menu } from "./Menu";
import type { MenuItem, MenuTree } from "./menuDefs";
import { plugin } from "../plugin/client";
import type { RpMissingFile } from "plugin-service";

interface RelinkMenuProps {
    missing:   RpMissingFile[];
    zoom:      number;
    sinkGroup: any;
}

// Strip a path down to its filename for display.
function basename(path: string): string {
    const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
    return i >= 0 ? path.slice(i + 1) : path;
}

// openRelinkBrowser is fire-and-forget on the server; the chosen path arrives
// via the one-shot "relink-path-selected" event. Mirrors KitEditor.pickSampleFile.
// `kind` ("rom" | "sram" | "sample") selects the browser's file-type filter.
function pickRelinkFile(kind: string): Promise<string | null> {
    return new Promise((resolve) => {
        let timer: ReturnType<typeof setTimeout> | null = null;
        const handler = (path: string) => {
            off("relink-path-selected", handler);
            if (timer) clearTimeout(timer);
            resolve(path && path.length > 0 ? path : null);
        };
        on("relink-path-selected", handler);
        void plugin.$notify("openRelinkBrowser", kind);
        timer = setTimeout(() => {
            off("relink-path-selected", handler);
            resolve(null);
        }, 5 * 60 * 1000);
    });
}

// Human label for a missing item's kind.
function kindLabel(kind: string): string {
    if (kind === "rom")  return "ROM";
    if (kind === "sram") return "save";
    return "sample";
}

// Shown (in place of the normal screen) when a loaded project references files
// that no longer exist. Each entry locates one missing file; resolving the last
// one commits the load (server emits "project-loaded"). Esc cancels the load.
//
// Reuses the standard <Menu> so the styling matches the rest of the app for free.
export function RelinkMenu({ missing, zoom, sinkGroup }: RelinkMenuProps) {
    const onClose = useCallback(() => {
        void plugin.$notify("cancelMissingFiles");
    }, []);

    const items: MenuItem[] = missing.map((m) => {
        return {
            // itemKind is load-bearing: a "rom" and an "sram" item for the same
            // system both default kitSlot/sampleIndex to -1, so without it the two
            // rows collapse to one key and the second becomes unfocusable.
            id:    `relink-${m.itemKind}-${m.systemIndex}-${m.kitSlot}-${m.sampleIndex}`,
            label: `Locate ${kindLabel(m.itemKind)}: ${basename(m.path)}`,
            kind:  "action",
            // Keep the menu open while browsing — activating must NOT close it
            // (that would cancel the pending load). It closes on its own when
            // the last file resolves and the load commits (project-loaded).
            keepOpen: true,
            onSelect: () => {
                void (async () => {
                    const picked = await pickRelinkFile(m.itemKind);
                    if (!picked) return;
                    // The result (and a re-emitted "missing-files" / "project-loaded")
                    // drives PluginUI's state; nothing to do with it here.
                    await plugin.relinkMissingFile(
                        m.systemIndex, m.itemKind, m.kitSlot, m.sampleIndex, picked);
                })();
            },
        };
    });

    const tree: MenuTree = { title: "Locate missing files", items };

    return (
        <Menu
            width={tileWidth(zoom)}
            height={tileHeight(zoom)}
            zoom={zoom}
            tree={tree}
            onClose={onClose}
            sinkGroup={sinkGroup}
        />
    );
}

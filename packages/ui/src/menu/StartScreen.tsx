import { useCallback } from "react";

import { tileWidth, tileHeight } from "../layout";
import { Menu } from "./Menu";
import { buildStartMenu, type RecentEntry } from "./menuDefs";
import type { BindingsEditor } from "../useBindingsEditor";

interface StartScreenProps {
    midiRouting:     number;
    audioRouting:    number;
    layout:          number;
    zoom:            number;
    projectZoom:     number;
    autoSaveSram:    boolean;
    defaultZoom:     number;
    recentFiles:     RecentEntry[];
    openAbout:       () => void;
    keyboardEditor:  BindingsEditor;
    gamepadEditor:   BindingsEditor;
    sinkGroup:       any;
}

// Empty-project landing. Renders the start menu inside a tile-sized panel,
// centered in the window by the root flex layout in PluginUI.
//
// Submenu navigation is in-place (children expand inline below their parent
// — see Menu.tsx). No pane-stack, no Back item.
export function StartScreen({
    midiRouting, audioRouting, layout, zoom, projectZoom, autoSaveSram, defaultZoom, recentFiles, openAbout,
    keyboardEditor, gamepadEditor, sinkGroup,
}: StartScreenProps) {
    // Esc on the start screen must NOT close the menu (the empty-project
    // invariant — see PluginUI's useKeyboard handler, which short-circuits
    // Esc when systems.length === 0). Defence in depth: even if some code
    // path inside Menu calls onClose(), it's a no-op here.
    const onClose = useCallback(() => { /* no-op */ }, []);

    const tree = buildStartMenu({
        systems:       [],
        focusedSystem: undefined,
        midiRouting,
        audioRouting,
        layout,
        zoom,
        projectZoom,
        autoSaveSram,
        defaultZoom,
        recentFiles,
        openKitEditor: () => { /* unreachable from start menu */ },
        openAbout,
        keyboardEditor,
        gamepadEditor,
    });

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

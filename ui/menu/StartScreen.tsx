import { useCallback } from "react";

import { TILE_W, TILE_H } from "../layout";
import { Menu } from "./Menu";
import { buildStartMenu } from "./menuDefs";

interface StartScreenProps {
    midiRouting:   number;
    currentPaneId: string;
    onPush:        (paneId: string) => void;
    onPop:         () => void;
    sinkGroup:     any;
}

// Empty-project landing. Renders the start menu inside a tile-sized panel,
// centered in the window by the root flex layout in PluginUI.
//
// The start menu can't bind any per-instance actions (there are no
// instances yet), so it provides Load, Recent, Project, Settings, About.
// Project / Settings panes are the same submenus from the per-instance
// menu — exposed here so the user can configure project-wide and global
// settings before adding any tile.
export function StartScreen({
    midiRouting, currentPaneId, onPush, onPop, sinkGroup,
}: StartScreenProps) {
    const onClose = useCallback(() => {
        // Esc on the start screen does nothing — the menu must stay open
        // when the project is empty. PluginUI already enforces this at the
        // useKeyboard layer; this no-op is just defence in depth in case a
        // future change wires a close action through here.
    }, []);

    const panes = buildStartMenu({
        systems:       [],
        focusedSystem: undefined,
        midiRouting,
        closeMenu:     onClose,
        openKitEditor: () => { /* unreachable from start menu */ },
    });

    return (
        <Menu
            width={TILE_W}
            height={TILE_H}
            panes={panes}
            currentPaneId={currentPaneId}
            onPush={onPush}
            onPop={onPop}
            onClose={onClose}
            sinkGroup={sinkGroup}
        />
    );
}

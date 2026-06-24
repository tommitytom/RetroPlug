import { useCallback } from "react";
import { off, on } from "lvgljs";

import { tileWidth, tileHeight } from "../layout";
import { Menu } from "./Menu";
import type { MenuItem, MenuTree } from "./menuDefs";
import { plugin } from "../plugin/client";

interface UnsavedChangesModalProps {
    zoom:      number;
    onClose:   () => void;   // dismiss (Cancel / Esc); the close was already vetoed
    sinkGroup: any;
}

// Wait once for a C++ event (e.g. project-saved), with a timeout so a cancelled
// save browser doesn't leak the listener. Mirrors RelinkMenu's pickRelinkFile.
function once(channel: string, timeoutMs = 5 * 60 * 1000): Promise<boolean> {
    return new Promise((resolve) => {
        let timer: ReturnType<typeof setTimeout> | null = null;
        const handler = () => {
            off(channel, handler);
            if (timer) clearTimeout(timer);
            resolve(true);
        };
        on(channel, handler);
        timer = setTimeout(() => { off(channel, handler); resolve(false); }, timeoutMs);
    });
}

// Shown (top priority) when the standalone window-close was vetoed because the
// project or a cartridge's SRAM has unsaved changes. Save / Discard / Cancel.
// Rendered through the standard <Menu> so the styling matches the app.
export function UnsavedChangesModal({ zoom, onClose, sinkGroup }: UnsavedChangesModalProps) {
    const onSaveQuit = useCallback(() => {
        void (async () => {
            await plugin.saveDirtySram();
            const path = await plugin.getCurrentProjectPath();
            if (path && path.length > 0) {
                await plugin.saveProject();              // silent save to known path
                void plugin.$notify("quitStandalone");
            } else {
                // No path yet — open the save browser and quit once it lands.
                void plugin.$notify("openSaveProjectBrowser");
                if (await once("project-saved")) void plugin.$notify("quitStandalone");
                // (Cancelled save ⇒ stay open; the user can choose again.)
            }
        })();
    }, []);

    const items: MenuItem[] = [
        // keepOpen so activating doesn't trigger the menu's own onClose (which is
        // our Cancel). Each action drives the outcome explicitly.
        { id: "saveQuit",    label: "Save & Quit",    kind: "action", keepOpen: true, onSelect: onSaveQuit },
        { id: "discardQuit", label: "Discard & Quit", kind: "action", keepOpen: true,
          onSelect: () => { void plugin.$notify("quitStandalone"); } },
        { id: "cancel",      label: "Cancel",         kind: "action", keepOpen: true,
          onSelect: () => onClose() },
    ];

    const tree: MenuTree = { title: "Unsaved changes", items };

    return (
        <Menu
            width={tileWidth(zoom)}
            height={tileHeight(zoom)}
            zoom={zoom}
            tree={tree}
            onClose={onClose}   // Esc dismisses (close stays vetoed)
            sinkGroup={sinkGroup}
        />
    );
}

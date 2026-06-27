import { useCallback } from "react";
import { off, on } from "lvgljs";

import { tileWidth, tileHeight } from "../layout";
import { Menu } from "./Menu";
import type { MenuItem, MenuTree } from "./menuDefs";
import { plugin } from "../plugin/client";

// What to carry out once the user resolves the unsaved-changes prompt. "quit"
// closes the standalone window; "new" / "loadBrowser" discard the current
// project for a new or freshly-loaded one. Save and Discard both end by
// performing this; Cancel aborts it (the current project is kept).
export type UnsavedIntent =
    | { kind: "quit" }
    | { kind: "new" }
    | { kind: "loadBrowser" };

interface UnsavedChangesModalProps {
    zoom:      number;
    intent:    UnsavedIntent;
    onClose:   () => void;   // dismiss (Cancel / Esc); the current project is kept
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

// Carry out the destructive action the prompt was guarding.
function proceed(intent: UnsavedIntent) {
    switch (intent.kind) {
        case "quit":        void plugin.$notify("quitStandalone");         break;
        case "new":         void plugin.$notify("newProject");             break;
        case "loadBrowser": void plugin.$notify("openLoadProjectBrowser"); break;
    }
}

// Shown (top priority) when an action would discard a project that has unsaved
// changes — the standalone window-close, or the menu's New / Load Project.
// Save / Discard / Cancel, rendered through the standard <Menu> for app styling.
export function UnsavedChangesModal({ zoom, intent, onClose, sinkGroup }: UnsavedChangesModalProps) {
    const onSave = useCallback(() => {
        void (async () => {
            await plugin.saveDirtySram();
            const path = await plugin.getCurrentProjectPath();
            if (path && path.length > 0) {
                await plugin.saveProject();   // silent save to known path
                proceed(intent);
                onClose();
            } else {
                // No path yet — open the save browser and proceed once it lands.
                void plugin.$notify("openSaveProjectBrowser");
                if (await once("project-saved")) { proceed(intent); onClose(); }
                // (Cancelled save ⇒ stay open; the user can choose again.)
            }
        })();
    }, [intent, onClose]);

    const onDiscard = useCallback(() => { proceed(intent); onClose(); }, [intent, onClose]);

    // "Quit" for the window-close case; "Continue" for New / Load Project.
    const verb = intent.kind === "quit" ? "Quit" : "Continue";

    const items: MenuItem[] = [
        // keepOpen so activating doesn't trigger the menu's own onClose (our
        // Cancel). Each action drives the outcome explicitly.
        { id: "save",    label: `Save & ${verb}`,    kind: "action", keepOpen: true, onSelect: onSave },
        { id: "discard", label: `Discard & ${verb}`, kind: "action", keepOpen: true, onSelect: onDiscard },
        { id: "cancel",  label: "Cancel",            kind: "action", keepOpen: true, onSelect: () => onClose() },
    ];

    const tree: MenuTree = { title: "Unsaved changes", items };

    return (
        <Menu
            width={tileWidth(zoom)}
            height={tileHeight(zoom)}
            zoom={zoom}
            tree={tree}
            onClose={onClose}   // Esc dismisses (the current project is kept)
            sinkGroup={sinkGroup}
        />
    );
}

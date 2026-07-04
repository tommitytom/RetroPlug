import { tileWidth, tileHeight } from "../layout";
import { Menu } from "./Menu";
import type { MenuItem, MenuTree } from "./menuDefs";

interface IncompatibleProjectModalProps {
    zoom:      number;
    onClose:   () => void;   // dismiss (OK / Esc)
    sinkGroup: any;
}

// Shown when C++ refuses to load a project whose stamped schemaVersion is newer
// than this build understands (see config/SchemaVersions.hpp — a format from the
// future can't be safely read). Message-only: the title carries the reason and a
// single OK dismisses. Rendered through the standard <Menu> for app styling,
// mirroring UnsavedChangesModal.
export function IncompatibleProjectModal({ zoom, onClose, sinkGroup }: IncompatibleProjectModalProps) {
    const items: MenuItem[] = [
        { id: "ok", label: "OK", kind: "action", onSelect: () => onClose() },
    ];

    const tree: MenuTree = {
        title: "Project saved by a newer version of RetroPlug",
        items,
    };

    return (
        <Menu
            width={tileWidth(zoom)}
            height={tileHeight(zoom)}
            zoom={zoom}
            tree={tree}
            onClose={onClose}   // Esc dismisses
            sinkGroup={sinkGroup}
        />
    );
}

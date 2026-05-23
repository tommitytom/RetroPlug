// Declarative menu tree. Pure data — no LVGL, no React.
//
// Each pane has an ordered list of items; selecting a "submenu" item pushes
// the target pane onto the back-stack maintained by MenuStack.
//
// Stubs: many entries fire a console.warn handler today because the C++ side
// has no RPC for them yet. See the menu redesign plan for the follow-up list.

import { plugin, type SystemEntry } from "../plugin/client";

export type MenuItemKind = "action" | "submenu" | "back";

export interface MenuItem {
    id:        string;
    label:     string;
    kind:      MenuItemKind;
    submenu?:  string;          // present iff kind === "submenu"
    onSelect?: () => void;      // present iff kind === "action" | "back"
    // True = clicking this item should NOT close the menu (e.g. cycling
    // labels like "Link group" / "MIDI routing" / "LSDJ mode"). The keep-
    // open behavior matches the previous flat-menu behaviour for those.
    keepOpen?: boolean;
}

export interface MenuPane {
    id:    string;
    title: string;
    items: MenuItem[];
}

export interface MenuContext {
    systems:        SystemEntry[];
    focusedSystem?: SystemEntry;
    midiRouting:    number;
    // Called by the Menu component after a non-`keepOpen` action fires.
    closeMenu:      () => void;
    // Called by Menu when the user picks Kit Editor.
    openKitEditor:  () => void;
}

// Mirrors C++ MidiRouting enum (src/project/ProjectConfig.hpp).
const MIDI_ROUTING_NAMES = [
    "Send to all",
    "4 ch / inst",
    "1 ch / inst",
    "ch -> inst",
];

// Mirrors C++ LsdjSyncMode enum (src/system/sameboy/roles/LsdjSyncRole.hpp).
const LSDJ_MODE_NAMES = [
    "Off",
    "MidiSync",
    "Arduinoboy",
    "MidiMap",
    "Keyboard",
    "KeyboardMidi",
    "Passthrough",
    "MI.OUT",
];

const LINK_GROUP_MAX = 4;

function stub(label: string): () => void {
    return () => {
        console.warn(`[menu] "${label}" is a stub — no RPC wired yet`);
    };
}

// Build the root pane for the per-instance menu. Some items are conditional
// on whether an LSDJ role is attached to the focused system.
function buildRootPane(ctx: MenuContext): MenuPane {
    const sys = ctx.focusedSystem;
    const linkGroupId = sys?.linkGroupId ?? 0;
    const linkSuffix  = sys == null
        ? "-"
        : linkGroupId === 0 ? "Off" : String(linkGroupId);
    const hasLsdjRole = sys?.lsdjSyncMode != null;
    const hasKitRole  = sys?.hasLsdjKitRole === true;
    const lsdjMode    = hasLsdjRole
        ? LSDJ_MODE_NAMES[sys!.lsdjSyncMode ?? 0] ?? LSDJ_MODE_NAMES[0]
        : "";

    const items: MenuItem[] = [
        { id: "recent",          label: "Recent >",                 kind: "submenu", submenu: "recent" },
        { id: "loadRom",         label: "Load ROM...",                kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
        { id: "addInstance",     label: "Add instance",             kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "add" }); } },
        { id: "removeInstance",  label: "Remove instance",          kind: "action",
          onSelect: () => {
              if (sys) void plugin.$notify("removeSystem", sys.id);
          } },
        { id: "linkGroup",       label: `Link group: ${linkSuffix}`, kind: "action", keepOpen: true,
          onSelect: () => {
              if (!sys) return;
              const next = (((sys.linkGroupId ?? 0) + 1) % LINK_GROUP_MAX);
              void plugin.$notify("setLinkGroupId", sys.id, next);
          } },
    ];

    if (hasLsdjRole) {
        items.push({
            id: "lsdjMode", label: `LSDJ mode: ${lsdjMode}`, kind: "action", keepOpen: true,
            onSelect: () => {
                if (!sys || sys.lsdjSyncMode == null) return;
                const next = (sys.lsdjSyncMode + 1) % LSDJ_MODE_NAMES.length;
                void plugin.$notify("setLsdjSyncConfig", sys.id, next, sys.lsdjTempoDivisor ?? 1);
            },
        });
    }
    if (hasKitRole) {
        items.push({
            id: "kitEditor", label: "Kit Editor", kind: "action",
            onSelect: () => ctx.openKitEditor(),
        });
    }

    items.push(
        { id: "system",   label: "System >",   kind: "submenu", submenu: "system" },
        { id: "project",  label: "Project >",  kind: "submenu", submenu: "project" },
        { id: "settings", label: "Settings >", kind: "submenu", submenu: "settings" },
        { id: "about",    label: "About",      kind: "action", onSelect: stub("About") },
    );

    const title = sys ? `RetroPlug - System #${sys.id}` : "RetroPlug";
    return { id: "root", title, items };
}

function backItem(): MenuItem {
    // onSelect is handled specially by Menu — it calls pop() on the stack.
    return { id: "back", label: "< Back", kind: "back" };
}

function buildRecentPane(): MenuPane {
    return {
        id: "recent",
        title: "Recent",
        items: [
            backItem(),
            { id: "recentEmpty", label: "(no recent files)", kind: "action", onSelect: stub("Recent file load") },
        ],
    };
}

function buildSystemPane(ctx: MenuContext): MenuPane {
    const sys = ctx.focusedSystem;
    const gainLabel = sys?.gainDb != null ? `Gain: ${sys.gainDb.toFixed(1)} dB` : "Gain: -";
    return {
        id: "system",
        title: "System",
        items: [
            backItem(),
            { id: "reset",      label: "Reset",                 kind: "action", onSelect: stub("Reset") },
            { id: "saveSram",   label: "Save SRAM",             kind: "action", onSelect: stub("Save SRAM") },
            { id: "saveSramAs", label: "Save SRAM As...",         kind: "action", onSelect: stub("Save SRAM As") },
            { id: "saveState",  label: "Save State",            kind: "action", onSelect: stub("Save State") },
            { id: "saveStateAs",label: "Save State As...",        kind: "action", onSelect: stub("Save State As") },
            { id: "newSram",    label: "New SRAM",              kind: "action", onSelect: stub("New SRAM") },
            { id: "duplicate",  label: "Duplicate",             kind: "action", onSelect: stub("Duplicate") },
            { id: "reloadRom",  label: "Reload on ROM change",  kind: "action", onSelect: stub("Reload on ROM change"), keepOpen: true },
            { id: "gain",       label: gainLabel,               kind: "action", onSelect: stub("Gain"),                keepOpen: true },
            { id: "model",      label: "Model: -",              kind: "action", onSelect: stub("Model"),               keepOpen: true },
            { id: "fastBoot",   label: "Fast boot",             kind: "action", onSelect: stub("Fast boot"),           keepOpen: true },
        ],
    };
}

function buildProjectPane(ctx: MenuContext): MenuPane {
    const routingName = MIDI_ROUTING_NAMES[ctx.midiRouting] ?? MIDI_ROUTING_NAMES[0];
    return {
        id: "project",
        title: "Project",
        items: [
            backItem(),
            { id: "saveProject", label: "Save project", kind: "action",
              onSelect: () => { void plugin.$notify("openSaveProjectBrowser"); } },
            { id: "loadProject", label: "Load project", kind: "action",
              onSelect: () => { void plugin.$notify("openLoadProjectBrowser"); } },
            { id: "midiRouting", label: `MIDI routing: ${routingName}`, kind: "action", keepOpen: true,
              onSelect: () => {
                  const next = (ctx.midiRouting + 1) % MIDI_ROUTING_NAMES.length;
                  void plugin.$notify("setMidiRouting", next);
              } },
            { id: "layout",       label: "Layout: -",       kind: "action", onSelect: stub("Layout"),       keepOpen: true },
            { id: "zoom",         label: "Zoom: -",         kind: "action", onSelect: stub("Zoom"),         keepOpen: true },
            { id: "audioRouting", label: "Audio routing: -", kind: "action", onSelect: stub("Audio routing"), keepOpen: true },
            { id: "autoSave",     label: "Auto save",       kind: "action", onSelect: stub("Auto save"),    keepOpen: true },
        ],
    };
}

function buildSettingsPane(): MenuPane {
    return {
        id: "settings",
        title: "Settings",
        items: [
            backItem(),
            { id: "keyboardProfile", label: "Keyboard profile: -", kind: "action", onSelect: stub("Keyboard profile"), keepOpen: true },
            { id: "padProfile",      label: "Pad profile: -",      kind: "action", onSelect: stub("Pad profile"),      keepOpen: true },
            { id: "audioDevice",     label: "Audio device: -",     kind: "action", onSelect: stub("Audio device"),     keepOpen: true },
            { id: "openSettings",    label: "Open settings folder", kind: "action", onSelect: stub("Open settings folder") },
        ],
    };
}

// Per-instance menu (opens over a focused tile). Recomputed every time
// the menu (re-)renders, so labels with live state (link group, MIDI
// routing, LSDJ mode) stay current as cycling fires.
export function buildInstanceMenu(ctx: MenuContext): Record<string, MenuPane> {
    return {
        root:     buildRootPane(ctx),
        recent:   buildRecentPane(),
        system:   buildSystemPane(ctx),
        project:  buildProjectPane(ctx),
        settings: buildSettingsPane(),
    };
}

// Start screen menu (no instances yet). Contains a Load entry plus the
// project / settings submenus from the per-instance menu so users can
// configure things before they've added any tile.
export function buildStartMenu(ctx: MenuContext): Record<string, MenuPane> {
    const root: MenuPane = {
        id: "root",
        title: "RetroPlug",
        items: [
            { id: "load",     label: "Load...", kind: "action",
              onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
            { id: "recent",   label: "Recent >",   kind: "submenu", submenu: "recent" },
            { id: "project",  label: "Project >",  kind: "submenu", submenu: "project" },
            { id: "settings", label: "Settings >", kind: "submenu", submenu: "settings" },
            { id: "about",    label: "About",      kind: "action", onSelect: stub("About") },
        ],
    };
    return {
        root,
        recent:   buildRecentPane(),
        project:  buildProjectPane(ctx),
        settings: buildSettingsPane(),
    };
}

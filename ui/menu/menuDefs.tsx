// Declarative menu tree. Pure data — no LVGL, no React.
//
// Each submenu item nests its children inline (`children: MenuItem[]`).
// Activating a submenu item toggles its expansion in the Menu component's
// local `openItems` set; expanded submenus' children render indented below
// the parent in the same flat scrollable list. There is no separate pane
// state — see v0.5's MenuView.cpp for the equivalent C++ pattern.
//
// Stubs: many entries fire a console.warn handler today because the C++ side
// has no RPC for them yet. See the menu redesign plan for the follow-up list.

import { plugin, type SystemEntry } from "../plugin/client";

export type MenuItemKind = "action" | "submenu";

export interface MenuItem {
    id:        string;
    label:     string;
    kind:      MenuItemKind;
    children?: MenuItem[];      // present iff kind === "submenu"
    onSelect?: () => void;      // present iff kind === "action"
    // True = activating this item should NOT close the menu (e.g. cycling
    // labels like "Link group" / "MIDI routing" / "LSDJ mode").
    keepOpen?: boolean;
}

export interface MenuTree {
    title: string;
    items: MenuItem[];
}

export interface MenuContext {
    systems:        SystemEntry[];
    focusedSystem?: SystemEntry;
    midiRouting:    number;
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

function recentChildren(): MenuItem[] {
    return [
        { id: "recentEmpty", label: "(no recent files)", kind: "action",
          onSelect: stub("Recent file load"), keepOpen: true },
    ];
}

function systemChildren(ctx: MenuContext): MenuItem[] {
    const sys = ctx.focusedSystem;
    const gainLabel = sys?.gainDb != null ? `Gain: ${sys.gainDb.toFixed(1)} dB` : "Gain: -";
    return [
        { id: "reset",      label: "Reset",                kind: "action", onSelect: stub("Reset") },
        { id: "saveSram",   label: "Save SRAM",            kind: "action", onSelect: stub("Save SRAM") },
        { id: "saveSramAs", label: "Save SRAM As...",      kind: "action", onSelect: stub("Save SRAM As") },
        { id: "saveState",  label: "Save State",           kind: "action", onSelect: stub("Save State") },
        { id: "saveStateAs",label: "Save State As...",     kind: "action", onSelect: stub("Save State As") },
        { id: "newSram",    label: "New SRAM",             kind: "action", onSelect: stub("New SRAM") },
        { id: "duplicate",  label: "Duplicate",            kind: "action", onSelect: stub("Duplicate") },
        { id: "reloadRom",  label: "Reload on ROM change", kind: "action", onSelect: stub("Reload on ROM change"), keepOpen: true },
        { id: "gain",       label: gainLabel,              kind: "action", onSelect: stub("Gain"),                keepOpen: true },
        { id: "model",      label: "Model: -",             kind: "action", onSelect: stub("Model"),               keepOpen: true },
        { id: "fastBoot",   label: "Fast boot",            kind: "action", onSelect: stub("Fast boot"),           keepOpen: true },
    ];
}

function projectChildren(ctx: MenuContext): MenuItem[] {
    const routingName = MIDI_ROUTING_NAMES[ctx.midiRouting] ?? MIDI_ROUTING_NAMES[0];
    return [
        { id: "saveProject", label: "Save project", kind: "action",
          onSelect: () => { void plugin.$notify("openSaveProjectBrowser"); } },
        { id: "loadProject", label: "Load project", kind: "action",
          onSelect: () => { void plugin.$notify("openLoadProjectBrowser"); } },
        { id: "midiRouting", label: `MIDI routing: ${routingName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = (ctx.midiRouting + 1) % MIDI_ROUTING_NAMES.length;
              void plugin.$notify("setMidiRouting", next);
          } },
        { id: "layout",       label: "Layout: -",        kind: "action", onSelect: stub("Layout"),        keepOpen: true },
        { id: "zoom",         label: "Zoom: -",          kind: "action", onSelect: stub("Zoom"),          keepOpen: true },
        { id: "audioRouting", label: "Audio routing: -", kind: "action", onSelect: stub("Audio routing"), keepOpen: true },
        { id: "autoSave",     label: "Auto save",        kind: "action", onSelect: stub("Auto save"),     keepOpen: true },
    ];
}

function settingsChildren(): MenuItem[] {
    return [
        { id: "keyboardProfile", label: "Keyboard profile: -", kind: "action", onSelect: stub("Keyboard profile"), keepOpen: true },
        { id: "padProfile",      label: "Pad profile: -",      kind: "action", onSelect: stub("Pad profile"),      keepOpen: true },
        { id: "audioDevice",     label: "Audio device: -",     kind: "action", onSelect: stub("Audio device"),     keepOpen: true },
        { id: "openSettings",    label: "Open settings folder", kind: "action", onSelect: stub("Open settings folder") },
    ];
}

// Per-instance tree. Recomputed every render so cycling labels (Link group,
// MIDI routing, LSDJ mode) reflect current state.
export function buildInstanceMenu(ctx: MenuContext): MenuTree {
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
        { id: "recent", label: "Recent", kind: "submenu", children: recentChildren() },
        { id: "loadRom",        label: "Load ROM...",    kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
        { id: "addInstance",    label: "Add instance",   kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "add" }); } },
        { id: "removeInstance", label: "Remove instance", kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("removeSystem", sys.id); } },
        { id: "linkGroup", label: `Link group: ${linkSuffix}`, kind: "action", keepOpen: true,
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
        { id: "system",   label: "System",   kind: "submenu", children: systemChildren(ctx) },
        { id: "project",  label: "Project",  kind: "submenu", children: projectChildren(ctx) },
        { id: "settings", label: "Settings", kind: "submenu", children: settingsChildren() },
        { id: "about",    label: "About",    kind: "action",  onSelect: stub("About") },
    );

    const title = sys ? `RetroPlug - System #${sys.id}` : "RetroPlug";
    return { title, items };
}

// Start tree (no instances). Project + Settings submenus surface project-
// wide config even before the user adds any instance.
export function buildStartMenu(ctx: MenuContext): MenuTree {
    return {
        title: "RetroPlug",
        items: [
            { id: "load", label: "Load...", kind: "action",
              onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
            { id: "recent",   label: "Recent",   kind: "submenu", children: recentChildren() },
            { id: "project",  label: "Project",  kind: "submenu", children: projectChildren(ctx) },
            { id: "settings", label: "Settings", kind: "submenu", children: settingsChildren() },
            { id: "about",    label: "About",    kind: "action",  onSelect: stub("About") },
        ],
    };
}

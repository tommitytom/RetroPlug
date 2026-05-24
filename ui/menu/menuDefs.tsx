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

export type MenuItemKind = "action" | "submenu" | "separator";

export interface MenuItem {
    id:        string;
    label:     string;
    kind:      MenuItemKind;
    children?: MenuItem[];      // present iff kind === "submenu"
    onSelect?: () => void;      // present iff kind === "action"
    // Right/Left arrow handler for multi-option items (Zoom, MIDI routing,
    // Link group, LSDJ mode). Direction is +1 (Right = next value) or -1
    // (Left = previous). Items without onCycle are no-op on Right/Left.
    onCycle?:  (direction: 1 | -1) => void;
    // True = activating this item should NOT close the menu (e.g. cycling
    // labels like "Link group" / "MIDI routing" / "LSDJ mode").
    keepOpen?: boolean;
}

// Per-build counter so each separator gets a unique id within a single
// menu tree. React keys + Menu's focus-tracking-by-id both rely on item
// ids being unique within a render.
let sepCounter = 0;
function sep(): MenuItem {
    return { id: `sep:${sepCounter++}`, label: "", kind: "separator" };
}

export interface MenuTree {
    title: string;
    items: MenuItem[];
}

export interface RecentEntry {
    path: string;
    kind: "rom" | "project";
}

export interface MenuContext {
    systems:        SystemEntry[];
    focusedSystem?: SystemEntry;
    midiRouting:    number;
    // AudioRouting enum value (0=Stereo, 1=TwoPerInstance, 2=OnePerInstance).
    audioRouting:   number;
    // SystemLayout enum value (0=Auto, 1=Row, 2=Column, 3=Grid).
    layout:         number;
    // Resolved zoom level 1..6 (project setting or user-config default).
    zoom:           number;
    // Most-recent first. Sourced from C++ via plugin.getRecentFiles().
    recentFiles:    RecentEntry[];
    // Called by Menu when the user picks Kit Editor.
    openKitEditor:  () => void;
    // Called by Menu when the user picks About.
    openAbout:      () => void;
    // Bindings profile state (sourced from plugin.getUserConfig()). Empty
    // arrays / strings before the first fetch lands.
    availableProfiles:       string[];
    activeKeyboardBindings:  string;
    activeGamepadBindings:   string;
}

// Mirrors C++ MidiRouting enum (src/project/ProjectConfig.hpp).
const MIDI_ROUTING_NAMES = [
    "Send to all",
    "4 ch / inst",
    "1 ch / inst",
    "ch -> inst",
];

// Mirrors C++ AudioRouting enum (src/project/ProjectConfig.hpp). Plugin
// declares 8 outs; "2 ch / inst" pairs each system into a stereo slot,
// "1 ch / inst" mixes each system's L+R into a single mono channel.
const AUDIO_ROUTING_NAMES = [
    "Stereo",
    "2 ch / inst",
    "1 ch / inst",
];

// Mirrors C++ SystemLayout enum (src/project/ProjectConfig.hpp).
const LAYOUT_NAMES = [
    "Auto",
    "Row",
    "Column",
    "Grid",
];

// Mirrors C++ SameBoyModel enum (src/system/sameboy/SameBoyConfig.hpp).
const MODEL_NAMES = [
    "Auto",
    "DMG-B",
    "MGB",
    "SGB",
    "SGB PAL",
    "SGB2",
    "CGB-0",
    "CGB-A",
    "CGB-B",
    "CGB-C",
    "CGB-D",
    "CGB-E",
    "AGB",
    "GBP",
];

// Mirrors C++ SameBoyHighpass enum (src/system/sameboy/SameBoyConfig.hpp).
const HIGHPASS_NAMES = [
    "Off",
    "Accurate",
    "DC-block",
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

// Wraps `current` within [min, max] inclusive. `+1` past max returns min;
// `-1` below min returns max. Used by every multi-option item below so the
// Enter-cycles-forward (onSelect) and Right/Left-cycle-either-direction
// (onCycle) paths share the same modulo math.
function cycleInt(current: number, min: number, max: number,
                  dir: 1 | -1): number {
    if (dir > 0) return current >= max ? min : current + 1;
    return current <= min ? max : current - 1;
}

// Filename component, handling both `/` and `\\` separators so paths recorded
// on either platform render cleanly. C++ side canonicalises before storing,
// so duplicates are already gone — the basename is purely cosmetic.
function basename(path: string): string {
    const idx = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
    return idx >= 0 ? path.slice(idx + 1) : path;
}

function recentChildren(ctx: MenuContext): MenuItem[] {
    if (ctx.recentFiles.length === 0) {
        return [
            { id: "recentEmpty", label: "(no recent files)", kind: "action",
              onSelect: () => {}, keepOpen: true },
        ];
    }
    return ctx.recentFiles.map((entry, i) => ({
        id:    `recent:${i}`,
        label: basename(entry.path),
        kind:  "action",
        onSelect: () => {
            if (entry.kind === "project") {
                void plugin.$notify("loadProjectFromPath", entry.path);
            } else {
                void plugin.$notify("loadRomFromPath", entry.path);
            }
        },
    }));
}

function systemChildren(ctx: MenuContext): MenuItem[] {
    const sys = ctx.focusedSystem;
    const items: MenuItem[] = [];

    items.push(
        { id: "reset",      label: "Reset",                kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("resetSystem", sys.id); } },
        sep(),
        { id: "saveState",  label: "Save State",           kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("saveState", sys.id); } },
        { id: "saveStateAs",label: "Save State As...",     kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("openSaveStateBrowser", sys.id); } },
        { id: "loadState",  label: "Load State...",        kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("openLoadStateBrowser", sys.id); } },
        sep(),
        { id: "saveSram",   label: "Save SRAM",            kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("saveSram", sys.id); } },
        { id: "saveSramAs", label: "Save SRAM As...",      kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("openSaveSramBrowser", sys.id); } },
        { id: "newSram",    label: "New SRAM",             kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("newSram", sys.id); } },
        sep(),
        { id: "reloadRom",  label: `Reload on ROM change: ${sys?.reloadOnRomChange ? "On" : "Off"}`, kind: "action", keepOpen: true,
          onSelect: () => {
              if (!sys) return;
              void plugin.$notify("setReloadOnRomChange", sys.id, !sys.reloadOnRomChange);
          } },
    );

    // Model is SameBoy-only (no equivalent enum on Mesen NES or GBA).
    if (sys?.kind === "sameboy") {
        const modelIdx   = sys.model ?? 0;
        const modelLabel = `Model: ${MODEL_NAMES[modelIdx] ?? MODEL_NAMES[0]}`;
        items.push({ id: "model", label: modelLabel, kind: "action", keepOpen: true,
            onSelect: () => {
                if (sys.model == null) return;
                const next = cycleInt(sys.model, 0, MODEL_NAMES.length - 1, 1);
                void plugin.$notify("setModel", sys.id, next);
            },
            onCycle: (dir) => {
                if (sys.model == null) return;
                const next = cycleInt(sys.model, 0, MODEL_NAMES.length - 1, dir);
                void plugin.$notify("setModel", sys.id, next);
            } });

        const hpIdx   = sys.highpass ?? 1;
        const hpLabel = `Highpass: ${HIGHPASS_NAMES[hpIdx] ?? HIGHPASS_NAMES[1]}`;
        items.push({ id: "highpass", label: hpLabel, kind: "action", keepOpen: true,
            onSelect: () => {
                if (sys.highpass == null) return;
                const next = cycleInt(sys.highpass, 0, HIGHPASS_NAMES.length - 1, 1);
                void plugin.$notify("setHighpass", sys.id, next);
            },
            onCycle: (dir) => {
                if (sys.highpass == null) return;
                const next = cycleInt(sys.highpass, 0, HIGHPASS_NAMES.length - 1, dir);
                void plugin.$notify("setHighpass", sys.id, next);
            } });
    }

    // Fast boot: present on SameBoy + GBA, hidden on Mesen (no equivalent).
    // SystemEntry.fastBoot is nullopt on Mesen.
    if (sys != null && sys.fastBoot != null) {
        const fastBootOn = sys.fastBoot === true;
        items.push({ id: "fastBoot", label: `Fast boot: ${fastBootOn ? "On" : "Off"}`,
            kind: "action", keepOpen: true,
            onSelect: () => { void plugin.$notify("setFastBoot", sys.id, !fastBootOn); } });
    }

    return items;
}

function projectChildren(ctx: MenuContext): MenuItem[] {
    const routingName      = MIDI_ROUTING_NAMES [ctx.midiRouting]  ?? MIDI_ROUTING_NAMES [0];
    const audioRoutingName = AUDIO_ROUTING_NAMES[ctx.audioRouting] ?? AUDIO_ROUTING_NAMES[0];
    const layoutName       = LAYOUT_NAMES       [ctx.layout]       ?? LAYOUT_NAMES       [0];
    const items: MenuItem[] = [];
    // Save is meaningless without systems to serialize — hide it on the start screen.
    if (ctx.systems.length > 0) {
        items.push({ id: "saveProject", label: "Save project", kind: "action",
                     onSelect: () => { void plugin.$notify("openSaveProjectBrowser"); } });
    }
    items.push(
        { id: "loadProject", label: "Load project", kind: "action",
          onSelect: () => { void plugin.$notify("openLoadProjectBrowser"); } },
        sep(),
        { id: "layout", label: `Layout: ${layoutName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleInt(ctx.layout, 0, LAYOUT_NAMES.length - 1, 1);
              void plugin.$notify("setLayout", next);
          },
          onCycle: (dir) => {
              const next = cycleInt(ctx.layout, 0, LAYOUT_NAMES.length - 1, dir);
              void plugin.$notify("setLayout", next);
          } },
        { id: "zoom",         label: `Zoom: ${ctx.zoom}x`, kind: "action", keepOpen: true,
          onSelect: () => { void plugin.$notify("setZoom", cycleInt(ctx.zoom, 1, 6, 1)); },
          onCycle: (dir) => { void plugin.$notify("setZoom", cycleInt(ctx.zoom, 1, 6, dir)); } },
        sep(),
        { id: "midiRouting", label: `MIDI routing: ${routingName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleInt(ctx.midiRouting, 0, MIDI_ROUTING_NAMES.length - 1, 1);
              void plugin.$notify("setMidiRouting", next);
          },
          onCycle: (dir) => {
              const next = cycleInt(ctx.midiRouting, 0, MIDI_ROUTING_NAMES.length - 1, dir);
              void plugin.$notify("setMidiRouting", next);
          } },
        { id: "audioRouting", label: `Audio routing: ${audioRoutingName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleInt(ctx.audioRouting, 0, AUDIO_ROUTING_NAMES.length - 1, 1);
              void plugin.$notify("setAudioRouting", next);
          },
          onCycle: (dir) => {
              const next = cycleInt(ctx.audioRouting, 0, AUDIO_ROUTING_NAMES.length - 1, dir);
              void plugin.$notify("setAudioRouting", next);
          } },
        sep(),
        { id: "autoSave",     label: "Auto save",        kind: "action", onSelect: stub("Auto save"),     keepOpen: true },
    );
    return items;
}

function settingsChildren(ctx: MenuContext): MenuItem[] {
    const profiles = ctx.availableProfiles;
    const kbActive = ctx.activeKeyboardBindings;
    const padActive = ctx.activeGamepadBindings;
    const cycleProfile = (current: string, dir: 1 | -1): string => {
        if (profiles.length === 0) return current;
        const idx = profiles.indexOf(current);
        const len = profiles.length;
        const next = idx < 0
            ? (dir > 0 ? 0 : len - 1)
            : cycleInt(idx, 0, len - 1, dir);
        return profiles[next];
    };
    return [
        { id: "keyboardProfile",
          label: `Keyboard profile: ${kbActive || "-"}`,
          kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleProfile(kbActive, 1);
              if (next !== kbActive) void plugin.$notify("setActiveKeyboardBindings", next);
          },
          onCycle: (dir) => {
              const next = cycleProfile(kbActive, dir);
              if (next !== kbActive) void plugin.$notify("setActiveKeyboardBindings", next);
          } },
        { id: "padProfile",
          label: `Pad profile: ${padActive || "-"}`,
          kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleProfile(padActive, 1);
              if (next !== padActive) void plugin.$notify("setActiveGamepadBindings", next);
          },
          onCycle: (dir) => {
              const next = cycleProfile(padActive, dir);
              if (next !== padActive) void plugin.$notify("setActiveGamepadBindings", next);
          } },
        { id: "audioDevice",     label: "Audio device: -",     kind: "action", onSelect: stub("Audio device"),     keepOpen: true },
        sep(),
        { id: "openSettings",    label: "Open settings folder", kind: "action",
          onSelect: () => { void plugin.$notify("openSettingsFolder"); } },
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
        { id: "recent", label: "Recent", kind: "submenu", children: recentChildren(ctx) },
        { id: "loadRom",        label: "Load ROM...",    kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
        sep(),
        { id: "addInstance",    label: "Add instance",   kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "add" }); } },
        { id: "duplicate",      label: "Duplicate instance", kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("duplicateSystem", sys.id); } },
        { id: "removeInstance", label: "Remove instance", kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("removeSystem", sys.id); } },
        sep(),
        { id: "linkGroup", label: `Link group: ${linkSuffix}`, kind: "action", keepOpen: true,
          onSelect: () => {
              if (!sys) return;
              const next = cycleInt(sys.linkGroupId ?? 0, 0, LINK_GROUP_MAX - 1, 1);
              void plugin.$notify("setLinkGroupId", sys.id, next);
          },
          onCycle: (dir) => {
              if (!sys) return;
              const next = cycleInt(sys.linkGroupId ?? 0, 0, LINK_GROUP_MAX - 1, dir);
              void plugin.$notify("setLinkGroupId", sys.id, next);
          } },
    ];

    if (hasLsdjRole) {
        items.push({
            id: "lsdjMode", label: `LSDJ mode: ${lsdjMode}`, kind: "action", keepOpen: true,
            onSelect: () => {
                if (!sys || sys.lsdjSyncMode == null) return;
                const next = cycleInt(sys.lsdjSyncMode, 0, LSDJ_MODE_NAMES.length - 1, 1);
                void plugin.$notify("setLsdjSyncConfig", sys.id, next, sys.lsdjTempoDivisor ?? 1);
            },
            onCycle: (dir) => {
                if (!sys || sys.lsdjSyncMode == null) return;
                const next = cycleInt(sys.lsdjSyncMode, 0, LSDJ_MODE_NAMES.length - 1, dir);
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
        sep(),
        { id: "system",   label: "System",   kind: "submenu", children: systemChildren(ctx) },
        { id: "project",  label: "Project",  kind: "submenu", children: projectChildren(ctx) },
        { id: "settings", label: "Settings", kind: "submenu", children: settingsChildren(ctx) },
        sep(),
        { id: "about",    label: "About",    kind: "action",  onSelect: () => ctx.openAbout() },
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
            { id: "recent",   label: "Recent",   kind: "submenu", children: recentChildren(ctx) },
            sep(),
            { id: "project",  label: "Project",  kind: "submenu", children: projectChildren(ctx) },
            { id: "settings", label: "Settings", kind: "submenu", children: settingsChildren(ctx) },
            sep(),
            { id: "about",    label: "About",    kind: "action",  onSelect: () => ctx.openAbout() },
        ],
    };
}

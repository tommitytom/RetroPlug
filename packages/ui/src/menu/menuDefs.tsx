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
import {
    GB_BUTTONS, formatBindingList, type BindingsEditor,
} from "../useBindingsEditor";

export type MenuItemKind =
    | "action"     // simple onSelect
    | "submenu"    // expandable parent with children
    | "separator"  // visual divider, not focusable
    | "capture"    // press Enter to bind next key / gamepad button
    | "prompt";    // press Enter to open inline text-input overlay

export interface CaptureSpec {
    // Which event channel to listen on while capturing.
    kind:      "keyboard" | "gamepad";
    // String to show after the item label (e.g. current binding list).
    value:     string;
    // Called when a key / button is captured.
    onCapture: (source: string) => void;
    // Called when Delete / Backspace is pressed on the focused row.
    onClear:   () => void;
}

export interface PromptSpec {
    // Heading shown above the input / confirmation message.
    title:        string;
    // Initial text-input value (ignored for confirm prompts).
    initial?:     string;
    // Hint shown below the input. Defaults to "Enter to confirm | Esc to cancel".
    hint?:        string;
    // Synchronous validation before submit. Return error string or null.
    validate?:    (value: string) => string | null;
    // Called on Enter. Return error string (keeps prompt open) or null (closes).
    onConfirm:    (value: string) => Promise<string | null> | string | null;
    // True = yes/no prompt (no text input). Enter confirms, Esc cancels.
    confirm?:     boolean;
}

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
    // labels like "Link group" / "MIDI routing" / "LSDJ mode"). Implicit
    // for capture / prompt / submenu / separator items.
    keepOpen?: boolean;
    capture?:  CaptureSpec;      // present iff kind === "capture"
    prompt?:   PromptSpec;       // present iff kind === "prompt"
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
    // Display alias; empty => derive a label from the path basename.
    name: string;
    // True when the project's `.rplg` no longer exists on disk.
    missing: boolean;
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
    // Global SRAM auto-save preference (UserConfig). Toggled in Settings.
    autoSaveSram:   boolean;
    // Most-recent first. Sourced from C++ via plugin.getRecentFiles().
    recentFiles:    RecentEntry[];
    // Called by Menu when the user picks Kit Editor.
    openKitEditor:  () => void;
    // Called by Menu when the user picks About.
    openAbout:      () => void;
    // Bindings editors (one per channel). Own all of their own state +
    // RPC calls — see ui/useBindingsEditor.ts. The Settings submenu
    // builds two inline submenus from these (one for keyboard, one for
    // gamepad). `availableProfiles` / `activeKeyboardBindings` /
    // `activeGamepadBindings` are present on each editor object.
    keyboardEditor: BindingsEditor;
    gamepadEditor:  BindingsEditor;
}

// Mirrors C++ MidiRouting enum (src/project/ProjectConfig.hpp).
const MIDI_ROUTING_NAMES = [
    "Send to All",
    "4 Ch / Inst",
    "1 Ch / Inst",
    "Ch -> Inst",
];

// Mirrors C++ AudioRouting enum (src/project/ProjectConfig.hpp). Plugin
// declares 8 outs; "2 ch / inst" pairs each system into a stereo slot,
// "1 ch / inst" mixes each system's L+R into a single mono channel.
const AUDIO_ROUTING_NAMES = [
    "Stereo",
    "2 Ch / Inst",
    "1 Ch / Inst",
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
    "DC-Block",
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

// Friendly name for a recent project: its alias, else the file basename.
function recentName(entry: RecentEntry): string {
    return entry.name.trim() || basename(entry.path);
}

// Row label for a recent project. A trailing "  (missing)" marker (two spaces,
// mirroring the "Save  *" dirty marker) flags a project whose file is gone.
function recentLabel(entry: RecentEntry): string {
    const name = recentName(entry);
    return entry.missing ? `${name}  (missing)` : name;
}

// Each recent project is a submenu: Load / Rename, plus Locate (when missing)
// and Remove. Mutations are fire-and-forget — the C++ side emits
// "recent-files-changed", PluginUI refetches, and the tree rebuilds.
function recentEntryChildren(entry: RecentEntry, idp: string): MenuItem[] {
    const children: MenuItem[] = [];

    children.push({
        id:       `${idp}:load`,
        label:    entry.missing ? "Locate to Load..." : "Load",
        kind:     "action",
        keepOpen: entry.missing,   // browsing must not close the menu
        onSelect: () => {
            if (entry.missing) { void plugin.$notify("openRecentRelinkBrowser", entry.path); return; }
            void plugin.$notify("loadProjectFromPath", entry.path);
        },
    });

    children.push({
        id:     `${idp}:rename`, label: "Rename...", kind: "prompt", keepOpen: true,
        prompt: {
            title:   `Rename "${recentName(entry)}" to:`,
            initial: recentName(entry),
            onConfirm: async (v) => {
                const name = v.trim();
                if (!name) return "Name cannot be empty.";
                try {
                    return (await plugin.renameRecentFile(entry.path, name)) ? null : "Rename failed.";
                } catch { return "Rename failed."; }
            },
        },
    });

    if (entry.missing) {
        children.push({
            id: `${idp}:locate`, label: "Locate on Disk...", kind: "action", keepOpen: true,
            onSelect: () => { void plugin.$notify("openRecentRelinkBrowser", entry.path); },
        });
    }

    children.push({
        id:     `${idp}:remove`, label: "Remove from List", kind: "prompt", keepOpen: true,
        prompt: {
            title:   `Remove "${recentName(entry)}" from recent files?`,
            hint:    "Enter to remove  |  Esc to cancel",
            confirm: true,
            onConfirm: async () => {
                try { await plugin.removeRecentFile(entry.path); return null; }
                catch { return "Could not remove entry."; }
            },
        },
    });

    return children;
}

function recentChildren(ctx: MenuContext): MenuItem[] {
    if (ctx.recentFiles.length === 0) {
        return [
            { id: "recentEmpty", label: "(No Recent Files)", kind: "action",
              onSelect: () => {}, keepOpen: true },
        ];
    }
    return ctx.recentFiles.map((entry, i) => {
        const idp = `recent:${i}`;
        return {
            id:       idp,
            label:    recentLabel(entry),
            kind:     "submenu",
            children: recentEntryChildren(entry, idp),
        };
    });
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
        { id: "loadSram",   label: "Load SRAM...",         kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("openLoadSramBrowser", sys.id); } },
        { id: "newSram",    label: "New SRAM",             kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("newSram", sys.id); } },
        sep(),
        { id: "reloadRom",  label: `Reload on ROM Change: ${sys?.reloadOnRomChange ? "On" : "Off"}`, kind: "action", keepOpen: true,
          onSelect: () => {
              if (!sys) return;
              void plugin.$notify("setReloadOnRomChange", sys.id, !sys.reloadOnRomChange);
          },
          // Boolean toggle: Left/Right both just flip it, matching the
          // other cyclable rows.
          onCycle: () => {
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
        items.push({ id: "fastBoot", label: `Fast Boot: ${fastBootOn ? "On" : "Off"}`,
            kind: "action", keepOpen: true,
            onSelect: () => { void plugin.$notify("setFastBoot", sys.id, !fastBootOn); },
            // Boolean toggle: Left/Right both just flip it, matching the
            // other cyclable rows (Model, Highpass, …).
            onCycle: () => { void plugin.$notify("setFastBoot", sys.id, !fastBootOn); } });
    }

    return items;
}

function projectChildren(ctx: MenuContext): MenuItem[] {
    const routingName      = MIDI_ROUTING_NAMES [ctx.midiRouting]  ?? MIDI_ROUTING_NAMES [0];
    const audioRoutingName = AUDIO_ROUTING_NAMES[ctx.audioRouting] ?? AUDIO_ROUTING_NAMES[0];
    const layoutName       = LAYOUT_NAMES       [ctx.layout]       ?? LAYOUT_NAMES       [0];
    const items: MenuItem[] = [];
    // Save / Export are meaningless without systems to serialize — hide them on
    // the start screen. "Save Project" writes a path-only JSON `.rplg`; "Export
    // Zip" bundles every binary (ROM/SRAM/savestate/kits) into a self-contained
    // `.zip`.
    if (ctx.systems.length > 0) {
        items.push(
            { id: "saveProject", label: "Save Project", kind: "action",
              onSelect: () => { void plugin.$notify("openSaveProjectBrowser"); } },
            { id: "exportZip", label: "Export Zip", kind: "action",
              onSelect: () => { void plugin.$notify("openExportZipBrowser"); } });
    }
    items.push(
        { id: "loadProject", label: "Load Project", kind: "action",
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
        { id: "midiRouting", label: `MIDI Routing: ${routingName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleInt(ctx.midiRouting, 0, MIDI_ROUTING_NAMES.length - 1, 1);
              void plugin.$notify("setMidiRouting", next);
          },
          onCycle: (dir) => {
              const next = cycleInt(ctx.midiRouting, 0, MIDI_ROUTING_NAMES.length - 1, dir);
              void plugin.$notify("setMidiRouting", next);
          } },
        { id: "audioRouting", label: `Audio Routing: ${audioRoutingName}`, kind: "action", keepOpen: true,
          onSelect: () => {
              const next = cycleInt(ctx.audioRouting, 0, AUDIO_ROUTING_NAMES.length - 1, 1);
              void plugin.$notify("setAudioRouting", next);
          },
          onCycle: (dir) => {
              const next = cycleInt(ctx.audioRouting, 0, AUDIO_ROUTING_NAMES.length - 1, dir);
              void plugin.$notify("setAudioRouting", next);
          } },
    );
    return items;
}

// Build a "Keyboard bindings" / "Gamepad bindings" submenu from one
// BindingsEditor instance. The submenu contains:
//   1. Profile cycler (Left/Right cycles browsed profile, Enter makes
//      it the live-active profile).
//   2. One capture row per GB button (8 total). Enter opens capture
//      mode; Backspace/Delete clears the binding.
//   3. Save / Revert / Save As / Rename / Duplicate / Delete actions.
function bindingsSubmenu(editor: BindingsEditor): MenuItem {
    const k = editor.kind;
    const idp = `bindings:${k}`;
    const title = k === "keyboard" ? "Keyboard Bindings" : "Gamepad Bindings";
    const isActive  = editor.profileName === editor.activeProfile && !!editor.profileName;
    const activeTag = isActive ? " [Active]" : "";
    const dirtyTag  = editor.dirty ? "  *" : "";
    const profileLabel = `Profile: ${editor.profileName || "-"}${activeTag}${dirtyTag}`;

    const children: MenuItem[] = [
        { id: `${idp}:profile`, label: profileLabel,
          kind: "action", keepOpen: true,
          onSelect: () => { void editor.makeActive(); },
          onCycle: editor.cycleProfile },
        sep(),
    ];

    for (const btn of GB_BUTTONS) {
        const display = formatBindingList(editor.channel[btn]);
        children.push({
            id: `${idp}:btn:${btn}`,
            label: `${btn}: ${display}`,
            kind: "capture", keepOpen: true,
            capture: {
                kind: k,
                value: display,
                onCapture: (source) => editor.applyCapture(btn, source),
                onClear:   ()        => editor.clearBinding(btn),
            },
        });
    }

    children.push(
        sep(),
        { id: `${idp}:save`, label: editor.dirty ? "Save  *" : "Save",
          kind: "action", keepOpen: true,
          onSelect: () => { void editor.save(); } },
        { id: `${idp}:revert`, label: "Revert",
          kind: "action", keepOpen: true,
          onSelect: () => { editor.revert(); } },
        { id: `${idp}:saveAs`, label: "Save As...",
          kind: "prompt", keepOpen: true,
          prompt: {
              title: `Save ${k} bindings as:`,
              initial: "",
              onConfirm: (v) => editor.saveAs(v.trim()),
          } },
        { id: `${idp}:rename`, label: "Rename...",
          kind: "prompt", keepOpen: true,
          prompt: {
              title: `Rename "${editor.profileName}" to:`,
              initial: editor.profileName,
              onConfirm: (v) => editor.rename(v.trim()),
          } },
        { id: `${idp}:duplicate`, label: "Duplicate",
          kind: "action", keepOpen: true,
          onSelect: () => { void editor.duplicate(); } },
        { id: `${idp}:delete`,
          label: editor.canDelete
              ? `Delete "${editor.profileName}"...`
              : `Delete (Switch Active First)`,
          kind: editor.canDelete ? "prompt" : "action", keepOpen: true,
          // Hint pressing this row when disabled does nothing useful.
          onSelect: editor.canDelete ? undefined : () => {},
          prompt: editor.canDelete ? {
              title: `Delete profile "${editor.profileName}"?`,
              hint:  `Enter to delete  |  Esc to cancel`,
              confirm: true,
              onConfirm: () => editor.deleteProfile(),
          } : undefined,
        },
    );

    return { id: idp, label: title, kind: "submenu", children };
}

function settingsChildren(ctx: MenuContext): MenuItem[] {
    return [
        bindingsSubmenu(ctx.keyboardEditor),
        bindingsSubmenu(ctx.gamepadEditor),
        sep(),
        // Global, sticky SRAM auto-save (UserConfig). Writes each system's
        // sibling <rom>.sav while playing. Boolean toggle: Left/Right flip it.
        { id: "autoSave", label: `Auto Save: ${ctx.autoSaveSram ? "On" : "Off"}`,
          kind: "action", keepOpen: true,
          onSelect: () => { void plugin.setAutoSaveSram(!ctx.autoSaveSram); },
          onCycle:  () => { void plugin.setAutoSaveSram(!ctx.autoSaveSram); } },
        sep(),
        { id: "openSettings", label: "Open Settings Folder", kind: "action",
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
    const lsdjMode    = hasLsdjRole
        ? LSDJ_MODE_NAMES[sys!.lsdjSyncMode ?? 0] ?? LSDJ_MODE_NAMES[0]
        : "";

    const items: MenuItem[] = [
        { id: "recent", label: "Recent", kind: "submenu", children: recentChildren(ctx) },
        { id: "loadRom",        label: "Load ROM...",    kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "replace" }); } },
        sep(),
        { id: "addInstance",    label: "Add Instance",   kind: "action",
          onSelect: () => { void plugin.$notify("openRomBrowser", { mode: "add" }); } },
        { id: "duplicate",      label: "Duplicate Instance", kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("duplicateSystem", sys.id); } },
        { id: "removeInstance", label: "Remove Instance", kind: "action",
          onSelect: () => { if (sys) void plugin.$notify("removeSystem", sys.id); } },
        sep(),
        { id: "linkGroup", label: `Link Group: ${linkSuffix}`, kind: "action", keepOpen: true,
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
            id: "lsdjMode", label: `LSDJ Mode: ${lsdjMode}`, kind: "action", keepOpen: true,
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

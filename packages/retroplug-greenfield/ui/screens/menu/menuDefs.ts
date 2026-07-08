// The menu trees, ported from legacy menuDefs.tsx. Every leaf drives a greenfield store method; current
// values are baked into labels and recomputed each render (there's no separate "checked" state — the
// label IS the display). Actions still gated on a deferred backend surface (the bindings editor) are
// omitted here, noted where their submenu would list them.

import type { AppStores } from "../../../src/appStores";
import type { SystemView } from "../../../src/systemsStore";
import type { ProjectSettings } from "../../../src/projectConfig";
import type { UserConfig } from "../../../src/userConfig";
import { SRAM_AUTO_SAVES } from "../../../src/userConfig";
import { defaultBindingMap, type BindingMap } from "../../../src/bindingMap";
import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
import type { RecentView } from "../../../src/recentStore";
import { resolveSavPath, siblingPath } from "../../../src/savPaths";
import { stem } from "../../../src/pathUtil";
import type { SelectionOutcome } from "../../../src/fileSelection";
import { openPath } from "../../lvgl/openPath";
import type { FileBrowserOpts } from "../../../src/backend";
import type { MenuItem, MenuTree } from "./menuTree";

/** Everything a builder reads (current values) + mutates through (the stores). Rebuilt each render. */
export interface MenuContext {
  stores: AppStores;
  system?: SystemView; // the anchored system (instance menu)
  settings: ProjectSettings;
  userConfig: UserConfig;
  bindings: BindingMap; // resolved active bindings — the keyboard editor reads/displays this
  systems: SystemView[];
  recent: RecentView[];
  version: string;
  // Destructive project ops, guarded (unsaved-changes prompt) + outcome-aware (incompatible / relink /
  // error) by useProjectModals — the menu drives these instead of project.newProject / project.load.
  newProject: () => void;
  loadProject: (path: string) => void;
}

// --- name tables (mirror the native enums, ported from legacy menuDefs.tsx) ---------------------------
const MIDI_ROUTING_NAMES = ["Send to All", "4 Ch / Inst", "1 Ch / Inst", "Ch -> Inst"];
const AUDIO_ROUTING_NAMES = ["Stereo", "2 Ch / Inst", "1 Ch / Inst"];
const LAYOUT_NAMES = ["Auto", "Row", "Column", "Grid"];
const MODEL_NAMES = ["Auto", "DMG-B", "MGB", "SGB", "SGB PAL", "SGB2", "CGB-0", "CGB-A", "CGB-B", "CGB-C", "CGB-D", "CGB-E", "AGB", "GBP"];
const HIGHPASS_NAMES = ["Off", "Accurate", "DC-Block"];
const SRAM_AUTO_SAVE_LABELS: Record<string, string> = { Off: "Off", OnProjectSave: "On Save", Continuous: "Continuous" };
// Link Group cycles 0..4 (0 = Off), mirroring the legacy LINK_GROUP_MAX.
const LINK_GROUP_NAMES = ["Off", "1", "2", "3", "4"];
const OFF_ON = ["Off", "On"]; // boolean toggles rendered as 2-value cyclers (Left/Right + Enter step)

// Glob filters for the file dialogs (realBackend space-joins them for DPF).
const PROJECT_PATTERNS = ["*.rplg"]; // thin project (raw JSON) — the Save target
const ZIP_PATTERNS = ["*.rplg.zip"]; // exported project (PKZIP) — always `.rplg.zip`
const LOAD_PATTERNS = ["*.rplg", "*.rplg.zip"]; // load/locate accept either on-disk shape
const STATE_PATTERNS = ["*.ss?"]; // slot-numbered savestates (.ss0..ss9), matching legacy
const SRAM_PATTERNS = ["*.sav"];

/** Wrap `current` within [min, max]: +1 past max → min, -1 below min → max. */
function cycleInt(current: number, min: number, max: number, dir: 1 | -1): number {
  if (dir > 0) return current >= max ? min : current + 1;
  return current <= min ? max : current - 1;
}

// --- item helpers -------------------------------------------------------------------------------------
function action(id: string, label: string, onSelect: () => void): MenuItem {
  return { id, label, kind: "action", onSelect };
}
function submenu(id: string, label: string, children: MenuItem[]): MenuItem {
  return { id, label, kind: "submenu", children };
}
function sep(id: string): MenuItem {
  return { id, label: "", kind: "separator" };
}

/** A value-cycler row: label shows `prefix: names[current]`, Enter/Right step forward, Left back. */
function cycler(id: string, prefix: string, names: string[], current: number, apply: (next: number) => void): MenuItem {
  const step = (dir: 1 | -1) => apply(cycleInt(current, 0, names.length - 1, dir));
  return { id, label: `${prefix}: ${names[current] ?? "?"}`, kind: "cycler", keepOpen: true, onSelect: () => step(1), onCycle: step };
}

/** Fire a FileSelection browse and apply its outcome once every dialog settles: the store mutates itself
 *  on load / add, and a `deferred` sibling `<rom>.rplg` is handed to the Project domain. Selection is
 *  fire-and-forget from a menu leaf — the store's change notification re-renders when it lands. */
function runSelection(ctx: MenuContext, p: Promise<SelectionOutcome>): void {
  void p.then((outcome) => {
    if (outcome.kind === "deferred") ctx.stores.project.load(outcome.project);
    // A fresh ROM load has no on-disk project yet — adopt its `<rom>.rplg` sibling so it enters recents
    // (an `added` instance deliberately doesn't: it appends to the current project, not a new one).
    else if (outcome.kind === "loaded") ctx.stores.project.adoptRomProject(outcome.romPath);
  });
}

/** Open the OS browser with `opts` and apply the picked path (a cancel is ignored). For the project file
 *  ops, whose store methods already take a resolved path — the dialog is the only missing piece. */
function browseThen(ctx: MenuContext, opts: FileBrowserOpts, apply: (path: string) => void): void {
  void ctx.stores.backend.openFileBrowser(opts).then((path) => {
    if (path) apply(path);
  });
}

/** The SameBoy core-role config for a system (model / highpass / linkGroupId / fastBoot), with defaults. */
function sameboyConfig(sys: SystemView): { model: number; highpass: number; linkGroupId: number; fastBoot: boolean } {
  const c = (sys.roles.find((r) => r.kind === "sameboy")?.config ?? {}) as Record<string, unknown>;
  return {
    model: typeof c.model === "number" ? c.model : 9,
    highpass: typeof c.highpass === "number" ? c.highpass : 1,
    linkGroupId: typeof c.linkGroupId === "number" ? c.linkGroupId : 0,
    fastBoot: c.fastBoot !== false,
  };
}

// --- child builders -----------------------------------------------------------------------------------
function systemChildren(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const systems = ctx.stores.project.systems;
  const items: MenuItem[] = [
    cycler("sys-reload", "Reload on ROM Change", OFF_ON, sys.settings.reloadOnRomChange ? 1 : 0, (n) =>
      systems.setReloadOnRomChange(sys.id, n === 1),
    ),
  ];
  // SameBoy-only core knobs.
  if (sys.core === "sameboy") {
    const cfg = sameboyConfig(sys);
    items.push(
      cycler("sys-model", "Model", MODEL_NAMES, cfg.model, (n) => systems.setRoleConfig(sys.id, "sameboy", { model: n })),
      cycler("sys-highpass", "Highpass", HIGHPASS_NAMES, cfg.highpass, (n) => systems.setRoleConfig(sys.id, "sameboy", { highpass: n })),
      cycler("sys-fastboot", "Fast Boot", OFF_ON, cfg.fastBoot ? 1 : 0, (n) => systems.setRoleConfig(sys.id, "sameboy", { fastBoot: n === 1 })),
    );
  }
  // Save/Load State + SRAM. The quick "Save State"/"Save SRAM" write to the ROM's sibling path with no
  // dialog (a real ROM only — the embedded synth has no on-disk target); the "As…" variants browse. The
  // store reads/writes the resolved path (the registry read is safe while playing; load reconstructs the
  // core in place). Reset reboots carrying the battery; New SRAM reboots with a blank battery — both
  // pathless, reconstructing in place (no live GB_reset/clearSram).
  const romStem = stem(sys.romPath);
  items.push(sep("sys-sep-state"));
  if (sys.romPath)
    items.push(action("sys-quicksavestate", "Save State", () => systems.saveState(sys.id, siblingPath(sys.romPath, sys.savSuffix, ".ss0"))));
  items.push(
    action("sys-savestate", "Save State As...", () =>
      browseThen(ctx, { title: "Save State", patterns: STATE_PATTERNS, saving: true, defaultName: `${romStem || "savestate"}.ss0` }, (p) => systems.saveState(sys.id, p)),
    ),
    action("sys-loadstate", "Load State...", () =>
      browseThen(ctx, { title: "Load State", patterns: STATE_PATTERNS }, (p) => void systems.loadState(sys.id, p)),
    ),
  );
  if (sys.romPath)
    items.push(action("sys-quicksavesram", "Save SRAM", () => systems.saveSram(sys.id, resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath))));
  items.push(
    action("sys-savesram", "Save SRAM As...", () =>
      browseThen(ctx, { title: "Save SRAM", patterns: SRAM_PATTERNS, saving: true, defaultName: `${romStem || "sram"}.sav` }, (p) => systems.saveSram(sys.id, p)),
    ),
    action("sys-loadsram", "Load SRAM...", () =>
      browseThen(ctx, { title: "Load SRAM", patterns: SRAM_PATTERNS }, (p) => void systems.loadSram(sys.id, p)),
    ),
    action("sys-newsram", "New SRAM", () => void systems.newSram(sys.id)),
    sep("sys-sep-reset"),
    action("sys-reset", "Reset", () => void systems.reset(sys.id)),
  );
  return items;
}

function projectChildren(ctx: MenuContext): MenuItem[] {
  const project = ctx.stores.project;
  const items: MenuItem[] = [];
  if (ctx.systems.length > 0) {
    items.push(action("proj-new", "New Project", () => ctx.newProject()));
    // Save writes to the known path when there is one (else Save As covers it). Save As / Export browse
    // for a target; each store method already takes a resolved path.
    if (project.currentPath()) items.push(action("proj-save", "Save Project", () => project.save(project.currentPath())));
    items.push(action("proj-saveas", "Save Project As...", () =>
      browseThen(ctx, { title: "Save Project", patterns: PROJECT_PATTERNS, saving: true, defaultName: "project.rplg" }, (p) => project.save(p)),
    ));
    items.push(action("proj-export", "Export Zip...", () =>
      browseThen(ctx, { title: "Export Zip", patterns: ZIP_PATTERNS, saving: true, defaultName: "project.rplg.zip" }, (p) => project.export(p)),
    ));
  }
  // Load is always available (even from an empty start menu). Guarded + outcome-aware via ctx.loadProject.
  items.push(action("proj-load", "Load Project...", () =>
    browseThen(ctx, { title: "Load Project", patterns: LOAD_PATTERNS }, (p) => ctx.loadProject(p)),
  ));
  items.push(sep("proj-sep0"));
  items.push(
    cycler("proj-layout", "Layout", LAYOUT_NAMES, ctx.settings.layout, (n) => project.setLayout(n)),
    { id: "proj-zoom", label: `Zoom: ${ctx.settings.zoom === 0 ? "Default" : `${ctx.settings.zoom}x`}`, kind: "cycler", keepOpen: true, onSelect: () => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, 1)), onCycle: (dir) => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, dir)) },
    sep("proj-sep1"),
    cycler("proj-midi", "MIDI Routing", MIDI_ROUTING_NAMES, ctx.settings.midiRouting, (n) => project.setMidiRouting(n)),
    cycler("proj-audio", "Audio Routing", AUDIO_ROUTING_NAMES, ctx.settings.audioRouting, (n) => project.setAudioRouting(n)),
  );
  return items;
}

// GB button display/edit order (mirrors the legacy bindings editor).
const GB_BUTTONS = ["Right", "Left", "Up", "Down", "A", "B", "Select", "Start"];

/** The keyboard bindings editor: a profile switcher, one capture row per GB button (Enter arms, the next
 *  key rebinds, Backspace clears), a keyboard reset, and named-profile management (New / Rename / Delete).
 *  Write-through / edit-active — every edit + profile switch re-resolves and the live joypad follows via
 *  useGameInput. Gamepad bindings stay out (no gamepad I/O yet). */
function bindingsChildren(ctx: MenuContext): MenuItem[] {
  const bindings = ctx.stores.bindings;
  const userConfig = ctx.stores.userConfig;
  const activeName = ctx.userConfig.activeKeyboardBindings;
  const gamepadName = ctx.userConfig.activeGamepadBindings;
  const profiles = bindings.availableProfiles();
  const kb = ctx.bindings.keyboard; // resolved active keyboard map — recomputed each render
  const write = (edit: (m: BindingMap) => BindingMap) => {
    const map = bindings.loadProfile(activeName) ?? defaultBindingMap();
    bindings.saveProfile(activeName, edit(map));
  };

  // Create a named copy of the current bindings and make it active. Errors surface in the prompt's red line.
  const newProfile = (raw: string): string | null => {
    const n = raw.trim();
    if (!isValidProfileName(n)) return "Invalid name (A-Z, 0-9, _, -).";
    if (profiles.includes(n)) return "Profile already exists.";
    const cur = bindings.loadProfile(activeName) ?? defaultBindingMap();
    if (!bindings.saveProfile(n, { ...cur, name: n })) return "Save failed.";
    userConfig.setActiveKeyboardBindings(n);
    return null;
  };
  const renameActive = (raw: string): string | null => {
    const n = raw.trim();
    if (n === activeName) return null; // no-op rename
    if (!isValidProfileName(n)) return "Invalid name (A-Z, 0-9, _, -).";
    if (profiles.includes(n)) return "Profile already exists.";
    return bindings.renameProfile(activeName, n) ? null : "Rename failed."; // repoints the active ref
  };
  // Deletable = neither active channel's profile, so nothing shown is un-deletable.
  const deletable = profiles.filter((p) => p !== activeName && p !== gamepadName);
  const deleteChildren: MenuItem[] = deletable.length
    ? deletable.map((p) => ({
        id: `bind-del-${p}`,
        label: p,
        kind: "prompt" as const,
        keepOpen: true,
        prompt: {
          title: `Delete profile "${p}"?`,
          hint: "Enter to delete  |  Esc to cancel",
          confirm: true,
          onConfirm: () => {
            bindings.deleteProfile(p);
            return null;
          },
        },
      }))
    : [action("bind-del-none", "(no other profiles)", () => {})];

  const captureRows: MenuItem[] = GB_BUTTONS.map((btn) => ({
    id: `bind-${btn}`,
    label: `${btn}: ${kb[btn]?.length ? kb[btn].join(", ") : "-"}`,
    kind: "capture" as const,
    keepOpen: true,
    capture: {
      onCapture: (name: string) => write((m) => ({ ...m, keyboard: { ...m.keyboard, [btn]: [name] } })),
      onClear: () => write((m) => ({ ...m, keyboard: { ...m.keyboard, [btn]: [] } })),
    },
  }));

  return [
    cycler("bind-profile", "Profile", profiles, Math.max(0, profiles.indexOf(activeName)), (n) => userConfig.setActiveKeyboardBindings(profiles[n])),
    sep("bind-sep-top"),
    ...captureRows,
    sep("bind-sep-reset"),
    // Keyboard channel only — preserve the profile's gamepad map.
    action("bind-reset", "Reset Keyboard to Defaults", () => write((m) => ({ ...m, keyboard: defaultBindingMap().keyboard }))),
    sep("bind-sep-mgmt"),
    { id: "bind-new", label: "New Profile...", kind: "prompt", keepOpen: true, prompt: { title: "New profile name:", filter: isValidProfileChar, onConfirm: newProfile } },
    { id: "bind-rename", label: "Rename...", kind: "prompt", keepOpen: true, prompt: { title: `Rename "${activeName}" to:`, initial: activeName, filter: isValidProfileChar, onConfirm: renameActive } },
    submenu("bind-delete", "Delete Profile", deleteChildren),
  ];
}

function settingsChildren(ctx: MenuContext): MenuItem[] {
  const userConfig = ctx.stores.userConfig;
  const sramIdx = Math.max(0, SRAM_AUTO_SAVES.indexOf(ctx.userConfig.sramAutoSave));
  return [
    cycler("set-sram", "SRAM Auto-Save", SRAM_AUTO_SAVES.map((m) => SRAM_AUTO_SAVE_LABELS[m] ?? m), sramIdx, (n) => userConfig.setSramAutoSave(SRAM_AUTO_SAVES[n])),
    { id: "set-defzoom", label: `Default Zoom: ${ctx.userConfig.defaultZoom}x`, kind: "cycler", keepOpen: true, onSelect: () => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, 1)), onCycle: (dir) => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, dir)) },
    submenu("set-keybindings", "Keyboard Bindings", bindingsChildren(ctx)),
    action("set-open-folder", "Open Settings Folder", () => openPath(ctx.stores.backend.configDir())),
    // Deferred: Gamepad Bindings (needs live gamepad I/O).
  ];
}

function recentChildren(ctx: MenuContext): MenuItem[] {
  if (ctx.recent.length === 0) return [action("recent-none", "(No Recent Files)", () => {})];
  return ctx.recent.map((entry, i) =>
    submenu(`recent-${i}`, entry.label, [
      action(`recent-${i}-load`, entry.missing ? "Load (missing)" : "Load", () => ctx.loadProject(entry.path)),
      action(`recent-${i}-locate`, "Locate on Disk", () =>
        browseThen(ctx, { title: "Locate Project", patterns: LOAD_PATTERNS }, (p) => ctx.stores.recent.relink(entry.path, p)),
      ),
      {
        id: `recent-${i}-rename`,
        label: "Rename...",
        kind: "prompt",
        keepOpen: true,
        prompt: {
          title: `Rename "${entry.label}" to:`,
          initial: entry.label,
          onConfirm: (v: string) => {
            const name = v.trim();
            if (!name) return "Name cannot be empty.";
            return ctx.stores.project.renameProject(entry.path, name) ? null : "Rename failed.";
          },
        },
      },
      action(`recent-${i}-remove`, "Remove from List", () => ctx.stores.recent.remove(entry.path)),
    ]),
  );
}

// --- top-level builders -------------------------------------------------------------------------------

/** The instance-menu title: "RetroPlug v<version> - <project> - <rom>". ROM name = the file stem, or
 *  "mGB" for the embedded synth (romPath === ""). Empty segments are dropped, and the ROM is omitted when
 *  it equals the project name (the common case where the name was seeded from the ROM) so it isn't shown
 *  twice. */
function instanceTitle(ctx: MenuContext, sys: SystemView): string {
  const base = ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug";
  const project = ctx.stores.project.name();
  const rom = sys.embedded ? "mGB" : stem(sys.romPath);
  const segs = [base];
  if (project) segs.push(project);
  if (rom && rom !== project) segs.push(rom);
  return segs.join(" - ");
}

export function buildInstanceMenu(ctx: MenuContext): MenuTree {
  const sys = ctx.system!;
  const systems = ctx.stores.project.systems;
  return {
    title: instanceTitle(ctx, sys),
    items: [
      action("inst-load", "Load ROM", () => runSelection(ctx, ctx.stores.fileSelection.browse("load"))),
      submenu("inst-recent", "Recent", recentChildren(ctx)),
      sep("inst-sep-top"),
      action("inst-add", "Add Instance", () => runSelection(ctx, ctx.stores.fileSelection.browse("add"))),
      action("inst-dup", "Duplicate Instance", () => systems.duplicateSystem(sys.id)),
      action("inst-remove", "Remove Instance", () => systems.removeSystem(sys.id)),
      sep("inst-sep0"),
      cycler("inst-link", "Link Group", LINK_GROUP_NAMES, sameboyConfig(sys).linkGroupId, (n) =>
        systems.setRoleConfig(sys.id, "sameboy", { linkGroupId: n }),
      ),
      sep("inst-sep1"),
      submenu("inst-system", "System", systemChildren(ctx, sys)),
      submenu("inst-project", "Project", projectChildren(ctx)),
      submenu("inst-settings", "Settings", settingsChildren(ctx)),
      // Deferred: About panel, LSDj Mode (feature role, no live apply).
    ],
  };
}

export function buildStartMenu(ctx: MenuContext): MenuTree {
  return {
    title: ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug",
    items: [
      action("start-load", "Load...", () => runSelection(ctx, ctx.stores.fileSelection.browse("load"))),
      action("start-mgb", "Load mGB (GB MIDI Synth)", () => ctx.stores.project.systems.loadMgb()),
      submenu("start-recent", "Recent", recentChildren(ctx)),
      sep("start-sep0"),
      submenu("start-project", "Project", projectChildren(ctx)),
      submenu("start-settings", "Settings", settingsChildren(ctx)),
      // Deferred: About panel.
    ],
  };
}

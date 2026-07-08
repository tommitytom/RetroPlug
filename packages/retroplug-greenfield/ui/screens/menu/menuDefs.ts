// The browser-free menu trees, ported from legacy menuDefs.tsx. Every leaf drives a greenfield store
// method; current values are baked into labels and recomputed each render (there's no separate "checked"
// state — the label IS the display). Actions gated on the deferred backend (file browsers, Reset, explicit
// Save/Load SRAM+state, bindings) are omitted here, noted where their submenu would list them.

import type { AppStores } from "../../../src/appStores";
import type { SystemView } from "../../../src/systemsStore";
import type { ProjectSettings } from "../../../src/projectConfig";
import type { UserConfig } from "../../../src/userConfig";
import { SRAM_AUTO_SAVES } from "../../../src/userConfig";
import type { RecentView } from "../../../src/recentStore";
import type { SelectionOutcome } from "../../../src/fileSelection";
import type { FileBrowserOpts } from "../../../src/backend";
import type { MenuItem, MenuTree } from "./menuTree";

/** Everything a builder reads (current values) + mutates through (the stores). Rebuilt each render. */
export interface MenuContext {
  stores: AppStores;
  system?: SystemView; // the anchored system (instance menu)
  settings: ProjectSettings;
  userConfig: UserConfig;
  systems: SystemView[];
  recent: RecentView[];
  version: string;
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

// Glob filters for the project file dialogs (realBackend space-joins them for DPF).
const PROJECT_PATTERNS = ["*.rplg"];
const ZIP_PATTERNS = ["*.zip"];

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
    action("sys-reload", `Reload on ROM Change: ${sys.settings.reloadOnRomChange ? "On" : "Off"}`, () =>
      systems.setReloadOnRomChange(sys.id, !sys.settings.reloadOnRomChange),
    ),
  ];
  // SameBoy-only core knobs.
  if (sys.core === "sameboy") {
    const cfg = sameboyConfig(sys);
    items.push(
      cycler("sys-model", "Model", MODEL_NAMES, cfg.model, (n) => systems.setRoleConfig(sys.id, "sameboy", { model: n })),
      cycler("sys-highpass", "Highpass", HIGHPASS_NAMES, cfg.highpass, (n) => systems.setRoleConfig(sys.id, "sameboy", { highpass: n })),
      action("sys-fastboot", `Fast Boot: ${cfg.fastBoot ? "On" : "Off"}`, () => systems.setRoleConfig(sys.id, "sameboy", { fastBoot: !cfg.fastBoot })),
    );
  }
  // Deferred (need native methods): Reset, Save/Load State, Save/Load/New SRAM.
  return items;
}

function projectChildren(ctx: MenuContext): MenuItem[] {
  const project = ctx.stores.project;
  const items: MenuItem[] = [];
  if (ctx.systems.length > 0) {
    items.push(action("proj-new", "New Project", () => project.newProject()));
    // Save writes to the known path when there is one (else Save As covers it). Save As / Export browse
    // for a target; each store method already takes a resolved path.
    if (project.currentPath()) items.push(action("proj-save", "Save Project", () => project.save(project.currentPath())));
    items.push(action("proj-saveas", "Save Project As...", () =>
      browseThen(ctx, { title: "Save Project", patterns: PROJECT_PATTERNS, saving: true, defaultName: "project.rplg" }, (p) => project.save(p)),
    ));
    items.push(action("proj-export", "Export Zip...", () =>
      browseThen(ctx, { title: "Export Zip", patterns: ZIP_PATTERNS, saving: true, defaultName: "project.zip" }, (p) => project.export(p)),
    ));
  }
  // Load is always available (even from an empty start menu).
  items.push(action("proj-load", "Load Project...", () =>
    browseThen(ctx, { title: "Load Project", patterns: PROJECT_PATTERNS }, (p) => void project.load(p)),
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

function settingsChildren(ctx: MenuContext): MenuItem[] {
  const userConfig = ctx.stores.userConfig;
  const sramIdx = Math.max(0, SRAM_AUTO_SAVES.indexOf(ctx.userConfig.sramAutoSave));
  return [
    cycler("set-sram", "SRAM Auto-Save", SRAM_AUTO_SAVES.map((m) => SRAM_AUTO_SAVE_LABELS[m] ?? m), sramIdx, (n) => userConfig.setSramAutoSave(SRAM_AUTO_SAVES[n])),
    { id: "set-defzoom", label: `Default Zoom: ${ctx.userConfig.defaultZoom}x`, kind: "cycler", keepOpen: true, onSelect: () => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, 1)), onCycle: (dir) => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, dir)) },
    // Deferred: Keyboard/Gamepad Bindings (editor), Open Settings Folder.
  ];
}

function recentChildren(ctx: MenuContext): MenuItem[] {
  if (ctx.recent.length === 0) return [action("recent-none", "(No Recent Files)", () => {})];
  return ctx.recent.map((entry, i) =>
    submenu(`recent-${i}`, entry.label, [
      action(`recent-${i}-load`, entry.missing ? "Load (missing)" : "Load", () => ctx.stores.project.load(entry.path)),
      action(`recent-${i}-locate`, "Locate on Disk", () =>
        browseThen(ctx, { title: "Locate Project", patterns: PROJECT_PATTERNS }, (p) => ctx.stores.recent.relink(entry.path, p)),
      ),
      action(`recent-${i}-remove`, "Remove from List", () => ctx.stores.recent.remove(entry.path)),
      // Deferred: Rename (needs a text prompt).
    ]),
  );
}

// --- top-level builders -------------------------------------------------------------------------------
export function buildInstanceMenu(ctx: MenuContext): MenuTree {
  const sys = ctx.system!;
  const systems = ctx.stores.project.systems;
  return {
    title: ctx.version ? `RetroPlug v${ctx.version} - #${sys.id}` : `RetroPlug - #${sys.id}`,
    items: [
      action("inst-dup", "Duplicate Instance", () => systems.duplicateSystem(sys.id)),
      action("inst-remove", "Remove Instance", () => systems.removeSystem(sys.id)),
      action("inst-load", "Load ROM", () => runSelection(ctx, ctx.stores.fileSelection.browse("load"))),
      action("inst-add", "Add Instance", () => runSelection(ctx, ctx.stores.fileSelection.browse("add"))),
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
      action("start-mgb", "Load mGB (GB MIDI Synth)", () => ctx.stores.project.systems.loadMgb()),
      action("start-load", "Load...", () => runSelection(ctx, ctx.stores.fileSelection.browse("load"))),
      submenu("start-recent", "Recent", recentChildren(ctx)),
      sep("start-sep0"),
      submenu("start-project", "Project", projectChildren(ctx)),
      submenu("start-settings", "Settings", settingsChildren(ctx)),
      // Deferred: About panel.
    ],
  };
}

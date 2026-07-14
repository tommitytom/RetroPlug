// The menu trees, ported from legacy menuDefs.tsx. Every leaf drives a store method; current
// values are baked into labels and recomputed each render (there's no separate "checked" state — the
// label IS the display). Actions still gated on a deferred backend surface (the bindings editor) are
// omitted here, noted where their submenu would list them.

import type { AppStores } from "../../../src/appStores";
import type { SystemView } from "../../../src/systemsStore";
import type { ProjectSettings } from "../../../src/projectConfig";
import {
  LAYOUT_VALUES,
  MIDI_ROUTING_VALUES,
  AUDIO_ROUTING_VALUES,
  MODEL_VALUES,
  HIGHPASS_VALUES,
  REGION_VALUES,
  LSDJ_MODE_VALUES,
  type SameBoyModel,
  type SameBoyHighpass,
  type ConsoleRegion,
  type LsdjSyncMode,
} from "../../../src/settingsEnums";
import type { UserConfig } from "../../../src/userConfig";
import { SRAM_AUTO_SAVES } from "../../../src/userConfig";
import { defaultBindingMap, type BindingMap } from "../../../src/bindingMap";
import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
import type { RecentView } from "../../../src/recentStore";
import { resolveSavPath, siblingPath } from "../../../src/savPaths";
import { stem } from "../../../src/pathUtil";
import { openPath } from "../../lvgl/openPath";
import { saveProjectInteractive } from "../../lvgl/saveProjectInteractive";
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
  loadRomAsProject: (romPath: string, explicitSav?: string) => void;
}

// --- name tables (mirror the native enums, ported from legacy menuDefs.tsx) ---------------------------
const MIDI_ROUTING_NAMES = ["Send to All", "4 Ch / Inst", "1 Ch / Inst", "Ch -> Inst"];
// Index 3 (ChannelSplit) fans one Game Boy's 4 channels across the 8 outputs; offered only for a
// single system (see projectChildren) — native gates it to systemCount()==1 too.
const AUDIO_ROUTING_NAMES = ["Stereo", "2 Ch / Inst", "1 Ch / Inst", "Channels (1 GB)"];
const LAYOUT_NAMES = ["Auto", "Row", "Column", "Grid"];
const MODEL_NAMES = ["Auto", "DMG-B", "MGB", "SGB", "SGB PAL", "SGB2", "CGB-0", "CGB-A", "CGB-B", "CGB-C", "CGB-D", "CGB-E", "AGB", "GBP"];
const HIGHPASS_NAMES = ["Off", "Accurate", "DC-Block"];
const SRAM_AUTO_SAVE_LABELS: Record<string, string> = { Off: "Off", OnProjectSave: "On Save", Continuous: "Continuous" };
// Link Group cycles 0..4 (0 = Off), mirroring the legacy LINK_GROUP_MAX.
const LINK_GROUP_NAMES = ["Off", "1", "2", "3", "4"];
// NES console region (ConsoleRegion 0..4), the "mesen" role's region knob.
const REGION_NAMES = ["Auto", "NTSC", "PAL", "Dendy", "NTSC-J"];
const OFF_ON = ["Off", "On"]; // boolean toggles rendered as 2-value cyclers (Left/Right + Enter step)
// LSDj sync modes (LsdjSyncMode 0..8). All shown; Keyboard(4) is not yet driven. "MIDI Out"(7, SYNC=MI.OUT)
// and "Master Sync"(8, SYNC=LSDJ) are the two LSDj→host MIDI-out modes. Tempo divisor subdivides the 24-PPQN clock.
const LSDJ_MODE_NAMES = ["Off", "MIDI Sync", "MIDI Sync (Arduinoboy)", "MIDI Map", "Keyboard", "Keyboard MIDI", "MIDI Passthrough", "MIDI Out", "Master Sync"];
const LSDJ_DIVISORS = [1, 2, 4, 8];

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
function action(id: string, label: string, onSelect: () => void, disabled = false): MenuItem {
  // Only attach `disabled` when set, so every enabled action stays byte-identical (toEqual-stable in tests).
  return disabled ? { id, label, kind: "action", onSelect, disabled } : { id, label, kind: "action", onSelect };
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

/** The shared "Load…" leaf (start + instance): browse a ROM/sav (resolve-only), then apply the pick behind
 *  the unsaved-changes guard — a sibling `<rom>.rplg` loads that project, a fresh ROM opens as a new project.
 *  Never mutates an existing instance (that's "Replace Instance"). Fire-and-forget; the store's change
 *  notification re-renders when it lands. */
function runLoad(ctx: MenuContext): void {
  void ctx.stores.fileSelection.resolveLoad().then((r) => {
    if (r.kind === "project") ctx.loadProject(r.path);
    else if (r.kind === "rom") ctx.loadRomAsProject(r.romPath, r.explicitSav);
    // cancelled / error: nothing to apply (a bad pair target silently no-ops, as before).
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
function sameboyConfig(sys: SystemView): { model: SameBoyModel; highpass: SameBoyHighpass; linkGroupId: number; fastBoot: boolean } {
  const c = (sys.roles.find((r) => r.kind === "sameboy")?.config ?? {}) as Record<string, unknown>;
  return {
    model: typeof c.model === "string" ? (c.model as SameBoyModel) : "cgbC",
    highpass: typeof c.highpass === "string" ? (c.highpass as SameBoyHighpass) : "accurate",
    linkGroupId: typeof c.linkGroupId === "number" ? c.linkGroupId : 0,
    fastBoot: c.fastBoot !== false,
  };
}

/** The Mesen core-role config (region / removeSpriteLimit), with defaults. The role attaches to any
 *  Mesen system; the knobs are NES-only, so the menu gates the rows on platform === "nes". */
function mesenConfig(sys: SystemView): { region: ConsoleRegion; removeSpriteLimit: boolean } {
  const c = (sys.roles.find((r) => r.kind === "mesen")?.config ?? {}) as Record<string, unknown>;
  return {
    region: typeof c.region === "string" ? (c.region as ConsoleRegion) : "auto",
    removeSpriteLimit: c.removeSpriteLimit === true,
  };
}

// --- child builders -----------------------------------------------------------------------------------
function systemChildren(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const systems = ctx.stores.project.systems;
  // Reset reboots carrying the battery — pathless, reconstructing in place (no live GB_reset). Sits at the
  // top with a separator below it.
  const items: MenuItem[] = [
    action("sys-reset", "Reset", () => void systems.reset(sys.id)),
    sep("sys-sep-reset"),
    cycler("sys-reload", "Reload on ROM Change", OFF_ON, sys.settings.reloadOnRomChange ? 1 : 0, (n) =>
      systems.setReloadOnRomChange(sys.id, n === 1),
    ),
  ];
  // SameBoy-only core knobs.
  if (sys.core === "sameboy") {
    const cfg = sameboyConfig(sys);
    items.push(
      cycler("sys-model", "Model", MODEL_NAMES, Math.max(0, MODEL_VALUES.indexOf(cfg.model)), (n) => systems.setRoleConfig(sys.id, "sameboy", { model: MODEL_VALUES[n] })),
      cycler("sys-highpass", "Highpass", HIGHPASS_NAMES, Math.max(0, HIGHPASS_VALUES.indexOf(cfg.highpass)), (n) => systems.setRoleConfig(sys.id, "sameboy", { highpass: HIGHPASS_VALUES[n] })),
      cycler("sys-fastboot", "Fast Boot", OFF_ON, cfg.fastBoot ? 1 : 0, (n) => systems.setRoleConfig(sys.id, "sameboy", { fastBoot: n === 1 })),
    );
  }
  // NES-only core knobs (the "mesen" role also attaches to GBA, so gate on platform, not core).
  if (sys.platform === "nes") {
    const cfg = mesenConfig(sys);
    items.push(
      cycler("sys-nes-region", "Region", REGION_NAMES, Math.max(0, REGION_VALUES.indexOf(cfg.region)), (n) => systems.setRoleConfig(sys.id, "mesen", { region: REGION_VALUES[n] })),
      cycler("sys-nes-spritelimit", "Remove Sprite Limit", OFF_ON, cfg.removeSpriteLimit ? 1 : 0, (n) =>
        systems.setRoleConfig(sys.id, "mesen", { removeSpriteLimit: n === 1 }),
      ),
    );
  }
  // Save/Load SRAM + State. The quick "Save SRAM"/"Save State" write to the ROM's sibling path with no
  // dialog (a real ROM only — the embedded synth has no on-disk target); the "As…" variants browse. The
  // store reads/writes the resolved path (the registry read is safe while playing; load reconstructs the
  // core in place). New SRAM reboots with a blank battery — pathless, reconstructing in place (no live
  // clearSram). The two Save-SRAM rows grey out for a battery-less cart (no save memory → only a stray
  // empty .sav); New / Load SRAM stay live (they touch the running core, not an on-disk artifact).
  const romStem = stem(sys.romPath);
  const noSave = !sys.battery;
  items.push(
    sep("sys-sep-state"),
    action("sys-newsram", "New SRAM", () => void systems.newSram(sys.id)),
    action("sys-loadsram", "Load SRAM...", () =>
      browseThen(ctx, { title: "Load SRAM", patterns: SRAM_PATTERNS }, (p) => void systems.loadSram(sys.id, p)),
    ),
  );
  if (sys.romPath)
    items.push(
      action("sys-quicksavesram", "Save SRAM", () => systems.saveSram(sys.id, resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath)), noSave),
    );
  items.push(
    action(
      "sys-savesram",
      "Save SRAM As...",
      () =>
        browseThen(ctx, { title: "Save SRAM", patterns: SRAM_PATTERNS, saving: true, defaultName: `${romStem || "sram"}.sav` }, (p) => systems.saveSram(sys.id, p)),
      noSave,
    ),
    action("sys-loadstate", "Load State...", () =>
      browseThen(ctx, { title: "Load State", patterns: STATE_PATTERNS }, (p) => void systems.loadState(sys.id, p)),
    ),
  );
  if (sys.romPath)
    items.push(action("sys-quicksavestate", "Save State", () => systems.saveState(sys.id, siblingPath(sys.romPath, sys.savSuffix, ".ss0"))));
  items.push(
    action("sys-savestate", "Save State As...", () =>
      browseThen(ctx, { title: "Save State", patterns: STATE_PATTERNS, saving: true, defaultName: `${romStem || "savestate"}.ss0` }, (p) => systems.saveState(sys.id, p)),
    ),
  );
  return items;
}

/** The LSDj sync submenu — Mode + Tempo Divisor cyclers. Shown only for a system carrying an lsdj-sync
 *  role (a sniffed LSDj cart). Both edits re-push the DSP kernel structure (setRoleConfig → markDirty →
 *  syncDspFromStore), so they apply to the running behaviour on the next block — no dedicated RPC. */
function lsdjChildren(ctx: MenuContext, sys: SystemView, cfg: Record<string, unknown>): MenuItem[] {
  const systems = ctx.stores.project.systems;
  const mode = typeof cfg.mode === "string" ? (cfg.mode as LsdjSyncMode) : "midiSync";
  const divisor = typeof cfg.tempoDivisor === "number" ? cfg.tempoDivisor : 1;
  return [
    cycler("lsdj-mode", "Mode", LSDJ_MODE_NAMES, Math.max(0, LSDJ_MODE_VALUES.indexOf(mode)), (n) => systems.setRoleConfig(sys.id, "lsdj-sync", { mode: LSDJ_MODE_VALUES[n] })),
    cycler("lsdj-divisor", "Tempo Divisor", LSDJ_DIVISORS.map(String), Math.max(0, LSDJ_DIVISORS.indexOf(divisor)), (n) =>
      systems.setRoleConfig(sys.id, "lsdj-sync", { tempoDivisor: LSDJ_DIVISORS[n] }),
    ),
  ];
}

function projectChildren(ctx: MenuContext): MenuItem[] {
  const project = ctx.stores.project;
  const items: MenuItem[] = [];
  // Order: New → Load → Save → Save As → Export. New/Save/SaveAs/Export need a project (systems > 0); Load
  // is always available (even from an empty start menu, where it's the only file op).
  if (ctx.systems.length > 0) items.push(action("proj-new", "New Project", () => ctx.newProject()));
  // Load is guarded + outcome-aware via ctx.loadProject.
  items.push(action("proj-load", "Load Project...", () =>
    browseThen(ctx, { title: "Load Project", patterns: LOAD_PATTERNS }, (p) => ctx.loadProject(p)),
  ));
  if (ctx.systems.length > 0) {
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
  items.push(sep("proj-sep0"));
  items.push(
    cycler("proj-layout", "Layout", LAYOUT_NAMES, Math.max(0, LAYOUT_VALUES.indexOf(ctx.settings.layout)), (n) => project.setLayout(LAYOUT_VALUES[n])),
    { id: "proj-zoom", label: `Zoom: ${ctx.settings.zoom === 0 ? "Default" : `${ctx.settings.zoom}x`}`, kind: "cycler", keepOpen: true, onSelect: () => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, 1)), onCycle: (dir) => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, dir)) },
    sep("proj-sep1"),
    cycler("proj-midi", "MIDI Routing", MIDI_ROUTING_NAMES, Math.max(0, MIDI_ROUTING_VALUES.indexOf(ctx.settings.midiRouting)), (n) => project.setMidiRouting(MIDI_ROUTING_VALUES[n])),
    // channelSplit (index 3) is single-system-only, so the cycler drops it with 0 or >1 systems (the
    // per-instance modes stay — they're the multi-system routes). Native is the authority and can't
    // mis-route regardless; this is UX only.
    cycler("proj-audio", "Audio Routing", ctx.systems.length === 1 ? AUDIO_ROUTING_NAMES : AUDIO_ROUTING_NAMES.slice(0, 3), Math.max(0, AUDIO_ROUTING_VALUES.indexOf(ctx.settings.audioRouting)), (n) => project.setAudioRouting(AUDIO_ROUTING_VALUES[n])),
  );
  return items;
}

// GB button display/edit order (mirrors the legacy bindings editor).
const GB_BUTTONS = ["Right", "Left", "Up", "Down", "A", "B", "Select", "Start"];

// The rebindable app actions (AppAction id → display label), shown below the GB buttons in each channel's
// editor. Same capture plumbing as a GB row, but written into the keyboardActions/gamepadActions section.
const APP_ACTION_ROWS: { id: string; label: string }[] = [
  { id: "OpenMenu", label: "Open Menu" },
  { id: "CycleNext", label: "Cycle Instances" },
  { id: "CyclePrev", label: "Cycle Instances (Back)" },
];

type BindingsChannel = "keyboard" | "gamepad";

/** The bindings editor for one channel: a profile switcher, one capture row per GB button (Enter arms, the
 *  next key/button rebinds, Backspace clears), a channel reset, and named-profile management (New / Rename /
 *  Delete). Write-through / edit-active — every edit + profile switch re-resolves and the live joypad follows
 *  via useGameInput / useGamepadInput. Both channels share this; only the active profile, the channel key,
 *  and the capture source differ. */
function bindingsChildren(ctx: MenuContext, channel: BindingsChannel): MenuItem[] {
  const bindings = ctx.stores.bindings;
  const userConfig = ctx.stores.userConfig;
  const kbName = ctx.userConfig.activeKeyboardBindings;
  const gpName = ctx.userConfig.activeGamepadBindings;
  const activeName = channel === "keyboard" ? kbName : gpName;
  const setActive = (n: string): boolean =>
    channel === "keyboard" ? userConfig.setActiveKeyboardBindings(n) : userConfig.setActiveGamepadBindings(n);
  const profiles = bindings.availableProfiles();
  const chMap = ctx.bindings[channel]; // resolved active channel map — recomputed each render
  // Distinct id prefix per channel; "bind" for keyboard keeps its existing row ids stable.
  const idp = channel === "keyboard" ? "bind" : "bind-gp";
  const label = channel === "keyboard" ? "Keyboard" : "Gamepad";

  const actionsKey: "keyboardActions" | "gamepadActions" = channel === "keyboard" ? "keyboardActions" : "gamepadActions";
  const actMap = ctx.bindings[actionsKey]; // resolved active app-action map

  const withChannel = (m: BindingMap, chan: Record<string, string[]>): BindingMap =>
    channel === "keyboard" ? { ...m, keyboard: chan } : { ...m, gamepad: chan };
  const write = (edit: (m: BindingMap) => BindingMap) => {
    const map = bindings.loadProfile(activeName) ?? defaultBindingMap();
    bindings.saveProfile(activeName, edit(map));
  };
  const setBtn = (btn: string, vals: string[]) => write((m) => withChannel(m, { ...m[channel], [btn]: vals }));
  const setAction = (id: string, vals: string[]) => write((m) => ({ ...m, [actionsKey]: { ...m[actionsKey], [id]: vals } }));

  // Create a named copy of the current bindings and make it active. Errors surface in the prompt's red line.
  const newProfile = (raw: string): string | null => {
    const n = raw.trim();
    if (!isValidProfileName(n)) return "Invalid name (A-Z, 0-9, _, -).";
    if (profiles.includes(n)) return "Profile already exists.";
    const cur = bindings.loadProfile(activeName) ?? defaultBindingMap();
    if (!bindings.saveProfile(n, { ...cur, name: n })) return "Save failed.";
    setActive(n);
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
  const deletable = profiles.filter((p) => p !== kbName && p !== gpName);
  const deleteChildren: MenuItem[] = deletable.length
    ? deletable.map((p) => ({
        id: `${idp}-del-${p}`,
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
    : [action(`${idp}-del-none`, "(no other profiles)", () => {})];

  const captureRows: MenuItem[] = GB_BUTTONS.map((btn) => ({
    id: `${idp}-${btn}`,
    label: `${btn}: ${chMap[btn]?.length ? chMap[btn].join(", ") : "-"}`,
    kind: "capture" as const,
    keepOpen: true,
    capture: {
      source: channel,
      onCapture: (name: string) => setBtn(btn, [name]),
      onClear: () => setBtn(btn, []),
    },
  }));

  // The app-action rows (Open Menu / Cycle Instances / Cycle Instances (Back)) — same capture plumbing as a GB
  // row, written into the actions section. The "<label>: " head is kept so Menu's "Press a key/button…" swap works.
  const actionRows: MenuItem[] = APP_ACTION_ROWS.map((a) => ({
    id: `${idp}-act-${a.id}`,
    label: `${a.label}: ${actMap[a.id]?.length ? actMap[a.id].join(", ") : "-"}`,
    kind: "capture" as const,
    keepOpen: true,
    capture: {
      source: channel,
      onCapture: (name: string) => setAction(a.id, [name]),
      onClear: () => setAction(a.id, []),
    },
  }));

  return [
    cycler(`${idp}-profile`, "Profile", profiles, Math.max(0, profiles.indexOf(activeName)), (n) => setActive(profiles[n])),
    sep(`${idp}-sep-top`),
    ...captureRows,
    sep(`${idp}-sep-actions`),
    ...actionRows,
    sep(`${idp}-sep-reset`),
    // This channel only — the GB buttons AND the app actions — preserving the profile's OTHER channel.
    action(`${idp}-reset`, `Reset ${label} to Defaults`, () =>
      write((m) => ({ ...withChannel(m, defaultBindingMap()[channel]), [actionsKey]: defaultBindingMap()[actionsKey] })),
    ),
    sep(`${idp}-sep-mgmt`),
    { id: `${idp}-new`, label: "New Profile...", kind: "prompt", keepOpen: true, prompt: { title: "New profile name:", filter: isValidProfileChar, onConfirm: newProfile } },
    { id: `${idp}-rename`, label: "Rename...", kind: "prompt", keepOpen: true, prompt: { title: `Rename "${activeName}" to:`, initial: activeName, filter: isValidProfileChar, onConfirm: renameActive } },
    submenu(`${idp}-delete`, "Delete Profile", deleteChildren),
  ];
}

function settingsChildren(ctx: MenuContext): MenuItem[] {
  const userConfig = ctx.stores.userConfig;
  const sramIdx = Math.max(0, SRAM_AUTO_SAVES.indexOf(ctx.userConfig.sramAutoSave));
  return [
    cycler("set-sram", "SRAM Auto-Save", SRAM_AUTO_SAVES.map((m) => SRAM_AUTO_SAVE_LABELS[m] ?? m), sramIdx, (n) => userConfig.setSramAutoSave(SRAM_AUTO_SAVES[n])),
    { id: "set-defzoom", label: `Default Zoom: ${ctx.userConfig.defaultZoom}x`, kind: "cycler", keepOpen: true, onSelect: () => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, 1)), onCycle: (dir) => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, dir)) },
    submenu("set-keybindings", "Keyboard Bindings", bindingsChildren(ctx, "keyboard")),
    submenu("set-gamepad-bindings", "Gamepad Bindings", bindingsChildren(ctx, "gamepad")),
    action("set-open-folder", "Open Settings Folder", () => openPath(ctx.stores.backend.configDir())),
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

/** The standalone OS window title: "RetroPlug v<version> - <project>" (no ROM name). Empty segments are
 *  dropped, so a nameless project shows just "RetroPlug v<version>". */
export function composeWindowTitle(version: string, project: string): string {
  const base = version ? `RetroPlug v${version}` : "RetroPlug";
  return project ? `${base} - ${project}` : base;
}

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
  const multi = ctx.systems.length > 1; // Replace / Remove / Link Group are peer-only rows
  const lsdj = sys.roles.find((r) => r.kind === "lsdj-sync"); // present iff the ROM sniffed as LSDj
  return {
    title: instanceTitle(ctx, sys),
    items: [
      // "Load…" is a project-level op (load the sibling project / new project from the ROM) — it never
      // swaps this instance. Swapping a single instance in place is "Replace Instance".
      action("inst-load", "Load...", () => runLoad(ctx)),
      action("inst-save", "Save Project", () => void saveProjectInteractive(ctx.stores)),
      action("inst-new", "New Project", () => ctx.newProject()),
      submenu("inst-recent", "Recent", recentChildren(ctx)),
      sep("inst-sep-top"),
      action("inst-add", "Add Instance", () => void ctx.stores.fileSelection.browseAdd(sys.id)),
      action("inst-dup", "Duplicate Instance", () => {
        const id = systems.duplicateSystem(sys.id);
        if (id != null) systems.inheritLinkGroup(id, sys.id);
      }),
      // Replace / Remove only make sense with a peer instance — hidden for a lone instance.
      ...(multi
        ? [
            action("inst-replace", "Replace Instance", () => void ctx.stores.fileSelection.browseReplace(sys.id)),
            action("inst-remove", "Remove Instance", () => systems.removeSystem(sys.id)),
          ]
        : []),
      // Link Group is SameBoy-only (the GB serial link cable); on a NES/GBA peer it would be a dead
      // always-"Off" row, so gate it on the core too — not just on having a peer.
      ...(multi && sys.core === "sameboy"
        ? [
            sep("inst-sep-link"),
            cycler("inst-link", "Link Group", LINK_GROUP_NAMES, sameboyConfig(sys).linkGroupId, (n) =>
              systems.setRoleConfig(sys.id, "sameboy", { linkGroupId: n }),
            ),
          ]
        : []),
      sep("inst-sep1"),
      submenu("inst-system", "System", systemChildren(ctx, sys)),
      ...(lsdj ? [submenu("inst-lsdj", "LSDj", lsdjChildren(ctx, sys, lsdj.config))] : []),
      submenu("inst-project", "Project", projectChildren(ctx)),
      submenu("inst-settings", "Settings", settingsChildren(ctx)),
      // Deferred: About panel.
    ],
  };
}

export function buildStartMenu(ctx: MenuContext): MenuTree {
  return {
    title: ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug",
    items: [
      submenu("start-recent", "Recent", recentChildren(ctx)),
      action("start-load", "Load...", () => runLoad(ctx)),
      action("start-mgb", "Load mGB (GB MIDI Synth)", () => ctx.stores.project.systems.loadMgb()),
      sep("start-sep0"),
      submenu("start-project", "Project", projectChildren(ctx)),
      submenu("start-settings", "Settings", settingsChildren(ctx)),
      // Deferred: About panel.
    ],
  };
}

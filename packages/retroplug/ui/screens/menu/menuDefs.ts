// The menu trees, ported from legacy menuDefs.tsx. Every leaf drives a store method; current
// values are baked into labels and recomputed each render (there's no separate "checked" state — the
// label IS the display). The bindings editor is no longer deferred - it is `bindingsChildren` below,
// a profile switcher plus a capture row per button.

import type { SystemInfo } from "@retroplug/core/host";
import type { AppStores } from "../../../src/appStores";
import type { SystemView } from "../../../src/systemsStore";
import type { ProjectSettings } from "../../../src/projectConfig";
import { MIDI_ROUTING_VALUES } from "@retroplug/core/midiRouting";
import { consoleOfPlatform } from "../../../src/consoles";
import { roleRowItems } from "./configRows";
import {
  contributedActions,
  contributedTrackerUi,
  firstWinsMenuIds,
  menuPlacement,
  menuPlacementOpen,
  roleRows,
} from "../../host/menuContributions";
import { LAYOUT_VALUES, AUDIO_ROUTING_VALUES, type AudioRouting } from "../../../src/settingsEnums";
import type { UserConfig } from "../../../src/userConfig";
import { SRAM_AUTO_SAVES, RENDER_SAMPLE_RATES, RENDER_ON_EXISTS } from "../../../src/userConfig";
import type { SplitMode } from "../../../src/render";
import { defaultBindingMap, withActionDefaults, type BindingMap } from "../../../src/bindingMap";
import { keyDisplayName } from "../../../src/keyCodes";
import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
import type { RecentView } from "../../../src/recentStore";
import { resolveSavPath, siblingPath, SAV_PATTERNS } from "../../../src/savPaths";
import { stem, dirname, basename, extension, joinPath, sanitizeName, shortenMiddle } from "../../../src/pathUtil";
import { ROM_PATTERNS } from "../../../src/fileSelection";
import { filesPngHost } from "../../../src/hostGroups";
import { createUiHost, type UiHost } from "../../host/uiHost";
import { embeddedRomOf, embeddedRoms } from "../../../src/embeds";
import {
  savEditWouldDiscard,
  saveWorkingSong,
  songLoadWouldDiscard,
} from "../../../src/tracker/liveSav";
import { effectiveAssets, readAssetOverrides } from "@retroplug/tracker/assetCatalog";
import type { AssetOverride, AssetSlotRow, AssetTypeInfo } from "@retroplug/tracker/assetCatalog";
import { workingSongTargets } from "@retroplug/tracker/songCatalog";
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import type { ContributedAction } from "@retroplug/ui/contribution";
import { resolveTracker } from "../../../src/trackerRegistry";
import type { HostBackend, ControlPlaneBackend } from "../../../src/backend";
import { openPath } from "../../host/openPath";
import { native } from "../../host/nativeHooks";
import { startSystemRender, renderBaseName, validSplits, formatDuration } from "../../host/render";
import { saveProjectInteractive } from "../../app/saveProjectInteractive";
import { hasUnsavedChanges, unsavedChanges } from "../../../src/unsavedChanges";
import { SONG_BLOCKED } from "../../app/unsavedRows";
import type { AssetMenuSpec, MenuBuildContext, SongMenuSpec } from "@retroplug/ui/contribution";
import type { MenuAction, MenuItem, MenuTree } from "@retroplug/ui/menu";
import { roleConfig } from "@retroplug/core/systemRoles";
import {
  action,
  actionCycler,
  browseThen,
  cycleInt,
  cycler,
  infoRow,
  separator,
  submenu,
} from "@retroplug/ui/menu";

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
  /** `song` (a recent row's) is loaded into the cart once the project lands - reopening that song, not just
   *  the project. Omitted (Load Project… / a file drop) leaves whatever song the project's sav carries. */
  loadProject: (path: string, song?: string) => void;
  loadRomAsProject: (romPath: string, explicitSav?: string) => void;
  /** Quit the standalone (unsaved-changes guarded). No-op in a DAW (the host owns the window). */
  requestExit: () => void;
  // The one input the FALLBACK host (below) needs: a composer that has no `host` of its own is a menu test's context,
  // built over the stores alone, and `hostOf` builds a host over this picker. App passes a host, so its own host is
  // what a contribution uses there.
  //
  // Open the Songs "import from a .sav" picker (validate the source against the cart's console, then show a
  // checkbox list) - owned by useSongImport, wired from App like the project modals.
  beginSongImport: (sys: SystemInfo, source: Uint8Array) => void;
  /** The UI host a contributed builder or cart callback reaches the shell through. App passes the one it built over
   *  the stores; a composer that has only the stores (a menu test) leaves it out and `hostOf` builds one over this
   *  context's `beginSongImport` and the generated view registry, so a contribution always has a host, though only
   *  App's has a window for a window view to open in. */
  host?: UiHost;
  /** The contributed app actions the bindings editor lists after the shell's three. App leaves it out and the
   *  generated registry answers; a test passes its own action, which is the only way a row for an action no product
   *  ships can be built. */
  actions?: readonly ContributedAction[];
}

/** The UI host for a context: the one App built when it passed one, else one built over the stores and this
 *  context's `beginSongImport`, cached for the context's life so a callback sees a stable host. */
const hosts = new WeakMap<MenuContext, UiHost>();
function hostOf(ctx: MenuContext): UiHost {
  const given = ctx.host;
  if (given) return given;
  let built = hosts.get(ctx);
  if (!built) {
    // A context built over the stores alone (the menu tests) has no window to open a view in, and no view registry
    // either: App's `views.open` is the one place an id is looked up and its refusals worded. This host imports no view
    // function on purpose, because importing viewContributions would pull the generated views module, and with it a
    // component view's React, into every pure-tier test that reaches this file.
    const views = {
      open: (id: string) => console.warn(`[views] the menu's host has no window to open ${id} in`),
    };
    built = createUiHost(ctx.stores, { views, beginImport: ctx.beginSongImport });
    hosts.set(ctx, built);
  }
  return built;
}

/** True only in a standalone host (the SDL handheld build or the DPF JACK standalone), which installs
 *  __rp_isStandalone; a DAW-hosted editor and the headless harness leave it undefined. Gates the "Exit"
 *  row — a DAW owns the plugin window, so it must not offer to quit. */
function isStandalone(): boolean {
  return native.__rp_isStandalone === true;
}

// --- name tables (mirror the native enums, ported from legacy menuDefs.tsx) ---------------------------
const MIDI_ROUTING_NAMES = ["Send to All", "4 Ch / Inst", "1 Ch / Inst", "Ch -> Inst"];
// Indices 3..5 (channelSplit / pinSplit / stereoPinSplit) fan ONE system's channels across the 8 outputs,
// so they're offered only for a single system — and the two pin rows only for a NES, the only console
// with output pins to split (see validAudioRoutings). Native gates them the same way and is the
// authority. "Channels" / "Pins" deliberately match the System > Render submenu's SPLIT_LABELS: same
// concept, same words. "Stereo Pins" spends a whole output pair on each pin so a DAW that can only
// bus/FX whole stereo pairs can treat each one separately.
const AUDIO_ROUTING_NAMES = ["Stereo", "2 Ch / Inst", "1 Ch / Inst", "Channels", "Pins", "Stereo Pins"];

/** The NES-only routing modes: both pin splits. A GB has no 2A03 output pins to fan out. */
const NES_ONLY_ROUTINGS: readonly AudioRouting[] = ["pinSplit", "stereoPinSplit"];

/** Which routing modes the Audio Routing cycler offers for a project. UX only — native re-checks.
 *  A split needs exactly one system (a lone system has no link peers), and the pin rows additionally
 *  need an output layout that HAS pins, which is the console descriptor's own declaration (`splits.pins`: the NES).
 *  Mirrors `validSplits` (ui/host/render.ts), which gates the Render submenu on the same declaration. */
export function validAudioRoutings(systems: readonly Pick<SystemView, "platform">[]): AudioRouting[] {
  if (systems.length !== 1) return [...AUDIO_ROUTING_VALUES.slice(0, 3)];
  const all = [...AUDIO_ROUTING_VALUES];
  return consoleOfPlatform(systems[0].platform)?.splits?.pins
    ? all
    : all.filter((v) => !NES_ONLY_ROUTINGS.includes(v));
}
const LAYOUT_NAMES = ["Auto", "Row", "Column", "Grid"];
const SRAM_AUTO_SAVE_LABELS: Record<string, string> = { OnProjectSave: "On Save", Continuous: "Continuous" };
const OFF_ON = ["Off", "On"]; // boolean toggles rendered as 2-value cyclers (Left/Right + Enter step)

// Glob filters for the file dialogs (realBackend space-joins them for DPF).
const PROJECT_PATTERNS = ["*.rplg"]; // thin project (raw JSON) — the Save target
const ZIP_PATTERNS = ["*.rplg.zip"]; // exported project (PKZIP) — always `.rplg.zip`
const LOAD_PATTERNS = ["*.rplg", "*.rplg.zip"]; // load/locate accept either on-disk shape
const STATE_PATTERNS = ["*.ss?"]; // slot-numbered savestates (.ss0..ss9), matching legacy
const SRAM_PATTERNS = SAV_PATTERNS; // .sav / .srm — battery saves (some NES/risa carts use .srm)

const SPLIT_LABELS: Record<SplitMode, string> = { mix: "Mix", channels: "Channels", pins: "Pins" };

// --- item helpers -------------------------------------------------------------------------------------
/** `action`, behind a yes/no confirm when running it would throw away an unsaved working song that lives
 *  outside the battery (smsggdj). For the rows that REPLACE what the cart holds - Load SRAM, Load State,
 *  New SRAM, Replace / Remove Instance - so carrying the song across, as Reset does, would be wrong; but
 *  saying nothing lost the song. Asked when the menu is built, and only on a positive signal (see
 *  savEditWouldDiscard): elsewhere it is exactly `action`. Confirms BEFORE any file picker, as the Songs
 *  menu does, so cancelling the picker costs nothing. */
function songGuardedAction(ctx: MenuContext, sys: SystemView, id: string, label: string, run: () => void): MenuItem {
  if (!savEditWouldDiscard(ctx.stores.project.systems, sys)) return action(id, label, run);
  return {
    id,
    label,
    kind: "prompt" as const,
    keepOpen: true,
    prompt: {
      title: `${label.replace(/\.\.\.$/, "")}? The working song has unsaved changes - they will be lost.`,
      hint: "Enter to continue  |  Esc to cancel",
      confirm: true,
      onConfirm: () => {
        run();
        return null;
      },
    },
  };
}

/** "Save Project", with a trailing " *" when the project or any battery SRAM is unsaved (the same signal the
 *  close guard asks). The star is the file's established "modified" marker (cf. the asset-override rows). */
function saveProjectLabel(ctx: MenuContext): string {
  if (!hasUnsavedChanges(ctx.stores.backend, ctx.stores.project)) return "Save Project";
  // A working song Save cannot store (a full cart, an old save format) stays unsaved however often the row
  // is pressed - say so here, where the press visibly did nothing, rather than only in the log.
  const blocked = unsavedChanges(ctx.stores.backend, ctx.stores.project).find((i) => i.kind === "song" && i.blocked);
  return blocked?.kind === "song" && blocked.blocked ? `Save Project * (song: ${SONG_BLOCKED[blocked.blocked]})` : "Save Project *";
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

// --- child builders -----------------------------------------------------------------------------------
// Build the System > Render submenu (a WAV background job on a COPY of the live SRAM/savestate — never the
// running core, like `retroplug-cli render`). NO dialog at render time: the output folder, filename,
// on-exists policy, and routing/rate/duration are explicit rows, and "Render" writes straight to
// <Output Dir>/<Filename>.wav (a prefix + _<channel> for splits). Only built for on-disk ROMs.
// Compose a "<prefix><dir>" menu label that stays on ONE line: the path is middle-elided so the whole label
// (plus the " >" LVGL appends to a submenu row) fits before it wraps. The budget is prefix-aware — a fixed
// path budget overflowed for a long prefix like "Default Render Dir: ", wrapping the row.
const DIR_LABEL_MAX = 38; // total label chars that fit on a row at the default window width (leaves room for " >")
function dirLabel(prefix: string, dir: string): string {
  return `${prefix}${shortenMiddle(dir, Math.max(6, DIR_LABEL_MAX - prefix.length))}`;
}

// The render folder to default to when neither a session override nor the Settings "Default Render Dir" is
// set: the folder the system's .sav lives in (a battery cart), else the ROM's own folder.
function renderDefaultDir(sys: SystemView): string {
  return sys.battery ? dirname(resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath)) : dirname(sys.romPath);
}

function renderSubmenu(ctx: MenuContext, sys: SystemView): MenuItem {
  const userConfig = ctx.stores.userConfig;
  const r = ctx.userConfig.render;
  const splits = validSplits(sys);
  const split = splits.includes(r.split) ? r.split : "mix"; // clamp a stored pins/channels to this platform
  const rateIdx = Math.max(0, RENDER_SAMPLE_RATES.indexOf(r.sampleRate as never));
  const setMaxDur = (delta: number) => userConfig.setRenderMaxDurationSec(r.maxDurationSec + delta);
  // Effective output folder: a per-session override the user picked in this menu, else the Settings "Default
  // Render Dir", else the folder the .sav is in (a battery cart) or the ROM's folder. Filename: a session
  // override the user typed, else re-derived from the loaded song each time the menu opens.
  const effectiveDir = userConfig.renderDir(sys.id) ?? (r.outputDir || renderDefaultDir(sys));
  const curName = userConfig.renderFilename(sys.id) ?? sanitizeName(renderBaseName(ctx.stores.backend, sys));
  const onExistsIdx = Math.max(0, RENDER_ON_EXISTS.indexOf(r.onExists));

  const renderChildren: MenuItem[] = [
    // Output Dir: a native FOLDER picker (our DPF fork). The chosen folder persists to userConfig; the long
    // path is middle-elided to fit the row. keepOpen so the menu stays up to keep configuring after picking.
    {
      id: "sys-render-dir",
      label: dirLabel("Output Dir: ", effectiveDir),
      kind: "action",
      keepOpen: true,
      onSelect: () =>
        // A per-session override (setRenderDir) — deliberately NOT the persisted Settings default.
        browseThen(hostOf(ctx), { title: "Output Dir", patterns: [], directory: true, startDir: effectiveDir }, (p) =>
          userConfig.setRenderDir(sys.id, p),
        ),
    },
    // Filename: an editable text prompt seeded with the derived name; the typed value is remembered for this
    // session only (setRenderFilename), re-derived next time the menu opens.
    {
      id: "sys-render-filename",
      label: `Filename: ${curName}`,
      kind: "prompt",
      keepOpen: true,
      prompt: {
        title: "Render filename:",
        initial: curName,
        filter: isFilenameChar,
        onConfirm: (v: string) => {
          userConfig.setRenderFilename(sys.id, sanitizeName(v));
          return null;
        },
      },
    },
    cycler("sys-render-ifexists", "If Exists", ["Overwrite", "Rename"], onExistsIdx, (n) =>
      userConfig.setRenderOnExists(RENDER_ON_EXISTS[n]),
    ),
    separator("sys-render-sep-opts"),
    cycler("sys-render-split", "Split", splits.map((s) => SPLIT_LABELS[s]), splits.indexOf(split), (n) =>
      userConfig.setRenderSplit(splits[n]),
    ),
    cycler("sys-render-rate", "Sample Rate", RENDER_SAMPLE_RATES.map((hz) => `${hz} Hz`), rateIdx, (n) =>
      userConfig.setRenderSampleRate(RENDER_SAMPLE_RATES[n]),
    ),
    // Max Duration: Left/Right step ±1s, PageUp/PageDown jump ±30s (Menu.tsx routes onCoarseStep).
    {
      id: "sys-render-maxdur",
      label: `Max Duration: ${formatDuration(r.maxDurationSec)}`,
      kind: "cycler",
      keepOpen: true,
      onSelect: () => setMaxDur(1),
      onCycle: (dir) => setMaxDur(dir),
      onCoarseStep: (dir) => setMaxDur(dir * 30),
    },
    separator("sys-render-sep"),
    // Render: no dialog. Writes to <dir>/<name>.wav (the render lib trims .wav to a prefix for splits).
    action(
      "sys-render-go",
      "Render",
      () =>
        void startSystemRender(
          ctx.stores.backend,
          ctx.stores.renders,
          sys,
          { split, sampleRate: r.sampleRate, maxDurationMs: r.maxDurationSec * 1000, onExists: r.onExists },
          joinPath(effectiveDir, `${curName}.wav`),
        ),
    ),
  ];
  return submenu("sys-render", "Render", renderChildren);
}

function systemChildren(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const systems = ctx.stores.project.systems;
  // Reset reboots carrying the battery — pathless, reconstructing in place (no live GB_reset). It leads the
  // menu; the Render submenu (a WAV background job) sits right below it, then a separator. A working song
  // that lives outside the battery (smsggdj) is carried across too - see SystemsStore.reset.
  const items: MenuItem[] = [action("sys-reset", "Reset", () => void systems.reset(sys.id))];
  if (sys.romPath) items.push(renderSubmenu(ctx, sys));
  items.push(
    separator("sys-sep-reset"),
    cycler("sys-reload", "Reload on ROM Change", OFF_ON, sys.settings.reloadOnRomChange ? 1 : 0, (n) =>
      systems.setReloadOnRomChange(sys.id, n === 1),
    ),
  );
  // A console's core-role knobs are its package's declarative rows (its UI contribution), rendered here over the role
  // config: the SameBoy display group and the Mesen per-platform knobs, in the product order their packages sit in.
  items.push(
    ...roleRowItems(roleRows(), sys, (kind) => ctx.stores.registry.roleType(kind), (role, key, value) =>
      systems.setRoleConfig(sys.id, role, { [key]: value }),
    ),
    // Then the rows a package builds itself, where a cycler cannot say it (a folder picked through a dialog: the NES
    // SD card).
    ...menuPlacement("system.settings", hostOf(ctx), sys, buildContext(ctx)),
  );
  // Save/Load SRAM + State. The quick "Save SRAM"/"Save State" write to the ROM's sibling path with no
  // dialog (a real ROM only — the embedded synth has no on-disk target); the "As…" variants browse. The
  // store reads/writes the resolved path (the registry read is safe while playing; load reconstructs the
  // core in place). "New SRAM…" cold-boots a blank battery and repoints the auto-save target to a file you
  // name, so it creates a fresh save rather than silently overwriting the ROM's own <rom>.sav; it's gated
  // on a battery cart (a battery-less cart has no save to create — the dialog would produce nothing), like
  // the Save-SRAM rows. Load SRAM reads a picked save into the live core (any cart). The two Save-SRAM rows
  // grey out for a battery-less cart (no save memory → only a stray empty .sav).
  const romStem = stem(sys.romPath);
  const noSave = !sys.battery;
  // The instance's own sibling .sav name (suffix-aware) — the sensible default target for its fresh save.
  const sramName = sys.savSuffix >= 2 ? `${romStem}-${sys.savSuffix}.sav` : `${romStem}.sav`;
  items.push(separator("sys-sep-state"));
  // Swap the ROM in place but keep the running battery SRAM (e.g. an LSDj version bump that keeps the song).
  // ROM-only browser; distinct from "Replace Instance", which cold-boots a fresh sav. Sits just above the
  // New/Load SRAM rows.
  items.push(action("sys-swaprom", "Swap ROM (Preserve SRAM)...", () => void ctx.stores.fileSelection.browseSwap(sys.id)));
  if (sys.romPath && !noSave)
    items.push(
      songGuardedAction(ctx, sys, "sys-newsram", "New SRAM...", () =>
        browseThen(
          hostOf(ctx),
          { title: "New SRAM", patterns: SRAM_PATTERNS, saving: true, defaultName: sramName },
          (p) => void systems.newSramAs(sys.id, p),
        ),
      ),
    );
  items.push(
    songGuardedAction(ctx, sys, "sys-loadsram", "Load SRAM...", () =>
      browseThen(hostOf(ctx), { title: "Load SRAM", patterns: SRAM_PATTERNS }, (p) => void systems.loadSram(sys.id, p)),
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
        browseThen(
          hostOf(ctx),
          { title: "Save SRAM", patterns: SRAM_PATTERNS, saving: true, defaultName: `${romStem || "sram"}.sav` },
          (p) => systems.saveSram(sys.id, p),
        ),
      noSave,
    ),
    songGuardedAction(ctx, sys, "sys-loadstate", "Load State...", () =>
      browseThen(hostOf(ctx), { title: "Load State", patterns: STATE_PATTERNS }, (p) =>
        void systems.loadState(sys.id, p),
      ),
    ),
  );
  if (sys.romPath)
    items.push(action("sys-quicksavestate", "Save State", () => systems.saveState(sys.id, siblingPath(sys.romPath, sys.savSuffix, ".ss0"))));
  items.push(
    action("sys-savestate", "Save State As...", () =>
      browseThen(
        hostOf(ctx),
        { title: "Save State", patterns: STATE_PATTERNS, saving: true, defaultName: `${romStem || "savestate"}.ss0` },
        (p) => systems.saveState(sys.id, p),
      ),
    ),
  );
  return items;
}

// --- shared tracker asset submenus (kits / palettes-or-themes / fonts) --------------------------------
// LSDj + risa both expose replaceable ROM assets as a per-system NON-DESTRUCTIVE override list (the
// `*-assets` role config, applied to the ROM in memory at construct — the base file is never written). The
// STRUCTURE is unified here: a generic assetMenu renders each asset type (submenu → per-slot Export / Replace
// / Delete-for-kits / Remove-Override rows, a top Add… for kits) over an AssetCatalog (src/tracker), which
// supplies the base-ROM slot list (baseSlots) + the effective merge (effectiveAssets). The per-console FILE
// actions (Export/Replace — which own the .kit/.lsdpal/.png vs .rit/.chr/.rkit formats) stay below as the
// spec's callbacks. A new console adds an AssetCatalog + an AssetMenuSpec.
// Read + cache the base ROM bytes once per romPath (the menu rebuilds every render; the catalog re-parses on
// demand — cheap vs the file read). Shared by both consoles (romPath is unique per ROM).
const assetRomCache = new Map<string, Uint8Array | null>();
function assetRomBytes(be: HostBackend, romPath: string): Uint8Array | null {
  if (assetRomCache.has(romPath)) return assetRomCache.get(romPath) ?? null;
  const bytes = romPath ? be.readFile(romPath) : null;
  assetRomCache.set(romPath, bytes);
  return bytes;
}
// Drop a cached base ROM: the file on disk no longer matches what we parsed (Patch ROM in Place rewrote it).
// Without this the asset submenus keep listing the PRE-patch slots and Delete's base-slot test reads stale.
const invalidateAssetRom = (romPath: string): void => void assetRomCache.delete(romPath);

// A detected tracker cart whose embedded version this build has no layout for is DETECTED but not driveable
// (the Songs/Assets rows read that layout). We grey the submenu out as "(Unsupported Version)" rather than
// hiding it, so it's clear the cart IS a tracker we just don't support this version of. LSDj drives every
// version (no predicate); risa only the versions with a bundled layout.
function trackerVersionSupported(t: TrackerIntegration, be: HostBackend, romPath: string): boolean {
  if (!t.isVersionSupported) return true;
  const rom = assetRomBytes(be, romPath);
  return !rom || t.isVersionSupported(rom); // unreadable ROM -> can't disprove support; leave the submenu live
}

// The override list out of a system's `*-assets` role config (console-agnostic: the generic rows only read
// type/slot/name/erase; the file-actions read the typed list for their console-specific fields).
const overridesFor = (sys: SystemView, role: string): AssetOverride[] => readAssetOverrides(roleConfig(sys.roles, role));

// Persist an override list + rebuild so onConstruct re-patches the effective ROM. setRoleConfig merges at the
// top level, so this leaves any sibling key (BlipToaster's baked `settings`) alone.
function writeOverrides(ctx: MenuContext, sys: SystemView, role: string, overrides: AssetOverride[]): void {
  ctx.stores.project.systems.setRoleConfig(sys.id, role, { overrides });
  ctx.stores.project.systems.reloadSystem(sys.id);
}

function removeOverride(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView, kind: string, slot: number): void {
  writeOverrides(ctx, sys, spec.catalog.assetRole, overridesFor(sys, spec.catalog.assetRole).filter((o) => !(o.type === kind && o.slot === slot)));
}

// Non-destructively remove a kit from a slot: drop any existing kit override there, and — when the BASE ROM
// has a kit in that slot — record an `erase` override so construct empties it. (A slot present only via a
// replace override just reverts to base-empty once that override is dropped.)
function deleteAsset(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number): void {
  const bytes = assetRomBytes(ctx.stores.backend, sys.romPath);
  const baseHas = !!bytes && spec.catalog.baseSlots(bytes, type.kind).some((s) => s.slot === slot);
  const overrides = overridesFor(sys, spec.catalog.assetRole).filter((o) => !(o.type === type.kind && o.slot === slot));
  if (baseHas) overrides.push({ type: type.kind, slot, name: "", erase: true });
  writeOverrides(ctx, sys, spec.catalog.assetRole, overrides);
}

// Add an asset from disk into the first free effective slot (a replace override on an unused slot).
function addAsset(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView, type: AssetTypeInfo): void {
  const bytes = assetRomBytes(ctx.stores.backend, sys.romPath);
  if (!bytes) return;
  const used = new Set(effectiveAssets(spec.catalog.baseSlots(bytes, type.kind), overridesFor(sys, spec.catalog.assetRole), type.kind, type.noun).map((r) => r.slot));
  let slot = 0;
  while (slot < type.maxSlots && used.has(slot)) slot++;
  if (slot >= type.maxSlots) return; // all slots full
  spec.replaceAsset(hostOf(ctx), sys, type, slot);
}

// One asset item: Export / Replace, plus Select (where the console has a live slot), Delete (kits only) and
// Remove Override (when overridden).
//
// Every asset type renders those verbs INLINE — `[0] TR-606   <  Export...  >`, Left/Right picking and Enter
// running — rather than as a submenu per slot, which made patching any one of them a four-level descent.
//
// Two markers, and they mean different things: `*` is the LIVE slot (what the cart is set to), `~` is an
// overridden one (its contents replaced from disk). A slot can be both.
function assetRow(
  spec: AssetMenuSpec,
  ctx: MenuContext,
  sys: SystemView,
  type: AssetTypeInfo,
  row: AssetSlotRow,
  build: MenuBuildContext,
): MenuItem {
  const id = `${spec.id}-${type.kind}-${row.slot}`;
  const selected = spec.selectedSlot?.(hostOf(ctx), sys, type.kind, build) ?? null;
  const verbs: MenuAction[] = [];
  // Select leads, so landing on a row offers the thing the list is FOR; it keeps the menu open so a run of
  // themes can be auditioned without reopening it each time. Absent on the slot already live - there is
  // nothing to select - which also stops it being the default verb on exactly the row where it is a no-op.
  if (spec.selectSlot && selected !== null && selected !== row.slot) {
    verbs.push({
      id: `${id}-select`,
      label: "Select",
      keepOpen: true,
      onSelect: () => spec.selectSlot!(hostOf(ctx), sys, type.kind, row.slot, build),
    });
  }
  verbs.push(
    {
      id: `${id}-export`,
      label: "Export...",
      onSelect: () => spec.exportAsset(hostOf(ctx), sys, type, row.slot, row.name),
    },
    { id: `${id}-replace`, label: "Replace...", onSelect: () => spec.replaceAsset(hostOf(ctx), sys, type, row.slot) },
  );
  if (type.addable) verbs.push({ id: `${id}-delete`, label: "Delete", onSelect: () => deleteAsset(spec, ctx, sys, type, row.slot) });
  if (row.overridden) verbs.push({ id: `${id}-remove`, label: "Remove Override", onSelect: () => removeOverride(spec, ctx, sys, type.kind, row.slot) });
  const marks = `${selected === row.slot ? "*" : ""}${row.overridden ? "~" : ""}`;
  return actionCycler(id, `[${row.slot}] ${row.name}${marks ? ` ${marks}` : ""}`, verbs);
}

// Build a console's asset submenus (one per asset type); empty when the ROM can't be read (e.g. headless). A
// kit type leads with an "Add..." item (+ separator) and its rows get a Delete; other types are a fixed
// base-slot list (Export / Replace / Remove-Override only).
function assetMenu(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const bytes = assetRomBytes(ctx.stores.backend, sys.romPath);
  if (!bytes) return [];
  const overrides = overridesFor(sys, spec.catalog.assetRole);
  // A catalog may refine its types per-ROM (BlipToaster: single-kit on NROM, addable/16 on a banking cart).
  const types = spec.catalog.resolveTypes ? spec.catalog.resolveTypes(bytes) : spec.catalog.types;
  const build = buildContext(ctx);
  return types.map((type) => {
    const rows = effectiveAssets(spec.catalog.baseSlots(bytes, type.kind), overrides, type.kind, type.noun);
    const children: MenuItem[] = [];
    if (type.addable) {
      children.push(action(`${spec.id}-${type.kind}-add`, "Add...", () => addAsset(spec, ctx, sys, type)));
      children.push(separator(`${spec.id}-${type.kind}-add-sep`));
    }
    children.push(...rows.map((row) => assetRow(spec, ctx, sys, type, row, build)));
    return submenu(`${spec.id}-${type.kind}s`, type.title, children);
  });
}

// --- baking the overrides into the ROM ----------------------------------------------------------------
// The overrides are deliberately non-destructive, which leaves the project depending on asset files spread
// around the disk (a `.lsdprj` import is the sharpest case: every kit it brought in links back to that one
// file). These two rows make the cart self-contained by writing out the EFFECTIVE ROM, the exact image
// construct already hands the core: in place (and the now-redundant overrides leave the .rplg), or to a
// file of the user's choosing (the project untouched).

// The effective ROM, the overrides that couldn't be applied ("kit 5"), and whether the patcher RECOGNISED the
// image at all; null when the base can't be read. Reads the base FRESH rather than through assetRomBytes:
// this feeds a write, so it must not race a cache seeded before someone edited the ROM underneath us.
//
// `recognised` is reference identity, and that is exactly what it means: both patchers bail with `return
// baseBytes` when the image isn't their console's (a swapped-out file, an LSDj build too old to parse), while
// a recognised one always comes back as a fresh clone from rom.bytes(). That bail happens BEFORE the
// per-override loop, so it reports no skips - it is silently "applied nothing", which the caller must not
// mistake for success.
function bakeRom(
  spec: AssetMenuSpec,
  ctx: MenuContext,
  sys: SystemView,
): { bytes: Uint8Array; skipped: string[]; recognised: boolean } | null {
  const base = sys.romPath ? ctx.stores.backend.readFile(sys.romPath) : null;
  if (!base) return null;
  const skipped: string[] = [];
  const bytes = spec.catalog.applyRoleConfig(
    base,
    roleConfig(sys.roles, spec.catalog.assetRole),
    filesPngHost(ctx.stores.backend),
    (ov, message) => {
      skipped.push(`${ov.type} ${ov.slot}`);
      // The row label is what the overlay shows; the REASON ("cannot read /kits/moved.rkit") has nowhere to
      // go in one line of red text, and it is the half that says what to do about it. The role modules used
      // to log it themselves, from library code, whether or not anyone was listening - so it is logged here
      // instead, by the caller that asked to be told.
      console.log(`[${spec.id}-assets] skipped ${ov.type} slot ${ov.slot}: ${message}`);
    },
  );
  return { bytes, skipped, recognised: bytes !== base };
}

// Overwrite the base ROM with the effective image and drop the (now baked-in) overrides. Returns null on
// success, else the message the confirm overlay shows in red. This is the one irreversible write here, and
// clearing the list throws away links the user may still be able to repair, so it is STRICT: it aborts before
// writing anything unless the whole list genuinely made it into the image.
function patchRomInPlace(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): string | null {
  const baked = bakeRom(spec, ctx, sys);
  if (!baked) return "Could not read the ROM";
  // Nothing was applied: the ROM on disk isn't one this console's patcher can read. Baking would write the
  // base back over itself and then drop every override, so refuse instead.
  if (!baked.recognised) return `Could not patch ${basename(sys.romPath)} - unrecognised ROM image`;
  if (baked.skipped.length) return `Could not apply ${baked.skipped.join(", ")} - fix or remove first`;
  if (!ctx.stores.backend.writeFileAtomic(sys.romPath, baked.bytes)) return `Could not write ${basename(sys.romPath)}`;
  invalidateAssetRom(sys.romPath);
  // No reloadSystem: the effective ROM is byte-identical to what the core is already running, so a cold boot
  // would cost the playback position and buy nothing. setRoleConfig re-renders the menu + marks the project
  // dirty on its own (a feature role is pure TS - see SystemsStore.setRoleConfig).
  //
  // `settings` is cleared with the overrides, and for the same reason: it is now IN the file, so keeping the
  // pin would only re-write the bytes it already matches - and would go on overriding a later hand-edit of the
  // ROM the user would expect to take effect. Consoles without that key are unaffected.
  ctx.stores.project.systems.setRoleConfig(sys.id, spec.catalog.assetRole, { overrides: [], settings: {} });
  return null;
}

// Write the effective ROM to a picked file. Best-effort (unlike the in-place bake): what lands is what the
// core is playing right now - including the case where the patcher recognised nothing and that IS the base
// ROM. Nothing on disk or in the project is at risk, so it stays silent on failure like every other
// Export... row.
function exportPatchedRom(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): void {
  const defaultName = `${stem(sys.romPath)}-patched${extension(sys.romPath)}`;
  const opts = { title: "Export Patched ROM", patterns: ROM_PATTERNS, saving: true, defaultName };
  browseThen(hostOf(ctx), opts, (path) => {
    const baked = bakeRom(spec, ctx, sys);
    if (baked) ctx.stores.backend.writeFileAtomic(path, baked.bytes);
  });
}

// The two bake rows, greyed when there's nothing to bake. Both live at the tracker submenu's root (they're
// whole-ROM ops, not per-asset-type), below the asset submenus.
function romPatchRows(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const config = roleConfig(sys.roles, spec.catalog.assetRole);
  // The catalog decides what counts as a pending edit: for most consoles that is a non-empty override list, but
  // BlipToaster also pins the cart's baked settings, and a bake that ignored those would grey the rows out with
  // real changes waiting (and then write an image missing them).
  const none = !(spec.catalog.hasEdits ? spec.catalog.hasEdits(config) : readAssetOverrides(config).length > 0);
  return [
    separator(`${spec.id}-patch-sep`),
    {
      id: `${spec.id}-patch-rom`,
      label: "Patch ROM in Place",
      kind: "prompt" as const,
      keepOpen: true,
      disabled: none,
      prompt: {
        title: `Overwrite ${basename(sys.romPath)} with the patched ROM?`,
        hint: "Enter to patch  |  Esc to cancel",
        confirm: true,
        onConfirm: () => patchRomInPlace(spec, ctx, sys),
      },
    },
    action(`${spec.id}-export-patched-rom`, "Export Patched ROM...", () => exportPatchedRom(spec, ctx, sys), none),
  ];
}

// Per-keystroke filter for the render Filename prompt: block path separators / dodgy chars at entry (a
// space is allowed and folded to "_" by sanitizeName on confirm).
const isFilenameChar = (ch: string): boolean => /^[A-Za-z0-9 ._-]$/.test(ch);
// LSDj song names are 8 chars from its own font: uppercase letters, digits and space. Distinct from
// sanitizeName (`@retroplug/core/path`), which builds a FILENAME (lowercase + dots are fine there, not here). The
// prompt pairs this with casing:"upper", so the field already shows what will be stored.
const isSongNameChar = (ch: string): boolean => /^[A-Z0-9 ]$/.test(ch);
const toSongName = (s: string): string => s.toUpperCase().replace(/[^A-Z0-9 ]/g, "").trim().slice(0, 8);

// --- the shared Songs submenu (SongCatalog-driven) -------------------------------------------------
// LSDj + risa songs are both the BATTERY, not a ROM override: edits act on the live SRAM through the host's liveSongs
// group (readSram → byte-level catalog op → writeFileAtomic → loadSram cold-boot). One generic songMenu renders
// the structure over a SongCatalog — Load / Delete / reorder are its byte-ops; the console supplies the
// file-dialog actions (Export/Replace/Add, which own their formats). Reorder (Move Up/Down) rows show only
// when the catalog implements reorder — risa's positional records, or LSDj by swapping saved slots.
// The "Load..." row when loading would discard uncommitted work: a submenu whose children are the two real
// answers, with Esc/Back as cancel. The first child is an inert line naming the casualty, so the warning is
// visible without having to read the option labels - the same "say what is unsaved" convention the
// unsaved-changes prompt follows. (A submenu rather than PromptSpec because there are three answers and
// PromptSpec carries two.)
function loadGuard(
  spec: SongMenuSpec,
  ctx: MenuContext,
  sys: SystemView,
  sav: Uint8Array,
  workingLabel: string,
  song: { index: number; name: string },
  doLoad: () => void,
): MenuItem {
  const idp = `${spec.id}-song-${song.index}-load`;

  // Commit + load as ONE byte-level mutation, not two chained ones. mutateLiveSav cold-boots the core and
  // `loadSram` allocates a NEW system id, so a second call against the SystemView captured here would read
  // a dead id and silently do nothing - discarding the very work the user asked to save. One transform also
  // makes it atomic: either both land or the sav is untouched. Mirrors addLsdsngToSav's add-then-load.
  const saveAndLoad = (name?: string): boolean => {
    const ok = hostOf(ctx).liveSongs.mutate(sys, (bytes) => {
      const saved = spec.commitWorking?.(bytes, name);
      return saved ? spec.catalog.load(saved, song.index) : null;
    });
    if (ok) ctx.stores.project.recordSong(song.name);
    return ok;
  };

  const saveRow = ((): MenuItem => {
    // A working song OUTSIDE the battery (smsggdj) cannot be saved by a byte transform on the sav - it is
    // not in it. Save it the way the close prompt does (into the live battery, no reboot), then load the
    // picked song through the same request every Load goes through. Offered only when a dry run says the
    // cart can take it, and greyed with the real reason when not - this row used to say "no free slot"
    // on a cart with thirty.
    const catalog = spec.catalog;
    if (catalog.commitWorkingSong) {
      const systems = ctx.stores.project.systems;
      const image = systems.workingSongImage(sys.id);
      const dry = image ? catalog.commitWorkingSong(sav, image) : null;
      if (!dry || !dry.ok) {
        return infoRow(`${idp}-save-unavailable`, `Save Working Song & Load (${dry ? SONG_BLOCKED[dry.reason] : "nothing to save"})`);
      }
      return action(`${idp}-save`, "Save Working Song & Load", () => {
        void (async () => {
          const r = await saveWorkingSong(ctx.stores.backend, systems, sys).catch(() => null);
          if (!r || r.kind === "refused") {
            console.warn(`[songs] working song not saved${r ? `: ${r.reason}` : ""}; nothing loaded`);
            return;
          }
          ctx.stores.project.requestSong({ index: song.index, confirmed: true, systemId: sys.id });
          ctx.stores.project.settleSong();
        })();
      });
    }
    if (!spec.commitWorking || !(spec.canCommitWorking?.(sav) ?? true)) {
      // Not offerable: no such op, or the catalog can't take it (full). Say why rather than fail on click.
      return infoRow(`${idp}-save-unavailable`, "Save Working Song & Load (no free slot)");
    }
    if (!spec.workingNeedsName?.(sav)) {
      return action(`${idp}-save`, "Save Working Song & Load", () => void saveAndLoad());
    }
    return {
      id: `${idp}-save`,
      label: "Save Working Song & Load...",
      kind: "prompt",
      keepOpen: true,
      prompt: {
        title: "Save working song as:",
        initial: "UNTITLED",
        hint: "Enter to save + load  |  Esc to cancel",
        casing: "upper",
        filter: isSongNameChar,
        onConfirm: (value) => {
          const name = toSongName(value);
          if (!name) return "name required";
          return saveAndLoad(name) ? null : "couldn't save the working song";
        },
      },
    };
  })();

  return submenu(idp, "Load...", [
    infoRow(`${idp}-warn`, `"${workingLabel}" has unsaved changes`),
    separator(`${idp}-warn-sep`),
    saveRow,
    action(`${idp}-discard`, "Discard & Load", doLoad),
  ]);
}

function songMenu(spec: SongMenuSpec, ctx: MenuContext, sys: SystemView): MenuItem {
  const cat = spec.catalog;
  const bytes = ctx.stores.project.systems.readSram(sys.id);
  const songs = bytes ? cat.list(bytes) : [];
  const last = songs.length - 1;
  // Would loading ANY song discard uncommitted work? One question per menu build, not per row - it's a
  // property of the working song, not of the row you're pointing at. Loading the song you're already on
  // still overwrites working memory from the stored slot, so every row is guarded, including that one.
  const ram = cat.workingSongOutsideBattery ? (ctx.stores.project.systems.readRam(sys.id) ?? undefined) : undefined;
  // Asked of the working song as an IMAGE (songLoadWouldDiscard), not of live RAM: inside a reset's boot
  // window the cart holds nothing yet but the song being carried back is real, and a RAM-only check turned
  // every row into a plain action there - Move Up then cold-booted the cart and dropped the carry.
  const discards = bytes ? songLoadWouldDiscard(ctx.stores.project.systems, sys) : false;
  const workingLabel = (bytes ? cat.workingName(bytes, ram) : null) || "the working song";
  // Every row below rewrites the battery and cold-boots the cart. On a console whose working song lives
  // OUTSIDE the battery that boot destroys it, so Delete / Replace / Move are as destructive as Load -
  // the assumption that only Load could lose work was LSDj's and risa's, not a universal one. When it
  // holds, this is false and nothing below changes.
  const rebootDiscards = (cat.workingSongOutsideBattery ?? false) && discards;
  const rebootWarning = ` ${workingLabel} has unsaved changes and the cart will restart - they will be lost.`;

  const rows: MenuItem[] = songs.map((s, i) => {
    const name = s.name || `Song ${s.index}`;
    // A REQUEST to the store, settled right here when the cart can take it - which for LSDj, risa and a
    // running smsggdj is now, so the menu keeps its immediacy - and by the frame tick otherwise: an
    // smsggdj that is still booting would have its song erased by that boot a moment after the write,
    // and used to be cold-booted blank instead. The store picks the live path (straight into work RAM, no
    // `.sav` rewrite, no reboot) or the cold-boot spine per console, and records the song's Recent row
    // itself, by NAME, so the row is at the top before the user gets back there. `confirmed`: this row was
    // already guarded (loadGuard below) or had nothing to guard, so the store must not ask again.
    const doLoad = (): void => {
      ctx.stores.project.requestSong({ index: s.index, confirmed: true, systemId: sys.id });
      ctx.stores.project.settleSong();
    };
    const items: MenuItem[] = [
      // Clean working song: load outright - a confirm that fires when nothing would be lost is worse than
      // none, because it trains people to dismiss it. Dirty: a submenu of the two ways forward, where
      // backing out (Esc) IS the cancel. A submenu rather than a yes/no prompt because there are three
      // answers and PromptSpec only carries two.
      discards ? loadGuard(spec, ctx, sys, bytes!, workingLabel, s, doLoad) : action(`${spec.id}-song-${s.index}-load`, "Load...", doLoad),
      action(`${spec.id}-song-${s.index}-export`, "Export...", () => spec.exportSong(hostOf(ctx), sys, s.index, name)),
      // Replace overwrites a SAVED slot - the durable copy, gone with no undo - so it always confirms,
      // whatever the working song's state. Confirm first, then browse: the picker is the console's own
      // (it owns the file formats), and a cancel there simply does nothing.
      {
        id: `${spec.id}-song-${s.index}-replace`,
        label: "Replace...",
        kind: "prompt" as const,
        keepOpen: true,
        prompt: {
          title: `Replace saved song "${name}"?${rebootDiscards ? rebootWarning : ""}`,
          hint: "Enter to pick a file  |  Esc to cancel",
          confirm: true,
          onConfirm: () => {
            spec.replaceSong(hostOf(ctx), sys, s.index);
            return null;
          },
        },
      },
    ];
    if (cat.reorder) {
      // reorder takes LIST POSITIONS (index into the rendered list), not the row's `index` — they coincide
      // for a positional catalog (risa) but not for LSDj's sparse slot numbers.
      // Reordering normally gets no confirm, and shouldn't: it is cheap, reversible and repeated, so a
      // prompt on every nudge would be pure friction. When the reboot costs the working song it is
      // neither cheap nor reversible, so it gains one for exactly that case and no other.
      const move = (dir: "up" | "down", to: number, disabled: boolean): MenuItem => {
        const label = dir === "up" ? "Move Up" : "Move Down";
        const id = `${spec.id}-song-${s.index}-${dir}`;
        const run = (): void => {
          hostOf(ctx).liveSongs.mutate(sys, (sav) => cat.reorder!(sav, i, to));
        };
        if (disabled || !rebootDiscards) return action(id, label, run, disabled);
        return {
          id,
          label,
          kind: "prompt" as const,
          keepOpen: true,
          prompt: {
            title: `${label}?${rebootWarning}`,
            hint: "Enter to continue  |  Esc to cancel",
            confirm: true,
            onConfirm: () => {
              run();
              return null;
            },
          },
        };
      };
      items.push(move("up", i - 1, i === 0));
      items.push(move("down", i + 1, i === last));
    }
    items.push({
      id: `${spec.id}-song-${s.index}-delete`,
      label: "Delete",
      kind: "prompt" as const,
      keepOpen: true,
      prompt: {
        title: `Delete song "${name}"?${rebootDiscards ? rebootWarning : ""}`,
        hint: "Enter to delete  |  Esc to cancel",
        confirm: true,
        onConfirm: () => {
          hostOf(ctx).liveSongs.mutate(sys, (sav) => cat.delete(sav, s.index));
          return null;
        },
      },
    });
    return submenu(`${spec.id}-song-${s.index}`, `[${s.index}] ${name}`, items);
  });
  const body = rows.length ? rows : [infoRow(`${spec.id}-song-none`, "(no saved songs)")];
  // The live working song, surfaced as a synthetic top row when (and only when) it holds work no saved slot
  // has (`discards`, the same content-level question the load guard asks). That covers both a cart whose song
  // lives only in working memory and the far commoner "loaded a song and edited it" state; a working song
  // that merely matches a slot gets no row, because it is already listed AS that slot. It's not a catalog
  // index, so it gets its own actions (save / Export), never Load/Delete/reorder.
  const working = discards && bytes ? cat.workingSong?.(bytes) : null;
  const workingRows: MenuItem[] = [];
  if (working) {
    const wName = working.name || "Working Song";
    const wItems: MenuItem[] = [];
    // An UNLINKED working song names no slot, so saving it can only append - and when a saved song already
    // carries its name, that append is a second "ECOLISOL" next to the first, which is the duplicate this
    // whole row exists to avoid. Offer those same-named slots as explicit overwrite targets first. Advisory,
    // never automatic: 8-char names aren't unique and overwriting a saved song has no undo, so the menu lists
    // the candidates by slot and the user picks. (A LINKED song already knows its slot and skips all this.)
    if (!working.linked && cat.saveWorkingToSlot) {
      for (const t of workingSongTargets(cat, bytes!)) {
        wItems.push(
          action(`${spec.id}-song-working-save-${t.index}`, `Save Changes to [${t.index}] ${t.name}`, () => {
            hostOf(ctx).liveSongs.mutate(sys, (sav) => cat.saveWorkingToSlot!(sav, t.index));
          }),
        );
      }
    }
    // Name the save by what it does, since the row now shows in both states and the two differ in a way the
    // user cares about: a LINKED song overwrites the slot it came from, an unlinked one grows the catalog.
    if (spec.saveWorkingToCatalog)
      wItems.push(
        action(`${spec.id}-song-working-save`, working.linked ? "Save Changes" : "Save as New Song", () =>
          spec.saveWorkingToCatalog!(hostOf(ctx), sys),
        ),
      );
    if (spec.exportWorking)
      wItems.push(
        action(`${spec.id}-song-working-export`, "Export...", () => spec.exportWorking!(hostOf(ctx), sys, wName)),
      );
    // "(unsaved)" is what separates this row from the identically-named slot row below it when the song is
    // linked - without it the two read as two songs rather than one song and its uncommitted edits.
    workingRows.push(submenu(`${spec.id}-song-working`, `[working] ${wName} (unsaved)`, wItems), separator(`${spec.id}-song-working-sep`));
  }
  // Add reaches the battery too, so it reboots as well - the file picker it opens makes that easy to
  // forget. Confirm BEFORE browsing: cancelling in the picker then costs nothing, and a prompt after the
  // user has already chosen a file reads as a second, redundant question.
  const addRow = rebootDiscards
    ? {
        id: `${spec.id}-song-add`,
        label: "Add...",
        kind: "prompt" as const,
        keepOpen: true,
        prompt: {
          title: `Add a song?${rebootWarning}`,
          hint: "Enter to pick a file  |  Esc to cancel",
          confirm: true,
          onConfirm: () => {
            spec.addSong(hostOf(ctx), sys);
            return null;
          },
        },
      }
    : action(`${spec.id}-song-add`, "Add...", () => spec.addSong(hostOf(ctx), sys));
  return submenu(`${spec.id}-songs`, "Songs", [...workingRows, addRow, separator(`${spec.id}-song-add-sep`), ...body]);
}

// --- one tracker's instance submenu --------------------------------------------------------------------
/** What a contributed builder reads beyond the host: the project's systems and the shell's per-path base-ROM cache (a
 *  catalogue parses a ROM once per path rather than once per menu build, and a bake drops it when it rewrites one). */
function buildContext(ctx: MenuContext): MenuBuildContext {
  return {
    systems: ctx.systems,
    assetRom: (path) => assetRomBytes(ctx.stores.backend, path),
    romPatterns: ROM_PATTERNS,
  };
}

/** One tracker's instance-submenu children, composed from what its package contributes: the rows its `extras` declare
 *  (its sync knobs), the fragment its `extras.build` returns (the HD Player, BlipToaster's settings), the shared Songs
 *  menu over its `song` spec, its asset menus and then the whole-ROM bake rows. A cart contributes the specs it has, so
 *  an asset-only console (BlipToaster) builds no Songs menu and an asset-less one builds no asset rows. */
function trackerChildren(t: TrackerIntegration, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const ui = contributedTrackerUi(t.id);
  if (!ui) return [];
  const host = hostOf(ctx);
  const systems = ctx.stores.project.systems;
  return [
    ...roleRowItems(
      ui.extras?.rows ?? [],
      sys,
      (kind) => ctx.stores.registry.roleType(kind),
      (role, key, val) => systems.setRoleConfig(sys.id, role, { [key]: val }),
    ),
    ...(ui.extras?.build ? ui.extras.build(host, sys, buildContext(ctx)) : []),
    ...(ui.song ? [songMenu(ui.song, ctx, sys)] : []),
    ...(ui.asset ? [...assetMenu(ui.asset, ctx, sys), ...romPatchRows(ui.asset, ctx, sys)] : []),
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
    browseThen(hostOf(ctx), { title: "Load Project", patterns: LOAD_PATTERNS }, (p) => ctx.loadProject(p)),
  ));
  if (ctx.systems.length > 0) {
    // Save writes to the known path when there is one (else Save As covers it). Save As / Export browse
    // for a target; each store method already takes a resolved path.
    // Both go through saveProjectInteractive, which flushes each cart's battery first. Calling
    // project.save directly meant this row carried the unsaved-battery "*" in its own label and then
    // saved everything except the battery.
    if (project.currentPath()) items.push(action("proj-save", saveProjectLabel(ctx), () => void saveProjectInteractive(ctx.stores)));
    items.push(action("proj-saveas", "Save Project As...", () =>
      // Through the same save as every other route, so an unsaved work-RAM working song is saved into its
      // battery first - flushing and saving directly here skipped it, and the user believed they had saved.
      browseThen(
        hostOf(ctx),
        { title: "Save Project", patterns: PROJECT_PATTERNS, saving: true, defaultName: "project.rplg" },
        (p) => void saveProjectInteractive(ctx.stores, p),
      ),
    ));
    items.push(action("proj-export", "Export Zip...", () =>
      browseThen(
        hostOf(ctx),
        { title: "Export Zip", patterns: ZIP_PATTERNS, saving: true, defaultName: "project.rplg.zip" },
        (p) => project.export(p),
      ),
    ));
  }
  items.push(separator("proj-sep0"));
  // The project's own name - blank until the user types one here, and only then persisted in the .rplg.
  // Blank shows what the recents entry / titles fall back to: the name derived from the systems (the primary
  // cart's sav/rom stem). Clearing the field restores that fallback.
  if (ctx.systems.length > 0) {
    const own = project.name();
    const derived = project.displayName(); // == own when set; "" for an embedded cart (nothing to derive from)
    items.push({
      id: "proj-name",
      label: `Name: ${own || (derived ? `${derived} (auto)` : "(None)")}`,
      kind: "prompt",
      keepOpen: true,
      prompt: {
        title: "Project name:",
        hint: "Enter to set  |  empty to use the instance name  |  Esc to cancel",
        initial: own,
        onConfirm: (v: string) => {
          project.setName(v);
          return null;
        },
      },
    });
  }
  const audioRoutings = validAudioRoutings(ctx.systems);
  items.push(
    cycler("proj-layout", "Layout", LAYOUT_NAMES, Math.max(0, LAYOUT_VALUES.indexOf(ctx.settings.layout)), (n) => project.setLayout(LAYOUT_VALUES[n])),
    { id: "proj-zoom", label: `Zoom: ${ctx.settings.zoom === 0 ? "Default" : `${ctx.settings.zoom}x`}`, kind: "cycler", keepOpen: true, onSelect: () => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, 1)), onCycle: (dir) => project.setZoom(cycleInt(ctx.settings.zoom, 0, 6, dir)) },
    separator("proj-sep1"),
    cycler("proj-midi", "MIDI Routing", MIDI_ROUTING_NAMES, Math.max(0, MIDI_ROUTING_VALUES.indexOf(ctx.settings.midiRouting)), (n) => project.setMidiRouting(MIDI_ROUTING_VALUES[n])),
    // The split modes are single-system-only (and Pins NES-only), so the cycler offers a filtered list
    // and steps through THAT — not a prefix of the full table, since dropping Pins while keeping
    // Channels would otherwise shift the indices. The per-instance modes always stay; they're the
    // multi-system routes. Native is the authority and can't mis-route regardless; this is UX only.
    cycler(
      "proj-audio",
      "Audio Routing",
      audioRoutings.map((v) => AUDIO_ROUTING_NAMES[AUDIO_ROUTING_VALUES.indexOf(v)]),
      Math.max(0, audioRoutings.indexOf(ctx.settings.audioRouting)),
      (n) => project.setAudioRouting(audioRoutings[n]),
    ),
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
  // The active app-action map, filled with the contributed actions' defaults first (design 5.6): a profile with no
  // entry for a contributed id shows the action's default, and only an id the user wrote (a key or `[]`) is theirs.
  const contributed = ctx.actions ?? contributedActions();
  const actMap = withActionDefaults(ctx.bindings, contributed.map((entry) => entry.action))[actionsKey];

  // The bound-names half of a row label ("-" when unbound). Gamepad names are the raw SDL names and show
  // as-is; keyboard names get their display form, so a space reads "Space" rather than an invisible glyph.
  const shown = (names: string[] | undefined) =>
    names?.length ? (channel === "keyboard" ? names.map(keyDisplayName) : names).join(", ") : "-";

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
    label: `${btn}: ${shown(chMap[btn])}`,
    kind: "capture" as const,
    keepOpen: true,
    capture: {
      source: channel,
      onCapture: (name: string) => setBtn(btn, [name]),
      onClear: () => setBtn(btn, []),
    },
  }));

  // The app-action rows: the shell's three (Open Menu / Cycle Instances / Cycle Instances (Back)), then one per
  // contributed action in registration order. Same capture plumbing as a GB row, written into the actions section
  // under the action's own id (a dot included, which nothing here or in Menu parses). The "<label>: " head is kept so
  // Menu's "Press a key/button…" swap works.
  const actionRows: MenuItem[] = [
    ...APP_ACTION_ROWS.map((a) => ({ id: a.id, label: a.label })),
    ...contributed.map(({ action }) => ({ id: action.id, label: action.label })),
  ].map((a) => ({
    id: `${idp}-act-${a.id}`,
    label: `${a.label}: ${shown(actMap[a.id])}`,
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
    separator(`${idp}-sep-top`),
    ...captureRows,
    separator(`${idp}-sep-actions`),
    ...actionRows,
    separator(`${idp}-sep-reset`),
    // This channel only — the GB buttons AND the app actions — preserving the profile's OTHER channel.
    action(`${idp}-reset`, `Reset ${label} to Defaults`, () =>
      write((m) => ({ ...withChannel(m, defaultBindingMap()[channel]), [actionsKey]: defaultBindingMap()[actionsKey] })),
    ),
    separator(`${idp}-sep-mgmt`),
    { id: `${idp}-new`, label: "New Profile...", kind: "prompt", keepOpen: true, prompt: { title: "New profile name:", filter: isValidProfileChar, onConfirm: newProfile } },
    { id: `${idp}-rename`, label: "Rename...", kind: "prompt", keepOpen: true, prompt: { title: `Rename "${activeName}" to:`, initial: activeName, filter: isValidProfileChar, onConfirm: renameActive } },
    submenu(`${idp}-delete`, "Delete Profile", deleteChildren),
  ];
}

function settingsChildren(ctx: MenuContext): MenuItem[] {
  const userConfig = ctx.stores.userConfig;
  const sramIdx = Math.max(0, SRAM_AUTO_SAVES.indexOf(ctx.userConfig.sramAutoSave));
  // The persisted default render folder (System > Render seeds its Output Dir from this; "" = unset →
  // each cart falls back to its .sav / ROM folder). Set via a native folder picker; Clear returns to unset.
  const defaultDir = ctx.userConfig.render.outputDir;
  const renderDirItem = submenu("set-render-dir", dirLabel("Default Render Dir: ", defaultDir || "(unset)"), [
    action("set-render-dir-set", "Set...", () =>
      browseThen(
        hostOf(ctx),
        { title: "Default Render Dir", patterns: [], directory: true, startDir: defaultDir },
        (p) => userConfig.setRenderOutputDir(p),
      ),
    ),
    action("set-render-dir-clear", "Clear", () => userConfig.setRenderOutputDir(""), !defaultDir),
  ]);
  // The device pickers (Audio + MIDI) lead the menu: standalone-only, and their rows are the packages' - the shell owns
  // the two containers and the placements, nothing about a backend, a driver or a port name (docs/modular-graph-phase-
  // f5b-design.md section 6.5). A package that has no device to show (the plugin, whose rows are not drawn at all)
  // contributes none, and the container goes with them; the separator under them is emitted only when at least one is
  // present, so a DAW build doesn't open on a stray leading rule.
  const host = hostOf(ctx);
  // A device container: the package's rows inside the row the shell keeps, with the placement's open hook - opening it
  // is when a package re-reads what its rows show (the MIDI checklist's port listing), and the shell names neither the
  // package nor the device to carry it there.
  const container = (
    id: string,
    label: string,
    placement: "settings.audio" | "settings.midi",
  ): MenuItem[] => {
    if (!isStandalone()) return [];
    const rows = menuPlacement(placement, host, ctx.system ?? null, buildContext(ctx));
    if (rows.length === 0) return [];
    return [submenu(id, label, rows, menuPlacementOpen(placement, host))];
  };
  const deviceRows: MenuItem[] = [
    ...container("set-audio", "Audio", "settings.audio"),
    ...container("set-midi", "MIDI", "settings.midi"),
  ];
  return [
    ...deviceRows,
    ...(deviceRows.length ? [separator("set-sep-devices")] : []),
    // The input-binding editors sit right below the device pickers, with their own separator under them.
    submenu("set-keybindings", "Keyboard Bindings", bindingsChildren(ctx, "keyboard")),
    submenu("set-gamepad-bindings", "Gamepad Bindings", bindingsChildren(ctx, "gamepad")),
    separator("set-sep-bindings"),
    cycler("set-sram", "SRAM Auto-Save", SRAM_AUTO_SAVES.map((m) => SRAM_AUTO_SAVE_LABELS[m] ?? m), sramIdx, (n) => userConfig.setSramAutoSave(SRAM_AUTO_SAVES[n])),
    { id: "set-defzoom", label: `Default Zoom: ${ctx.userConfig.defaultZoom}x`, kind: "cycler", keepOpen: true, onSelect: () => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, 1)), onCycle: (dir) => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, dir)) },
    renderDirItem,
    // The host's OS file dialog (default) vs the in-app browser. On a host with no OS dialog it stays in-app.
    cycler("set-native-dialogs", "File Dialogs", ["In-App", "OS Native"], ctx.userConfig.useNativeFileDialogs ? 1 : 0, (n) => userConfig.setUseNativeFileDialogs(n === 1)),
    action("set-open-folder", "Open Settings Folder", () => openPath(ctx.stores.backend.configDir())),
  ];
}

function recentChildren(ctx: MenuContext): MenuItem[] {
  if (ctx.recent.length === 0) return [action("recent-none", "(No Recent Files)", () => {})];
  return ctx.recent.map((entry, i) => {
    // A single action row (no nested submenu): Enter loads the project WITH this row's song - or Locates it
    // when its file is gone; Del (onDelete) drops just this row, a hotkey the Menu drives off that field. A
    // project holds one row per song it has had loaded, so picking a row is "reopen that song". Label leads
    // with the song, then the project half: "SONG - project", ASCII " - " (the LVGL font has no emdash
    // glyph), matching the tracker window-title order. That half is whatever ProjectStore.recentName
    // resolved when the entry was recorded - the name the user gave the project, else its cart's
    // "<sav.ext> [<rom>]" identity, so a nameless entry reads "SONG - mysongs.sav [LSDj-v5.3.0]". A missing
    // entry is drawn yellow (warn) with a trailing " [!]".
    const base = entry.song ? `${entry.song} - ${entry.label}` : entry.label;
    const row: MenuItem = {
      id: `recent-${i}`,
      label: entry.missing ? `${base} [!]` : base,
      kind: "action",
      warn: entry.missing,
      onSelect: entry.missing
        ? () =>
            browseThen(
              hostOf(ctx),
              { title: "Locate Project", patterns: LOAD_PATTERNS },
              (p) => ctx.stores.recent.relink(entry.path, p),
            )
        : () => ctx.loadProject(entry.path, entry.song),
      onDelete: () => ctx.stores.recent.remove(entry.path, entry.song),
    };
    return row;
  });
}

// --- top-level builders -------------------------------------------------------------------------------

/** The ROM's own internal name is a full-file scan (LSDj title / risa "RISA V" marker); cache it per
 *  romPath so the per-render title composition doesn't re-scan a 512 KB ROM. A given path's ROM is stable
 *  (the asset caches make the same assumption). */
const cartRomNameCache = new Map<string, string | null>();
function cartRomName(backend: Pick<HostBackend, "readFile">, tracker: TrackerIntegration, romPath: string): string | null {
  const key = `${tracker.id}:${romPath}`;
  if (cartRomNameCache.has(key)) return cartRomNameCache.get(key) ?? null;
  const rom = romPath ? backend.readFile(romPath) : null;
  const name = rom ? tracker.romName(rom) : null;
  cartRomNameCache.set(key, name);
  return name;
}

/** The title subtitle for a tracker cart (LSDj / risa): the WORKING song name, then the ROM's OWN name (its
 *  internal title / version marker, NOT the on-disk filename) — e.g. "MYSONG - LSDj v9.4.2". Either piece is
 *  dropped when absent (no song loaded / an unversioned ROM). null for a non-tracker system, so the caller
 *  keeps its default project/rom-stem title. */
export function trackerCartLabel(backend: Pick<ControlPlaneBackend, "readFile" | "readSram" | "readRam">, sys: SystemView): string | null {
  const tracker = resolveTracker(sys.roles);
  if (!tracker) return null;
  const romName = cartRomName(backend, tracker, sys.romPath);
  const sram = backend.readSram(sys.id);
  // The window title is the most visible place the working song is named, and for smsggdj the only
  // source is the cart's own song_name in work RAM. An asset-only cart (BlipToaster) has no battery to name.
  const song = sram && tracker.songs ? tracker.songs.workingName(sram, backend.readRam(sys.id) ?? undefined) : null;
  const segs = [song, romName].filter((s): s is string => !!s);
  return segs.length ? segs.join(" - ") : null;
}

/** The standalone OS window title: "RetroPlug v<version> - <subtitle>". The subtitle is the tracker cart
 *  label ("<song> - <ROM name>") when given, else the project name; empty segments are dropped, so a
 *  nameless project shows just "RetroPlug v<version>". */
export function composeWindowTitle(version: string, project: string, cart?: string | null): string {
  const base = version ? `RetroPlug v${version}` : "RetroPlug";
  const subtitle = cart || project; // a tracker cart's "song - ROM name" supersedes the project name
  return subtitle ? `${base} - ${subtitle}` : base;
}

/** The instance-menu title. For a tracker cart: "RetroPlug v<version> - <song> - <ROM name>" (the cart's
 *  own name, not the filename). Otherwise "RetroPlug v<version> - <project> - <rom>", where ROM = the file
 *  stem, or the embedded ROM's OWN short name ("mGB" for the synth, its declaration's shortLabel), dropped when it
 *  equals the project name so it isn't shown twice. */
function instanceTitle(ctx: MenuContext, sys: SystemView): string {
  const base = ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug";
  const cart = trackerCartLabel(ctx.stores.backend, sys);
  if (cart) return `${base} - ${cart}`;
  const project = ctx.stores.project.displayName(); // the user's name, else the one derived from the systems
  const embed = sys.embeddedRom ? embeddedRomOf(sys.embeddedRom) : undefined;
  const rom = embed ? embed.shortLabel ?? embed.label : stem(sys.romPath);
  const segs = [base];
  if (project) segs.push(project);
  if (rom && rom !== project) segs.push(rom);
  return segs.join(" - ");
}

export function buildInstanceMenu(ctx: MenuContext): MenuTree {
  const sys = ctx.system!;
  const systems = ctx.stores.project.systems;
  const multi = ctx.systems.length > 1; // Replace / Remove / Link Group are peer-only rows
  // The tracker submenu (LSDj / risa / …) — one generic branch, gated on the marker role the ROM sniffed.
  const tracker = resolveTracker(sys.roles);
  // The tracker submenu, then the contributed device entries in product order (Launchpad, then N8 Pro): both drive
  // real hardware, so they belong beside the trackers rather than buried in Settings, and an entry whose device is
  // not here contributes nothing and hides itself.
  const block: MenuItem[] = [
    ...(tracker
      ? [
          trackerVersionSupported(tracker, ctx.stores.backend, sys.romPath)
            ? submenu(`inst-${tracker.id}`, tracker.label, trackerChildren(tracker, ctx, sys))
            : infoRow(`inst-${tracker.id}`, `${tracker.label} (Unsupported Version)`),
        ]
      : []),
    ...menuPlacement("instance.devices", hostOf(ctx), sys, buildContext(ctx)),
  ];
  const items: MenuItem[] = [
      // "Load…" is a project-level op (load the sibling project / new project from the ROM) — it never
      // swaps this instance. Swapping a single instance in place is "Replace Instance".
      action("inst-load", "Load...", () => runLoad(ctx)),
      action("inst-save", saveProjectLabel(ctx), () => void saveProjectInteractive(ctx.stores)),
      action("inst-new", "New Project", () => ctx.newProject()),
      submenu("inst-recent", "Recent", recentChildren(ctx)),
      // The tracker submenu and the contributed device entries share the block right under Recent, fenced by a
      // separator on each side (the one below here, and inst-sep-top). The fence appears when anything in it does.
      ...(block.length > 0 ? [separator("inst-sep-tracker"), ...block] : []),
      separator("inst-sep-top"),
      submenu("inst-system", "System", systemChildren(ctx, sys)),
      submenu("inst-project", "Project", projectChildren(ctx)),
      submenu("inst-settings", "Settings", settingsChildren(ctx)),
      // The host-transport readout (the MIDI Clock submenu) fences itself and sits BETWEEN the two groups, belonging to
      // neither: the midi package builds it at the `instance.clock` placement, and a host with no transport contributes
      // nothing.
      ...menuPlacement("instance.clock", hostOf(ctx), sys, buildContext(ctx)),
      separator("inst-sep1"),
      action("inst-add", "Add Instance", () => void ctx.stores.fileSelection.browseAdd(sys.id)),
      action("inst-dup", "Duplicate Instance", () => {
        const id = systems.duplicateSystem(sys.id);
        if (id != null) systems.inheritLinkGroup(id, sys.id);
      }),
      // Replace / Remove only make sense with a peer instance: hidden for a lone instance.
      ...(multi
        ? [
            songGuardedAction(ctx, sys, "inst-replace", "Replace Instance", () => void ctx.stores.fileSelection.browseReplace(sys.id)),
            songGuardedAction(ctx, sys, "inst-remove", "Remove Instance", () => systems.removeSystem(sys.id)),
          ]
        : []),
      // The instance footer: what a console contributes after Remove Instance and before Exit (the Game Boy's Link
      // Group, a peer-only row), so a console's own rows sit with the instance's rather than in its System submenu.
      ...menuPlacement("instance.footer", hostOf(ctx), sys, buildContext(ctx)),
      ...(isStandalone() ? [separator("inst-sep-exit"), action("inst-exit", "Exit RetroPlug", () => ctx.requestExit())] : []),
      // Deferred: About panel.
  ];
  // A duplicate id here would replace the row a user can see (the menu is keyed by id), and a hand-edited asset
  // override can produce one: the first row is kept, the later ones are reported, and the menu still renders.
  const unique = firstWinsMenuIds(items, `the ${sys.platform} instance menu`);
  return { title: instanceTitle(ctx, sys), items: unique };
}

export function buildStartMenu(ctx: MenuContext): MenuTree {
  const items: MenuItem[] = [
      submenu("start-recent", "Recent", recentChildren(ctx)),
      action("start-load", "Load...", () => runLoad(ctx)),
      // One row per embedded ROM the product's packages declare (mGB's synth today), its label from the declaration, so
      // the menu names no cart. The action's id keeps the embedded id, which is what a menu test keys on.
      ...embeddedRoms().map((embed) =>
        action(`start-${embed.id}`, `Load ${embed.label}`, () => ctx.stores.project.systems.loadEmbedded(embed.id)),
      ),
      // A device's start-menu block (the N8 Pro's today), in its own fenced block mirroring the instance menu's
      // tracker block: its streaming + card ops need no loaded system, so the cart can be configured - or a ROM
      // loaded onto it - before anything is opened in RetroPlug.
      ...menuPlacement("start", hostOf(ctx), null, buildContext(ctx)),
      separator("start-sep0"),
      submenu("start-project", "Project", projectChildren(ctx)),
      submenu("start-settings", "Settings", settingsChildren(ctx)),
      ...(isStandalone() ? [separator("start-sep-exit"), action("start-exit", "Exit RetroPlug", () => ctx.requestExit())] : []),
      // Deferred: About panel.
  ];
  const unique = firstWinsMenuIds(items, "the start menu");
  return { title: ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug", items: unique };
}

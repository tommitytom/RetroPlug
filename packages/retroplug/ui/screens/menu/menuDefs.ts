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
  COLOR_CORRECTION_VALUES,
  DMG_PALETTE_VALUES,
  type SameBoyModel,
  type SameBoyHighpass,
  type SameBoyColorCorrection,
  type SameBoyDmgPalette,
  type ConsoleRegion,
  type LsdjSyncMode,
} from "../../../src/settingsEnums";
import type { UserConfig } from "../../../src/userConfig";
import { SRAM_AUTO_SAVES, RENDER_SAMPLE_RATES, RENDER_ON_EXISTS } from "../../../src/userConfig";
import type { SplitMode } from "../../../src/render";
import { defaultBindingMap, type BindingMap } from "../../../src/bindingMap";
import { keyDisplayName } from "../../../src/keyCodes";
import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
import type { RecentView } from "../../../src/recentStore";
import { resolveSavPath, siblingPath, SAV_PATTERNS, isSavPath } from "../../../src/savPaths";
import { stem, dirname, basename, extension, joinPath, shortenMiddle } from "../../../src/pathUtil";
import { ROM_PATTERNS } from "../../../src/fileSelection";
import { LsdjRom, decodeLsdpal, encodeLsdpal } from "../../../src/lsdj/rom";
import { decompressSlot, encodeLsdsngRaw, savSongName, savSongVersion } from "../../../src/lsdjSav";
import {
  replaceSongInSav,
  saveWorkingToCatalog as lsdjSaveWorkingToCatalog,
  canSaveWorkingToCatalog,
} from "../../../src/lsdjSongOps";
import { activeSlot as lsdjActiveSlot } from "../../../src/lsdj/codec/sav";
import { importSongFiles } from "../../../src/lsdjSongImport";
import {
  songRecordBytes,
  replaceSongRecordInSav,
  addSongRecordToSav,
  workingSongRecord,
  saveWorkingToCatalog,
  canSaveWorkingToCatalog as canRisaSaveWorking,
} from "../../../src/risaSongOps";
import { RisaRom, serializeRit, parseRit, decodeThemeFromRom, isBankPopulated, bankToModel, KIT_BANK_SIZE } from "../../../src/risa/rom";
import { readOverrides as readRisaOverrides, type RisaAssetOverride } from "../../../src/risaAssetsRole";
import { readOverrides, applyOverridesToRom, type LsdjAssetOverride } from "../../../src/lsdjAssetsRole";
import { planLsdprjImport } from "../../../src/lsdjLsdprjImport";
import {
  resolveTracker,
  mutateLiveSav,
  effectiveAssets,
  readAssetOverrides,
  lsdjSongCatalog,
  risaSongCatalog,
  workingSongTargets,
  lsdjAssetCatalog,
  risaAssetCatalog,
  type SongCatalog,
  type AssetCatalog,
  type AssetSlotRow,
  type AssetOverride,
  type AssetTypeInfo,
  type TrackerIntegration,
} from "../../../src/tracker";
import type { HostBackend, ControlPlaneBackend } from "../../../src/backend";
import { openPath } from "../../lvgl/openPath";
import { startSystemRender, renderBaseName, validSplits, formatDuration } from "../../lvgl/render";
import { saveProjectInteractive } from "../../lvgl/saveProjectInteractive";
import { hasUnsavedChanges } from "../../../src/unsavedChanges";
import type { FileBrowserOpts } from "../../../src/backend";
import { hasAudioConfig, getAudioDraft, setAudioDraft, applyAudioDraft, audioDraftDirty, getAudioDrivers } from "./audioDraft";
import { hasMidiConfig, getMidiConfig, setMidiInput, setMidiOutput } from "./midiDevices";
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
  /** `song` (a recent row's) is loaded into the cart once the project lands - reopening that song, not just
   *  the project. Omitted (Load Project… / a file drop) leaves whatever song the project's sav carries. */
  loadProject: (path: string, song?: string) => void;
  loadRomAsProject: (romPath: string, explicitSav?: string) => void;
  /** Quit the standalone (unsaved-changes guarded). No-op in a DAW (the host owns the window). */
  requestExit: () => void;
  // Open the Songs "import from a .sav" picker (validate the source against the cart's console, then show a
  // checkbox list) — owned by useSongImport, wired from App like the project modals.
  beginSongImport: (sys: SystemView, source: Uint8Array) => void;
}

/** True only in a standalone host (the SDL handheld build or the DPF JACK standalone), which installs
 *  __rp_isStandalone; a DAW-hosted editor and the headless harness leave it undefined. Gates the "Exit"
 *  row — a DAW owns the plugin window, so it must not offer to quit. */
function isStandalone(): boolean {
  return (globalThis as { __rp_isStandalone?: boolean }).__rp_isStandalone === true;
}

// Standalone audio device config (the SDL host's sample-rate / block-size, for the Audio submenu). The
// cyclers edit a DRAFT (audioDraft.ts) — nothing is applied until the "Apply" row commits it via
// __rp_setAudioConfig (re-open device + persist). Absent in a DAW / the harness (hasAudioConfig() false).
const AUDIO_RATES = [22050, 32000, 44100, 48000];
const AUDIO_BLOCKS = [128, 256, 512, 1024, 2048, 4096];
// Output device channels: 2 = stereo mix; 4/6/8 open that many device channels so the project's Audio
// Routing (2-Ch/Inst, 1-Ch/Inst, Channels) fans real stems out to a multichannel interface. Labelled by
// pair count since routing works in stereo pairs.
const AUDIO_CHANNELS = [2, 4, 6, 8];
const AUDIO_CHANNEL_NAMES = ["Stereo", "4 (2 pairs)", "6 (3 pairs)", "8 (4 pairs)"];
function audioSettingsChildren(): MenuItem[] {
  const cfg = getAudioDraft() ?? { sampleRate: 48000, blockSize: 2048, outChannels: 2, driver: "Auto" };
  const rateIdx = Math.max(0, AUDIO_RATES.indexOf(cfg.sampleRate));
  const blockIdx = Math.max(0, AUDIO_BLOCKS.indexOf(cfg.blockSize));
  const chIdx = Math.max(0, AUDIO_CHANNELS.indexOf(cfg.outChannels));
  // The driver list ("Auto" + each compiled-in/available host API) is enumerated natively, so the picker shows
  // exactly what the build/runtime offers (PipeWire+ALSA on the handheld; +JACK on a -DRETROPLUG_SDL_JACK build).
  const drivers = getAudioDrivers();
  const driverIdx = Math.max(0, drivers.indexOf(cfg.driver));
  const dirty = audioDraftDirty();
  return [
    // The cyclers stage a pending value only — the label tracks the draft, but the live device is unchanged.
    cycler("audio-driver", "Driver", drivers, driverIdx, (n) => setAudioDraft({ driver: drivers[n] })),
    cycler("audio-rate", "Sample Rate", AUDIO_RATES.map((r) => `${r} Hz`), rateIdx, (n) => setAudioDraft({ sampleRate: AUDIO_RATES[n] })),
    cycler("audio-block", "Block Size", AUDIO_BLOCKS.map((b) => `${b}`), blockIdx, (n) => setAudioDraft({ blockSize: AUDIO_BLOCKS[n] })),
    cycler("audio-channels", "Out Channels", AUDIO_CHANNEL_NAMES, chIdx, (n) => setAudioDraft({ outChannels: AUDIO_CHANNELS[n] })),
    sep("audio-sep-apply"),
    // Commit the staged rate/block/channels to the device. Greyed (inert) until there's a pending change.
    action("audio-apply", "Apply", () => applyAudioDraft(), !dirty),
  ];
}

// Standalone-only MIDI device pickers (Settings > MIDI), gated on hasMidiConfig(). Unlike Audio, a pick
// applies immediately (setMidiInput/Output → the native host reconnects the port + persists). Input defaults
// to "All Devices" (every hardware input, the historical behavior); output to "None" (virtual port only). A
// saved device that isn't currently present is still shown, marked "(not connected)", and stays selected.
function deviceCyclerNames(devices: string[], selected: string, allLabel: string): { names: string[]; index: number } {
  const names = [allLabel, ...devices];
  if (selected === "") return { names, index: 0 };
  const i = devices.indexOf(selected);
  if (i >= 0) return { names, index: i + 1 };
  // Selected device not present: append it so the label still reflects the choice (re-applied on reconnect).
  names.push(`${selected} (not connected)`);
  return { names, index: names.length - 1 };
}

function midiSettingsChildren(): MenuItem[] {
  const cfg = getMidiConfig() ?? { inputs: [], outputs: [], selectedInput: "", selectedOutput: "" };
  const inp = deviceCyclerNames(cfg.inputs, cfg.selectedInput, "All Devices");
  const out = deviceCyclerNames(cfg.outputs, cfg.selectedOutput, "None");
  // Cycler index 0 = the default sentinel ("" selection); any other index maps back to the device name.
  const inName = (n: number) => (n === 0 ? "" : cfg.inputs[n - 1] ?? cfg.selectedInput);
  const outName = (n: number) => (n === 0 ? "" : cfg.outputs[n - 1] ?? cfg.selectedOutput);
  return [
    cycler("midi-input", "Input Device", inp.names, inp.index, (n) => setMidiInput(inName(n))),
    cycler("midi-output", "Output Device", out.names, out.index, (n) => setMidiOutput(outName(n))),
  ];
}

// --- name tables (mirror the native enums, ported from legacy menuDefs.tsx) ---------------------------
const MIDI_ROUTING_NAMES = ["Send to All", "4 Ch / Inst", "1 Ch / Inst", "Ch -> Inst"];
// Index 3 (ChannelSplit) fans one Game Boy's 4 channels across the 8 outputs; offered only for a
// single system (see projectChildren) — native gates it to systemCount()==1 too.
const AUDIO_ROUTING_NAMES = ["Stereo", "2 Ch / Inst", "1 Ch / Inst", "Channels (1 GB)"];
const LAYOUT_NAMES = ["Auto", "Row", "Column", "Grid"];
const MODEL_NAMES = ["Auto", "DMG-B", "MGB", "SGB", "SGB PAL", "SGB2", "CGB-0", "CGB-A", "CGB-B", "CGB-C", "CGB-D", "CGB-E", "AGB", "GBP"];
const HIGHPASS_NAMES = ["Off", "Accurate", "DC-Block"];
// SameBoy display knobs (the "sameboy" role). Index == the value tuple in settingsEnums.
const COLOR_CORRECTION_NAMES = ["Off", "Correct Curves", "Balanced", "Boost Contrast", "Reduce Contrast", "Low Contrast", "Accurate"];
const DMG_PALETTE_NAMES = ["Grey", "DMG", "MGB", "GBL"];
// Light temperature is a continuous -1..1 in the core; the menu has cyclers, not sliders, so offer it
// as 11 steps over the full range. Upstream's own slider is 21 steps, which is a lot of key presses
// for a tint. Stored as the double, so a value that came from elsewhere still round-trips through
// nearestIndex() to the closest row.
const LIGHT_TEMP_STEPS = [-1, -0.8, -0.6, -0.4, -0.2, 0, 0.2, 0.4, 0.6, 0.8, 1];
const LIGHT_TEMP_NAMES = ["Cool 100%", "Cool 80%", "Cool 60%", "Cool 40%", "Cool 20%", "Neutral", "Warm 20%", "Warm 40%", "Warm 60%", "Warm 80%", "Warm 100%"];

// The models that render in DMG mode, i.e. the ones the DMG palette applies to. Mirrors the core's own
// split: GB_is_cgb is `model >= GB_MODEL_CGB_0`, and everything below that (DMG, the Game Boy Pocket,
// and the three Super Game Boys) takes its colours from GB_update_dmg_palette instead of the CGB
// palette RAM. `auto` is NOT one of them - RetroPlug resolves it to CGB-C (toSameBoyModel), so it gets
// the CGB rows. Colour correction and light temperature are shown on exactly the complement.
const DMG_MODELS: readonly SameBoyModel[] = ["dmgB", "mgb", "sgb", "sgbPal", "sgb2"];
const isDmgModel = (m: SameBoyModel): boolean => DMG_MODELS.includes(m);
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
const SRAM_PATTERNS = SAV_PATTERNS; // .sav / .srm — battery saves (some NES/risa carts use .srm)

/** Wrap `current` within [min, max]: +1 past max → min, -1 below min → max. */
function cycleInt(current: number, min: number, max: number, dir: 1 | -1): number {
  if (dir > 0) return current >= max ? min : current + 1;
  return current <= min ? max : current - 1;
}

const SPLIT_LABELS: Record<SplitMode, string> = { mix: "Mix", channels: "Channels", pins: "Pins" };

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

/** "Save Project", with a trailing " *" when the project or any battery SRAM is unsaved (the same signal the
 *  close guard asks). The star is the file's established "modified" marker (cf. the asset-override rows). */
function saveProjectLabel(ctx: MenuContext): string {
  return `Save Project${hasUnsavedChanges(ctx.stores.backend, ctx.stores.project) ? " *" : ""}`;
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

/** The SameBoy core-role config for a system (model / highpass / link group / fast boot + the display
 *  group), with defaults. Every display default matches the core's own, so a project saved before those
 *  knobs existed reads back as the appearance it was saved with. */
function sameboyConfig(sys: SystemView): {
  model: SameBoyModel;
  highpass: SameBoyHighpass;
  linkGroupId: number;
  fastBoot: boolean;
  colorCorrection: SameBoyColorCorrection;
  dmgPalette: SameBoyDmgPalette;
  lightTemperature: number;
} {
  const c = (sys.roles.find((r) => r.kind === "sameboy")?.config ?? {}) as Record<string, unknown>;
  return {
    model: typeof c.model === "string" ? (c.model as SameBoyModel) : "cgbC",
    highpass: typeof c.highpass === "string" ? (c.highpass as SameBoyHighpass) : "accurate",
    linkGroupId: typeof c.linkGroupId === "number" ? c.linkGroupId : 0,
    fastBoot: c.fastBoot !== false,
    colorCorrection: typeof c.colorCorrection === "string" ? (c.colorCorrection as SameBoyColorCorrection) : "disabled",
    dmgPalette: typeof c.dmgPalette === "string" ? (c.dmgPalette as SameBoyDmgPalette) : "grey",
    lightTemperature: typeof c.lightTemperature === "number" ? c.lightTemperature : 0,
  };
}

// APU flush-window latency presets (ms) for the NES "APU Latency" cycler. The role config accepts any
// value in [0.25, 6.0] (clampedNumber); the menu just offers a few sensible steps. 1.4ms ≈ the default.
const APU_LATENCY_MS = [0.5, 1.0, 1.4, 3.0, 5.0];
const APU_LATENCY_NAMES = ["0.5 ms", "1.0 ms", "1.4 ms", "3.0 ms", "5.0 ms"];

/** Index of the preset in `presets` nearest `v`, so a cycler over a continuous value still shows the
 *  current setting even when it's off-grid (a project written by an older build, or by hand). */
function nearestIndex(presets: readonly number[], v: number): number {
  let best = 0;
  for (let i = 1; i < presets.length; i++) {
    if (Math.abs(presets[i] - v) < Math.abs(presets[best] - v)) best = i;
  }
  return best;
}

/** The Mesen core-role config, with defaults. The role attaches to any Mesen system but its knobs are
 *  per-platform, so the menu gates each group on `platform`: region / removeSpriteLimit / apuLatencyMs
 *  on "nes", enableFm on "sms" and "gg". */
function mesenConfig(sys: SystemView): {
  region: ConsoleRegion;
  removeSpriteLimit: boolean;
  apuLatencyMs: number;
  enableFm: boolean;
} {
  const c = (sys.roles.find((r) => r.kind === "mesen")?.config ?? {}) as Record<string, unknown>;
  return {
    region: typeof c.region === "string" ? (c.region as ConsoleRegion) : "auto",
    removeSpriteLimit: c.removeSpriteLimit === true,
    apuLatencyMs: typeof c.apuLatencyMs === "number" ? c.apuLatencyMs : 1.4,
    enableFm: c.enableFm !== false, // default ON, matching the schema + MesenSmsConfig
  };
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
        browseThen(ctx, { title: "Output Dir", patterns: [], directory: true, startDir: effectiveDir }, (p) =>
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
    sep("sys-render-sep-opts"),
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
    sep("sys-render-sep"),
    // Render: no dialog. Writes to <dir>/<name>.wav (the render lib trims .wav to a prefix for splits).
    action(
      "sys-render-go",
      "Render",
      () =>
        void startSystemRender(
          ctx.stores.backend,
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
  // menu; the Render submenu (a WAV background job) sits right below it, then a separator.
  const items: MenuItem[] = [action("sys-reset", "Reset", () => void systems.reset(sys.id))];
  if (sys.romPath) items.push(renderSubmenu(ctx, sys));
  items.push(
    sep("sys-sep-reset"),
    cycler("sys-reload", "Reload on ROM Change", OFF_ON, sys.settings.reloadOnRomChange ? 1 : 0, (n) =>
      systems.setReloadOnRomChange(sys.id, n === 1),
    ),
  );
  // SameBoy-only core knobs.
  if (sys.core === "sameboy") {
    const cfg = sameboyConfig(sys);
    items.push(
      cycler("sys-model", "Model", MODEL_NAMES, Math.max(0, MODEL_VALUES.indexOf(cfg.model)), (n) => systems.setRoleConfig(sys.id, "sameboy", { model: MODEL_VALUES[n] })),
      cycler("sys-highpass", "Highpass", HIGHPASS_NAMES, Math.max(0, HIGHPASS_VALUES.indexOf(cfg.highpass)), (n) => systems.setRoleConfig(sys.id, "sameboy", { highpass: HIGHPASS_VALUES[n] })),
      cycler("sys-fastboot", "Fast Boot", OFF_ON, cfg.fastBoot ? 1 : 0, (n) => systems.setRoleConfig(sys.id, "sameboy", { fastBoot: n === 1 })),
    );
    // Display. Live — the core applies these to the next rendered frame, no restart. Each row is shown
    // only on the models where the core will actually use it: it gates colour correction and light
    // temperature on GB_is_cgb and the DMG palette on the negation, so exactly one of the two groups
    // applies to any given model. See isDmgModel.
    items.push(sep("sys-sep-display"));
    if (isDmgModel(cfg.model)) {
      items.push(
        cycler("sys-dmg-palette", "DMG Palette", DMG_PALETTE_NAMES, Math.max(0, DMG_PALETTE_VALUES.indexOf(cfg.dmgPalette)), (n) =>
          systems.setRoleConfig(sys.id, "sameboy", { dmgPalette: DMG_PALETTE_VALUES[n] }),
        ),
      );
    } else {
      items.push(
        cycler("sys-color-correction", "Color Correction", COLOR_CORRECTION_NAMES, Math.max(0, COLOR_CORRECTION_VALUES.indexOf(cfg.colorCorrection)), (n) =>
          systems.setRoleConfig(sys.id, "sameboy", { colorCorrection: COLOR_CORRECTION_VALUES[n] }),
        ),
        cycler("sys-light-temp", "Light Temp", LIGHT_TEMP_NAMES, nearestIndex(LIGHT_TEMP_STEPS, cfg.lightTemperature), (n) =>
          systems.setRoleConfig(sys.id, "sameboy", { lightTemperature: LIGHT_TEMP_STEPS[n] }),
        ),
      );
    }
  }
  // NES-only core knobs (the "mesen" role also attaches to GBA, so gate on platform, not core).
  if (sys.platform === "nes") {
    const cfg = mesenConfig(sys);
    items.push(
      cycler("sys-nes-region", "Region", REGION_NAMES, Math.max(0, REGION_VALUES.indexOf(cfg.region)), (n) => systems.setRoleConfig(sys.id, "mesen", { region: REGION_VALUES[n] })),
      cycler("sys-nes-spritelimit", "Remove Sprite Limit", OFF_ON, cfg.removeSpriteLimit ? 1 : 0, (n) =>
        systems.setRoleConfig(sys.id, "mesen", { removeSpriteLimit: n === 1 }),
      ),
      cycler("sys-nes-apu-latency", "APU Latency", APU_LATENCY_NAMES, nearestIndex(APU_LATENCY_MS, cfg.apuLatencyMs), (n) =>
        systems.setRoleConfig(sys.id, "mesen", { apuLatencyMs: APU_LATENCY_MS[n] }),
      ),
    );
  }
  // SMS/GG-only core knobs (same "mesen" role, gated on platform for the same reason as above).
  // FM Audio is applied at construct, so flipping it reboots the core (configureSms runs before LoadRom).
  if (sys.platform === "sms" || sys.platform === "gg") {
    const cfg = mesenConfig(sys);
    items.push(
      cycler("sys-sms-fm", "FM Audio", OFF_ON, cfg.enableFm ? 1 : 0, (n) =>
        systems.setRoleConfig(sys.id, "mesen", { enableFm: n === 1 }),
      ),
    );
  }
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
  items.push(sep("sys-sep-state"));
  // Swap the ROM in place but keep the running battery SRAM (e.g. an LSDj version bump that keeps the song).
  // ROM-only browser; distinct from "Replace Instance", which cold-boots a fresh sav. Sits just above the
  // New/Load SRAM rows.
  items.push(action("sys-swaprom", "Swap ROM (Preserve SRAM)...", () => void ctx.stores.fileSelection.browseSwap(sys.id)));
  if (sys.romPath && !noSave)
    items.push(
      action("sys-newsram", "New SRAM...", () =>
        browseThen(
          ctx,
          { title: "New SRAM", patterns: SRAM_PATTERNS, saving: true, defaultName: sramName },
          (p) => void systems.newSramAs(sys.id, p),
        ),
      ),
    );
  items.push(
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

/** The LSDj sync submenu — Mode + Tempo Divisor + Auto Start cyclers. Shown only for a system carrying an lsdj-sync
 *  role (a sniffed LSDj cart). Both edits re-push the DSP kernel structure (setRoleConfig → markDirty →
 *  syncDspFromStore), so they apply to the running behaviour on the next block — no dedicated RPC. */
// --- shared tracker asset submenus (kits / palettes-or-themes / fonts) --------------------------------
// LSDj + risa both expose replaceable ROM assets as a per-system NON-DESTRUCTIVE override list (the
// `*-assets` role config, applied to the ROM in memory at construct — the base file is never written). The
// STRUCTURE is unified here: a generic assetMenu renders each asset type (submenu → per-slot Export / Replace
// / Delete-for-kits / Remove-Override rows, a top Add… for kits) over an AssetCatalog (src/tracker), which
// supplies the base-ROM slot list (baseSlots) + the effective merge (effectiveAssets). The per-console FILE
// actions (Export/Replace — which own the .kit/.lsdpal/.png vs .rit/.chr/.rkit formats) stay below as the
// spec's callbacks. A new console adds an AssetCatalog + an AssetMenuSpec.
interface AssetMenuSpec {
  id: string; // row-id prefix + submenu-id prefix, e.g. "lsdj" / "risa"
  catalog: AssetCatalog;
  exportAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number, label: string): void;
  replaceAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number): void;
}

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

// The override list off a system's `*-assets` role config (console-agnostic — the generic rows only read
// type/slot/name/erase; the file-actions read the typed list for their console-specific fields).
const overridesFor = (sys: SystemView, role: string): AssetOverride[] =>
  readAssetOverrides(sys.roles.find((r) => r.kind === role)?.config);

// Persist an override list + rebuild so onConstruct re-patches the effective ROM.
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
  spec.replaceAsset(ctx, sys, type, slot);
}

// One asset item: Export / Replace, plus Delete (kits only) + Remove Override (when overridden).
function assetRow(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, row: AssetSlotRow): MenuItem {
  const id = `${spec.id}-${type.kind}-${row.slot}`;
  const items: MenuItem[] = [
    action(`${id}-export`, "Export...", () => spec.exportAsset(ctx, sys, type, row.slot, row.name)),
    action(`${id}-replace`, "Replace from Disk...", () => spec.replaceAsset(ctx, sys, type, row.slot)),
  ];
  if (type.addable) items.push(action(`${id}-delete`, "Delete", () => deleteAsset(spec, ctx, sys, type, row.slot)));
  if (row.overridden) items.push(action(`${id}-remove`, "Remove Override", () => removeOverride(spec, ctx, sys, type.kind, row.slot)));
  return submenu(id, `[${row.slot}] ${row.name}${row.overridden ? " *" : ""}`, items);
}

// Build a console's asset submenus (one per asset type); empty when the ROM can't be read (e.g. headless). A
// kit type leads with an "Add..." item (+ separator) and its rows get a Delete; other types are a fixed
// base-slot list (Export / Replace / Remove-Override only).
function assetMenu(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const bytes = assetRomBytes(ctx.stores.backend, sys.romPath);
  if (!bytes) return [];
  const overrides = overridesFor(sys, spec.catalog.assetRole);
  return spec.catalog.types.map((type) => {
    const rows = effectiveAssets(spec.catalog.baseSlots(bytes, type.kind), overrides, type.kind, type.noun);
    const children: MenuItem[] = [];
    if (type.addable) {
      children.push(action(`${spec.id}-${type.kind}-add`, "Add...", () => addAsset(spec, ctx, sys, type)));
      children.push(sep(`${spec.id}-${type.kind}-add-sep`));
    }
    children.push(...rows.map((row) => assetRow(spec, ctx, sys, type, row)));
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
  const bytes = spec.catalog.applyOverrides(base, overridesFor(sys, spec.catalog.assetRole), ctx.stores.backend, (ov) =>
    skipped.push(`${ov.type} ${ov.slot}`),
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
  ctx.stores.project.systems.setRoleConfig(sys.id, spec.catalog.assetRole, { overrides: [] });
  return null;
}

// Write the effective ROM to a picked file. Best-effort (unlike the in-place bake): what lands is what the
// core is playing right now - including the case where the patcher recognised nothing and that IS the base
// ROM. Nothing on disk or in the project is at risk, so it stays silent on failure like every other
// Export... row.
function exportPatchedRom(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): void {
  const defaultName = `${stem(sys.romPath)}-patched${extension(sys.romPath)}`;
  browseThen(ctx, { title: "Export Patched ROM", patterns: ROM_PATTERNS, saving: true, defaultName }, (path) => {
    const baked = bakeRom(spec, ctx, sys);
    if (baked) ctx.stores.backend.writeFileAtomic(path, baked.bytes);
  });
}

// The two bake rows, greyed when there's nothing to bake. Both live at the tracker submenu's root (they're
// whole-ROM ops, not per-asset-type), below the asset submenus.
function romPatchRows(spec: AssetMenuSpec, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const none = overridesFor(sys, spec.catalog.assetRole).length === 0;
  return [
    sep(`${spec.id}-patch-sep`),
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

// A safe filename fragment (mirrors the CLI's sanitize).
const sanitizeName = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "asset";
// Per-keystroke filter for the render Filename prompt: block path separators / dodgy chars at entry (a
// space is allowed and folded to "_" by sanitizeName on confirm).
const isFilenameChar = (ch: string): boolean => /^[A-Za-z0-9 ._-]$/.test(ch);
// LSDj song names are 8 chars from its own font: uppercase letters, digits and space. Distinct from
// sanitizeName above, which builds a FILENAME (lowercase + dots are fine there, not here). The prompt
// pairs this with casing:"upper", so the field already shows what will be stored.
const isSongNameChar = (ch: string): boolean => /^[A-Z0-9 ]$/.test(ch);
const toSongName = (s: string): string => s.toUpperCase().replace(/[^A-Z0-9 ]/g, "").trim().slice(0, 8);
const readLsdjRom = (be: HostBackend, romPath: string): LsdjRom | null => {
  const bytes = romPath ? be.readFile(romPath) : null;
  if (!bytes) return null;
  const rom = LsdjRom.fromBytes(bytes);
  return rom.isLsdj ? rom : null;
};

// --- LSDj asset file actions (Export/Replace own the .kit/.lsdpal/.png formats) -----------------------
const lsdjOverrides = (sys: SystemView): LsdjAssetOverride[] =>
  readOverrides(sys.roles.find((r) => r.kind === "lsdj-assets")?.config);

// Export asset `type`/`slot` to a picked file: the override bytes if replaced (already the file format), else
// the base ROM's asset read straight out via the pure-TS module.
function exportAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number, label: string): void {
  const be = ctx.stores.backend;
  const kind = type.kind;
  const ov = lsdjOverrides(sys).find((o) => o.type === kind && o.slot === slot);
  const defaultName = `${sanitizeName(label)}${type.ext}`;
  browseThen(ctx, { title: `Export ${kind} ${slot}`, patterns: type.patterns, saving: true, defaultName }, (path) => {
    // An overridden slot's current asset comes from the override (a palette re-encodes its inline colours;
    // a kit/font copies its linked file); otherwise read it from the base ROM.
    let bytes: Uint8Array | null = null;
    if (ov) bytes = ov.type === "palette" && ov.colorSets ? encodeLsdpal(ov.name ?? "", ov.colorSets) : ov.path ? be.readFile(ov.path) : null;
    if (!bytes) {
      const rom = readLsdjRom(be, sys.romPath);
      if (!rom) return;
      if (kind === "kit") bytes = rom.exportKitFile(slot);
      else if (kind === "palette") bytes = rom.exportPaletteFile(slot);
      else {
        const img = rom.exportFontImage(slot, true); // include the extended gfx tiles (64×120) — lossless
        bytes = be.pngEncode(img.width, img.height, img.rgba);
      }
    }
    if (bytes && bytes.length) be.writeFileAtomic(path, bytes);
  });
}

// Replace asset `type`/`slot` from a picked file: validate by trial-applying to the base ROM (import* throws
// on a bad file), record the override (kit/font by PATH, palette INLINE) in role config, and reload so it
// takes effect. NON-DESTRUCTIVE — the base .gb is never written.
function replaceAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number): void {
  const be = ctx.stores.backend;
  const kind = type.kind;
  browseThen(ctx, { title: `Replace ${kind} ${slot}`, patterns: type.patterns }, (path) => {
    const data = be.readFile(path);
    if (!data) return;
    const rom = readLsdjRom(be, sys.romPath);
    if (!rom) return;
    // The new override: PALETTES are stored inline as structured colours (never a file link); KITS/FONTS
    // link to the file on disk by path. Validate by trial-applying to the base ROM (import* throws on a
    // bad file / out-of-range slot) — leaving the real ROM untouched.
    let entry: LsdjAssetOverride;
    try {
      if (kind === "kit") {
        rom.importKitFile(slot, data);
        entry = { type: "kit", slot, name: rom.kit(slot).name() || stem(path), path };
      } else if (kind === "palette") {
        const decoded = decodeLsdpal(data);
        if (!decoded) return; // not a valid .lsdpal
        rom.importPaletteFile(slot, data); // validates the slot is in range on this ROM
        entry = { type: "palette", slot, name: decoded.name || stem(path), colorSets: decoded.colorSets };
      } else {
        const img = be.pngDecode(data);
        if (!img) return; // not a decodable PNG
        rom.importFontImage(slot, img);
        entry = { type: "font", slot, name: stem(path), path };
      }
    } catch {
      return; // invalid asset file (wrong size / bad image) → leave the ROM untouched
    }
    writeOverrides(ctx, sys, "lsdj-assets", [...lsdjOverrides(sys).filter((o) => !(o.type === kind && o.slot === slot)), entry]);
  });
}

// --- risa asset file actions (Export/Replace own the .rit/.chr/.rkit formats) -------------------------
// THEMES are palette indices stored INLINE (readable JSON `.rit`, no file); FONTS LINK a `.chr` bank by path;
// KITS LINK a pre-built 8 KB `.rkit` DMC bank by path (compilation is offline — the plugin can't reach
// compileDmc — so a kit override just splices a ready-made bank, as risa's Export produces).
const risaAssetOverrides = (sys: SystemView): RisaAssetOverride[] =>
  readRisaOverrides(sys.roles.find((r) => r.kind === "risa-assets")?.config);

const readRisaRomFor = (be: HostBackend, romPath: string): RisaRom | null => {
  const bytes = romPath ? be.readFile(romPath) : null;
  if (!bytes) return null;
  const rom = RisaRom.fromBytes(bytes);
  return rom.isRisa ? rom : null;
};

// Export theme/font/kit `slot` to a picked file: the override's data if replaced, else the base ROM's asset.
function exportRisaAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number, label: string): void {
  const be = ctx.stores.backend;
  const kind = type.kind;
  const ov = risaAssetOverrides(sys).find((o) => o.type === kind && o.slot === slot);
  const defaultName = `${sanitizeName(label)}${type.ext}`;
  browseThen(ctx, { title: `Export ${kind} ${slot}`, patterns: type.patterns, saving: true, defaultName }, (path) => {
    let bytes: Uint8Array | null = null;
    if (kind === "theme") {
      // The theme comes from the inline override, or the base ROM decoded; emit a .rit (readable JSON).
      let theme = ov?.theme ?? null;
      if (!theme) {
        const t = readRisaRomFor(be, sys.romPath)?.getTheme(slot);
        if (t) theme = decodeThemeFromRom(t.recordBytes, t.nameBytes);
      }
      if (theme) bytes = new TextEncoder().encode(JSON.stringify(serializeRit(theme), null, 2) + "\n");
    } else if (kind === "kit") {
      // The linked bank if overridden, else the base ROM's 8 KB DMC bank — either is a ready-to-link .rkit.
      bytes = ov?.path ? be.readFile(ov.path) : (readRisaRomFor(be, sys.romPath)?.getKitBank(slot) ?? null);
    } else {
      bytes = ov?.path ? be.readFile(ov.path) : (readRisaRomFor(be, sys.romPath)?.getChrFontSlot(slot) ?? null);
    }
    if (bytes && bytes.length) be.writeFileAtomic(path, bytes);
  });
}

// Replace theme/font/kit `slot` from a picked file: validate it, record the override (theme INLINE / font +
// kit by PATH) in role config, and reload so it takes effect. NON-DESTRUCTIVE — the base .nes is never written.
function replaceRisaAsset(ctx: MenuContext, sys: SystemView, type: AssetTypeInfo, slot: number): void {
  const be = ctx.stores.backend;
  const kind = type.kind;
  browseThen(ctx, { title: `Replace ${kind} ${slot}`, patterns: type.patterns }, (path) => {
    const data = be.readFile(path);
    if (!data) return;
    let entry: RisaAssetOverride;
    try {
      if (kind === "theme") {
        const { theme } = parseRit(JSON.parse(new TextDecoder().decode(data))); // throws on a bad .rit
        entry = { type: "theme", slot, name: theme.name.trim() || stem(path), theme };
      } else if (kind === "kit") {
        if (data.length !== KIT_BANK_SIZE || !isBankPopulated(data)) return; // a .rkit is exactly one populated 8 KB DMC bank
        entry = { type: "kit", slot, name: bankToModel(data).name.trim() || stem(path), path };
      } else {
        if (data.length !== 0x2000) return; // a .chr is exactly one 8 KB CHR bank
        entry = { type: "font", slot, name: stem(path), path };
      }
    } catch {
      return; // malformed .rit / unreadable → leave the ROM untouched
    }
    writeOverrides(ctx, sys, "risa-assets", [...risaAssetOverrides(sys).filter((o) => !(o.type === kind && o.slot === slot)), entry]);
  });
}

const lsdjAssetSpec: AssetMenuSpec = { id: "lsdj", catalog: lsdjAssetCatalog, exportAsset, replaceAsset };
const risaAssetSpec: AssetMenuSpec = { id: "risa", catalog: risaAssetCatalog, exportAsset: exportRisaAsset, replaceAsset: replaceRisaAsset };

// --- LSDj Songs submenu (the SAV's 32 saved-song slots: export / replace / delete / add) ---------------
// Songs are the battery, NOT a ROM override: edits act directly on the live SRAM (like LSDj's own FILE
// screen) via mutateLiveSav (src/tracker/liveSav) - read the live sav, apply a BYTE-LEVEL transform (never
// the lossy Song model - see lsdjSongOps), write the resolved .sav, cold-boot the core from it. Durable on
// disk and reflected in the running LSDj. A no-op if there's no readable SRAM or the op returns null.
// Returns whether the edit landed, so a caller that CHAINS edits (save the working song, then load another)
// can stop when the first one declined rather than discarding work the save didn't actually preserve.
function mutateSavBytes(ctx: MenuContext, sys: SystemView, fn: (sav: Uint8Array) => Uint8Array | null): boolean {
  return mutateLiveSav(ctx.stores.backend, ctx.stores.project.systems, sys, fn);
}

// Export the saved song in `slot` to a picked `.lsdsng` file (byte-exact — decompress the slot, re-wrap).
function exportSong(ctx: MenuContext, sys: SystemView, slot: number, name: string): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: `Export song ${slot}`, patterns: ["*.lsdsng"], saving: true, defaultName: `${sanitizeName(name)}.lsdsng` }, (path) => {
    const bytes = ctx.stores.project.systems.readSram(sys.id);
    if (!bytes) return;
    const raw = decompressSlot(bytes, slot);
    if (!raw) return;
    be.writeFileAtomic(path, encodeLsdsngRaw(savSongName(bytes, slot) || name, savSongVersion(bytes, slot), raw));
  });
}

// Replace the song in `slot` from a picked `.lsdsng` or `.lsdprj` (byte-exact; a bad file leaves the sav
// alone). A `.lsdprj` also imports its kits (see importLsdprj).
function replaceSong(ctx: MenuContext, sys: SystemView, slot: number): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: `Replace song ${slot}`, patterns: ["*.lsdsng", "*.lsdprj"] }, (path) => {
    if (path.toLowerCase().endsWith(".lsdprj")) {
      importLsdprj(ctx, sys, path, slot);
      return;
    }
    const data = be.readFile(path);
    if (!data) return;
    mutateSavBytes(ctx, sys, (sav) => replaceSongInSav(sav, slot, data));
  });
}

// Import a `.lsdprj` (song + its kit banks). The song goes into a slot (targetSlot for Replace, else the
// first free slot); its kits are deduped against the effective ROM and the missing ones are recorded as
// lsdj-assets kit overrides that LINK the `.lsdprj` by path (+ ordinal), with the song's kit references
// remapped to the assigned slots (all byte-level — the Song model can't hold 6-bit kit indices). One rebuild
// (loadSram) applies the kit overrides → romBytes and boots the imported song from the .sav.
function importLsdprj(ctx: MenuContext, sys: SystemView, path: string, targetSlot?: number): void {
  const be = ctx.stores.backend;
  const systems = ctx.stores.project.systems;
  const file = be.readFile(path);
  const liveSram = systems.readSram(sys.id);
  const baseRom = sys.romPath ? be.readFile(sys.romPath) : null;
  if (!file || !liveSram || !baseRom) return;

  const overrides = lsdjOverrides(sys);
  const effectiveRom = applyOverridesToRom(baseRom, overrides, be);
  const plan = planLsdprjImport({ file, path, effectiveRom, overrides, liveSram, targetSlot });
  if (!plan) return; // malformed file / out of kit or song slots

  const target = resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath);
  if (!be.writeFileAtomic(target, plan.savBytes)) return;
  if (plan.addedKits > 0) systems.setRoleConfig(sys.id, "lsdj-assets", { overrides: plan.overrides });
  systems.loadSram(sys.id, target); // one rebuild: kit overrides → romBytes + boot the imported song
}

// Add a song: a `.lsdsng`/`.lsdprj` into the first free slot (the same importer drag-and-drop uses), or a
// selection of songs from a `.sav`/`.srm` (the checkbox picker, validated against the cart's console).
function addSongFromDisk(ctx: MenuContext, sys: SystemView): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: "Add Song", patterns: ["*.lsdsng", "*.lsdprj", ...SAV_PATTERNS] }, (path) => {
    if (isSavPath(path)) {
      const data = be.readFile(path);
      if (data) ctx.beginSongImport(sys, data);
    } else {
      importSongFiles(be, ctx.stores.project.systems, sys, [path]);
    }
  });
}

// --- the shared Songs submenu (SongCatalog-driven) -------------------------------------------------
// LSDj + risa songs are both the BATTERY, not a ROM override: edits act on the live SRAM via mutateSavBytes
// (readSram → byte-level catalog op → writeFileAtomic → loadSram cold-boot). One generic songMenu renders
// the structure over a SongCatalog — Load / Delete / reorder are its byte-ops; the console supplies the
// file-dialog actions (Export/Replace/Add, which own their formats). Reorder (Move Up/Down) rows show only
// when the catalog implements reorder — risa's positional records, or LSDj by swapping saved slots.
interface SongMenuSpec {
  id: string; // row-id prefix, e.g. "lsdj" / "risa"
  catalog: SongCatalog;
  exportSong(ctx: MenuContext, sys: SystemView, index: number, name: string): void;
  replaceSong(ctx: MenuContext, sys: SystemView, index: number): void;
  addSong(ctx: MenuContext, sys: SystemView): void;
  // The synthetic working-song row's actions (only for a console whose catalog reports an unsaved working
  // song — risa). Export writes the working song to disk; saveToCatalog promotes it to a real saved slot.
  exportWorking?(ctx: MenuContext, sys: SystemView, name: string): void;
  saveWorkingToCatalog?(ctx: MenuContext, sys: SystemView): void;
  // Commit the live working song into the catalog - the pure byte-op behind "Save Working Song & Load",
  // which is the only answer to the discard prompt that PRESERVES the work. Null = declined (full catalog
  // / malformed working song), which the menu surfaces as a disabled row rather than an action that fails.
  // `name` arrives only when `workingNeedsName` asked for one. Omitted by a console with no such op.
  commitWorking?(sav: Uint8Array, name?: string): Uint8Array | null;
  // True when the working song has no name to inherit and the user must supply one. LSDj keeps names on the
  // stored project rather than in the song, so an UNLINKED working song genuinely has none; risa's working
  // song carries its own, and a LINKED LSDj song inherits its slot's - both answer false and save outright.
  workingNeedsName?(sav: Uint8Array): boolean;
  // Cheap "is there anywhere to put it" test, so the menu can grey the row instead of offering a save that
  // fails. Deliberately NOT `commitWorking(sav) === null`: that compresses the whole song, and the answer is
  // needed once per menu build (the guard is identical on every song row). Omitted = assume yes.
  canCommitWorking?(sav: Uint8Array): boolean;
}

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
    const ok = mutateSavBytes(ctx, sys, (bytes) => {
      const saved = spec.commitWorking?.(bytes, name);
      return saved ? spec.catalog.load(saved, song.index) : null;
    });
    if (ok) ctx.stores.project.recordSong(song.name);
    return ok;
  };

  const saveRow = ((): MenuItem => {
    if (!spec.commitWorking || !(spec.canCommitWorking?.(sav) ?? true)) {
      // Not offerable: no such op, or the catalog can't take it (full). Say why rather than fail on click.
      return action(`${idp}-save-unavailable`, "Save Working Song & Load (no free slot)", () => {}, true);
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
    action(`${idp}-warn`, `"${workingLabel}" has unsaved changes`, () => {}, true),
    sep(`${idp}-warn-sep`),
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
  const discards = bytes ? (cat.workingSongDirty?.(bytes) ?? false) : false;
  const workingLabel = (bytes ? cat.workingName(bytes) : null) || "the working song";

  const rows: MenuItem[] = songs.map((s, i) => {
    const name = s.name || `Song ${s.index}`;
    // Load, then record this song in recents right away - by NAME, since we know which one we just loaded
    // and needn't wait for the rebuilt core to publish a battery snapshot. The song watcher would catch it
    // on its next tick anyway (that's what covers a load made from INSIDE the cart), but a menu load is a
    // deliberate act: its row should be at the top of Recent before the user gets back there.
    const doLoad = (): void => {
      mutateSavBytes(ctx, sys, (sav) => cat.load(sav, s.index));
      ctx.stores.project.recordSong(s.name);
    };
    const items: MenuItem[] = [
      // Clean working song: load outright - a confirm that fires when nothing would be lost is worse than
      // none, because it trains people to dismiss it. Dirty: a submenu of the two ways forward, where
      // backing out (Esc) IS the cancel. A submenu rather than a yes/no prompt because there are three
      // answers and PromptSpec only carries two.
      discards ? loadGuard(spec, ctx, sys, bytes!, workingLabel, s, doLoad) : action(`${spec.id}-song-${s.index}-load`, "Load...", doLoad),
      action(`${spec.id}-song-${s.index}-export`, "Export...", () => spec.exportSong(ctx, sys, s.index, name)),
      // Replace overwrites a SAVED slot - the durable copy, gone with no undo - so it always confirms,
      // whatever the working song's state. Confirm first, then browse: the picker is the console's own
      // (it owns the file formats), and a cancel there simply does nothing.
      {
        id: `${spec.id}-song-${s.index}-replace`,
        label: "Replace...",
        kind: "prompt" as const,
        keepOpen: true,
        prompt: {
          title: `Replace saved song "${name}"?`,
          hint: "Enter to pick a file  |  Esc to cancel",
          confirm: true,
          onConfirm: () => {
            spec.replaceSong(ctx, sys, s.index);
            return null;
          },
        },
      },
    ];
    if (cat.reorder) {
      // reorder takes LIST POSITIONS (index into the rendered list), not the row's `index` — they coincide
      // for a positional catalog (risa) but not for LSDj's sparse slot numbers.
      items.push(action(`${spec.id}-song-${s.index}-up`, "Move Up", () => mutateSavBytes(ctx, sys, (sav) => cat.reorder!(sav, i, i - 1)), i === 0));
      items.push(action(`${spec.id}-song-${s.index}-down`, "Move Down", () => mutateSavBytes(ctx, sys, (sav) => cat.reorder!(sav, i, i + 1)), i === last));
    }
    items.push({
      id: `${spec.id}-song-${s.index}-delete`,
      label: "Delete",
      kind: "prompt" as const,
      keepOpen: true,
      prompt: {
        title: `Delete song "${name}"?`,
        hint: "Enter to delete  |  Esc to cancel",
        confirm: true,
        onConfirm: () => {
          mutateSavBytes(ctx, sys, (sav) => cat.delete(sav, s.index));
          return null;
        },
      },
    });
    return submenu(`${spec.id}-song-${s.index}`, `[${s.index}] ${name}`, items);
  });
  const body = rows.length ? rows : [action(`${spec.id}-song-none`, "(no saved songs)", () => {}, true)];
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
            mutateSavBytes(ctx, sys, (sav) => cat.saveWorkingToSlot!(sav, t.index));
          }),
        );
      }
    }
    // Name the save by what it does, since the row now shows in both states and the two differ in a way the
    // user cares about: a LINKED song overwrites the slot it came from, an unlinked one grows the catalog.
    if (spec.saveWorkingToCatalog)
      wItems.push(
        action(`${spec.id}-song-working-save`, working.linked ? "Save Changes" : "Save as New Song", () => spec.saveWorkingToCatalog!(ctx, sys)),
      );
    if (spec.exportWorking)
      wItems.push(action(`${spec.id}-song-working-export`, "Export...", () => spec.exportWorking!(ctx, sys, wName)));
    // "(unsaved)" is what separates this row from the identically-named slot row below it when the song is
    // linked - without it the two read as two songs rather than one song and its uncommitted edits.
    workingRows.push(submenu(`${spec.id}-song-working`, `[working] ${wName} (unsaved)`, wItems), sep(`${spec.id}-song-working-sep`));
  }
  return submenu(`${spec.id}-songs`, "Songs", [
    ...workingRows,
    action(`${spec.id}-song-add`, "Add...", () => spec.addSong(ctx, sys)),
    sep(`${spec.id}-song-add-sep`),
    ...body,
  ]);
}

// The throw→null wrapper for risa's catalog ops used by its file actions (LSDj's ops return null directly;
// risa's byte-ops throw on a bad index / no space).
function tryOp(fn: () => Uint8Array): Uint8Array | null {
  try {
    return fn();
  } catch {
    return null; // out of range / malformed → leave the sav untouched
  }
}

// risa song file I/O: a `.risong` is the raw self-describing song record — the `.lsdsng` analog (song only,
// no kits). Export writes songRecordBytes; Replace/Add inject the record via the byte-level catalog ops.
function risaExportSong(ctx: MenuContext, sys: SystemView, index: number, name: string): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: `Export song ${index}`, patterns: ["*.risong"], saving: true, defaultName: `${sanitizeName(name)}.risong` }, (path) => {
    const bytes = ctx.stores.project.systems.readSram(sys.id);
    if (!bytes) return;
    const record = songRecordBytes(bytes, index);
    if (record) be.writeFileAtomic(path, record);
  });
}
function risaReplaceSong(ctx: MenuContext, sys: SystemView, index: number): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: `Replace song ${index}`, patterns: ["*.risong"] }, (path) => {
    const data = be.readFile(path);
    if (!data) return;
    mutateSavBytes(ctx, sys, (sav) => tryOp(() => replaceSongRecordInSav(sav, index, data)));
  });
}
function risaAddSong(ctx: MenuContext, sys: SystemView): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: "Add Song", patterns: ["*.risong", ...SAV_PATTERNS] }, (path) => {
    const data = be.readFile(path);
    if (!data) return;
    if (isSavPath(path)) ctx.beginSongImport(sys, data); // a whole .sav/.srm → the checkbox picker
    else mutateSavBytes(ctx, sys, (sav) => tryOp(() => addSongRecordToSav(sav, data))); // a single .risong record
  });
}
// The synthetic working-song row's actions. Export writes the LIVE working song (banks 0-3, encoded as a
// record) to a `.risong`; Save to Catalog promotes it into a new saved slot + links it, so it stops being
// unsaved and moves into the numbered list.
function risaExportWorking(ctx: MenuContext, sys: SystemView, name: string): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: "Export working song", patterns: ["*.risong"], saving: true, defaultName: `${sanitizeName(name)}.risong` }, (path) => {
    const bytes = ctx.stores.project.systems.readSram(sys.id);
    if (!bytes) return;
    const record = workingSongRecord(bytes);
    if (record) be.writeFileAtomic(path, record);
  });
}
function risaSaveWorking(ctx: MenuContext, sys: SystemView): void {
  mutateSavBytes(ctx, sys, (sav) => tryOp(() => saveWorkingToCatalog(sav)));
}

// risa's working song carries its own name, so it never needs one asked for - `workingNeedsName` is simply
// absent (false) and the menu offers a plain "Save Working Song & Load".
const risaCommitWorking = (sav: Uint8Array): Uint8Array | null => tryOp(() => saveWorkingToCatalog(sav));

// The LSDj working-song row's actions. The row shows only for a LINKED working song (see lsdjSongCatalog),
// so both of these inherit the active slot's name + version - LSDj keeps those on the stored project rather
// than in the song, so a detached working song has neither.
function lsdjExportWorking(ctx: MenuContext, sys: SystemView, name: string): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: "Export working song", patterns: ["*.lsdsng"], saving: true, defaultName: `${sanitizeName(name)}.lsdsng` }, (path) => {
    const bytes = ctx.stores.project.systems.readSram(sys.id);
    if (!bytes) return;
    const slot = lsdjActiveSlot(bytes);
    if (slot < 0) return;
    // The LIVE working song (the first 0x8000), not the stored slot - exporting the edits is the point.
    be.writeFileAtomic(path, encodeLsdsngRaw(savSongName(bytes, slot) || name, savSongVersion(bytes, slot), bytes.slice(0, 0x8000)));
  });
}
function lsdjSaveWorking(ctx: MenuContext, sys: SystemView): void {
  // No name argument: the row is linked-only, so saveWorkingToCatalog overwrites its slot and keeps its name.
  mutateSavBytes(ctx, sys, (sav) => lsdjSaveWorkingToCatalog(sav));
}

const lsdjSongSpec: SongMenuSpec = {
  id: "lsdj",
  catalog: lsdjSongCatalog,
  exportSong,
  replaceSong,
  addSong: addSongFromDisk,
  exportWorking: lsdjExportWorking,
  saveWorkingToCatalog: lsdjSaveWorking,
  commitWorking: lsdjSaveWorkingToCatalog,
  // Only an UNLINKED LSDj working song needs a name: a linked one inherits its slot's.
  workingNeedsName: (sav) => lsdjActiveSlot(sav) < 0,
  canCommitWorking: canSaveWorkingToCatalog,
};
const risaSongSpec: SongMenuSpec = {
  id: "risa",
  catalog: risaSongCatalog,
  exportSong: risaExportSong,
  replaceSong: risaReplaceSong,
  addSong: risaAddSong,
  exportWorking: risaExportWorking,
  saveWorkingToCatalog: risaSaveWorking,
  commitWorking: risaCommitWorking,
  canCommitWorking: canRisaSaveWorking,
};

// The per-console UI bindings for a tracker integration: the file-dialog specs (Songs + assets, which own
// file formats) plus any per-console extras not yet unified (LSDj's sync cyclers). Keyed by integration id;
// the integration itself (id/label/markerRole/songs/assets) rides src/tracker (the one place a console is
// registered).
interface TrackerUi {
  song: SongMenuSpec;
  asset: AssetMenuSpec;
  extras?(ctx: MenuContext, sys: SystemView): MenuItem[];
}
const TRACKER_UI: Record<string, TrackerUi> = {
  lsdj: { song: lsdjSongSpec, asset: lsdjAssetSpec, extras: lsdjExtras },
  risa: { song: risaSongSpec, asset: risaAssetSpec },
};

// One tracker's instance-submenu children: its extras (if any), the shared Songs menu, its asset menus, then
// the whole-ROM bake rows.
function trackerChildren(t: TrackerIntegration, ctx: MenuContext, sys: SystemView): MenuItem[] {
  const ui = TRACKER_UI[t.id];
  return [
    ...(ui.extras ? ui.extras(ctx, sys) : []),
    songMenu(ui.song, ctx, sys),
    ...assetMenu(ui.asset, ctx, sys),
    ...romPatchRows(ui.asset, ctx, sys),
  ];
}

// LSDj's sync cyclers (Mode / Tempo Divisor / Auto Start) + a separator — the one per-console tracker extra
// not yet unified (risa has no sync knobs).
function lsdjExtras(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const systems = ctx.stores.project.systems;
  const cfg = sys.roles.find((r) => r.kind === "lsdj-sync")?.config ?? {};
  const mode = typeof cfg.mode === "string" ? (cfg.mode as LsdjSyncMode) : "midiSync";
  const divisor = typeof cfg.tempoDivisor === "number" ? cfg.tempoDivisor : 1;
  const autoStart = cfg.autoStart === true;
  return [
    cycler("lsdj-mode", "Mode", LSDJ_MODE_NAMES, Math.max(0, LSDJ_MODE_VALUES.indexOf(mode)), (n) => systems.setRoleConfig(sys.id, "lsdj-sync", { mode: LSDJ_MODE_VALUES[n] })),
    cycler("lsdj-divisor", "Tempo Divisor", LSDJ_DIVISORS.map(String), Math.max(0, LSDJ_DIVISORS.indexOf(divisor)), (n) =>
      systems.setRoleConfig(sys.id, "lsdj-sync", { tempoDivisor: LSDJ_DIVISORS[n] }),
    ),
    // Auto Start taps START on the host transport rise to auto-arm SYNC=MIDI carts (MidiSync / Arduinoboy) —
    // off by default so the modes keep their manual-arm behaviour.
    cycler("lsdj-autostart", "Auto Start", OFF_ON, autoStart ? 1 : 0, (n) =>
      systems.setRoleConfig(sys.id, "lsdj-sync", { autoStart: n === 1 }),
    ),
    sep("lsdj-assets-sep"),
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
    if (project.currentPath()) items.push(action("proj-save", saveProjectLabel(ctx), () => project.save(project.currentPath())));
    items.push(action("proj-saveas", "Save Project As...", () =>
      browseThen(ctx, { title: "Save Project", patterns: PROJECT_PATTERNS, saving: true, defaultName: "project.rplg" }, (p) => project.save(p)),
    ));
    items.push(action("proj-export", "Export Zip...", () =>
      browseThen(ctx, { title: "Export Zip", patterns: ZIP_PATTERNS, saving: true, defaultName: "project.rplg.zip" }, (p) => project.export(p)),
    ));
  }
  items.push(sep("proj-sep0"));
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

  // The app-action rows (Open Menu / Cycle Instances / Cycle Instances (Back)) — same capture plumbing as a GB
  // row, written into the actions section. The "<label>: " head is kept so Menu's "Press a key/button…" swap works.
  const actionRows: MenuItem[] = APP_ACTION_ROWS.map((a) => ({
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
  // The persisted default render folder (System > Render seeds its Output Dir from this; "" = unset →
  // each cart falls back to its .sav / ROM folder). Set via a native folder picker; Clear returns to unset.
  const defaultDir = ctx.userConfig.render.outputDir;
  const renderDirItem = submenu("set-render-dir", dirLabel("Default Render Dir: ", defaultDir || "(unset)"), [
    action("set-render-dir-set", "Set...", () =>
      browseThen(ctx, { title: "Default Render Dir", patterns: [], directory: true, startDir: defaultDir }, (p) =>
        userConfig.setRenderOutputDir(p),
      ),
    ),
    action("set-render-dir-clear", "Clear", () => userConfig.setRenderOutputDir(""), !defaultDir),
  ]);
  return [
    cycler("set-sram", "SRAM Auto-Save", SRAM_AUTO_SAVES.map((m) => SRAM_AUTO_SAVE_LABELS[m] ?? m), sramIdx, (n) => userConfig.setSramAutoSave(SRAM_AUTO_SAVES[n])),
    { id: "set-defzoom", label: `Default Zoom: ${ctx.userConfig.defaultZoom}x`, kind: "cycler", keepOpen: true, onSelect: () => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, 1)), onCycle: (dir) => userConfig.setDefaultZoom(cycleInt(ctx.userConfig.defaultZoom, 1, 6, dir)) },
    renderDirItem,
    submenu("set-keybindings", "Keyboard Bindings", bindingsChildren(ctx, "keyboard")),
    submenu("set-gamepad-bindings", "Gamepad Bindings", bindingsChildren(ctx, "gamepad")),
    // The host's OS file dialog (default) vs the in-app browser. On a host with no OS dialog it stays in-app.
    cycler("set-native-dialogs", "File Dialogs", ["In-App", "OS Native"], ctx.userConfig.useNativeFileDialogs ? 1 : 0, (n) => userConfig.setUseNativeFileDialogs(n === 1)),
    // Audio device (sample rate / block size) — standalone only, where the SDL host exposes the seam.
    ...(isStandalone() && hasAudioConfig() ? [submenu("set-audio", "Audio", audioSettingsChildren())] : []),
    // MIDI input/output device selection — standalone only, where the SDL host exposes the RtMidi seam.
    ...(isStandalone() && hasMidiConfig() ? [submenu("set-midi", "MIDI", midiSettingsChildren())] : []),
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
        ? () => browseThen(ctx, { title: "Locate Project", patterns: LOAD_PATTERNS }, (p) => ctx.stores.recent.relink(entry.path, p))
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
export function trackerCartLabel(backend: Pick<ControlPlaneBackend, "readFile" | "readSram">, sys: SystemView): string | null {
  const tracker = resolveTracker(sys.roles);
  if (!tracker) return null;
  const romName = cartRomName(backend, tracker, sys.romPath);
  const sram = backend.readSram(sys.id);
  const song = sram ? tracker.songs.workingName(sram) : null;
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
 *  stem (or "mGB" for the embedded synth), dropped when it equals the project name so it isn't shown twice. */
function instanceTitle(ctx: MenuContext, sys: SystemView): string {
  const base = ctx.version ? `RetroPlug v${ctx.version}` : "RetroPlug";
  const cart = trackerCartLabel(ctx.stores.backend, sys);
  if (cart) return `${base} - ${cart}`;
  const project = ctx.stores.project.displayName(); // the user's name, else the one derived from the systems
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
  // The tracker submenu (LSDj / risa / …) — one generic branch, gated on the marker role the ROM sniffed.
  const tracker = resolveTracker(sys.roles);
  return {
    title: instanceTitle(ctx, sys),
    items: [
      // "Load…" is a project-level op (load the sibling project / new project from the ROM) — it never
      // swaps this instance. Swapping a single instance in place is "Replace Instance".
      action("inst-load", "Load...", () => runLoad(ctx)),
      action("inst-save", saveProjectLabel(ctx), () => void saveProjectInteractive(ctx.stores)),
      action("inst-new", "New Project", () => ctx.newProject()),
      submenu("inst-recent", "Recent", recentChildren(ctx)),
      // The tracker submenu (LSDj / risa) sits right under Recent, fenced by a separator on each side (the
      // one above here, and inst-sep-top below). Only present when the cart sniffed a tracker.
      ...(tracker
        ? [
            sep("inst-sep-tracker"),
            trackerVersionSupported(tracker, ctx.stores.backend, sys.romPath)
              ? submenu(`inst-${tracker.id}`, tracker.label, trackerChildren(tracker, ctx, sys))
              : action(`inst-${tracker.id}`, `${tracker.label} (Unsupported Version)`, () => {}, true),
          ]
        : []),
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
      submenu("inst-project", "Project", projectChildren(ctx)),
      submenu("inst-settings", "Settings", settingsChildren(ctx)),
      ...(isStandalone() ? [sep("inst-sep-exit"), action("inst-exit", "Exit RetroPlug", () => ctx.requestExit())] : []),
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
      ...(isStandalone() ? [sep("start-sep-exit"), action("start-exit", "Exit RetroPlug", () => ctx.requestExit())] : []),
      // Deferred: About panel.
    ],
  };
}

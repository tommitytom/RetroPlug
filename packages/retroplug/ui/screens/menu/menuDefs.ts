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
import { SRAM_AUTO_SAVES, RENDER_SAMPLE_RATES } from "../../../src/userConfig";
import type { SplitMode } from "../../../src/render";
import { defaultBindingMap, type BindingMap } from "../../../src/bindingMap";
import { isValidProfileName, isValidProfileChar } from "../../../src/bindingsStore";
import type { RecentView } from "../../../src/recentStore";
import { resolveSavPath, siblingPath } from "../../../src/savPaths";
import { stem } from "../../../src/pathUtil";
import { LsdjRom, decodeLsdpal, encodeLsdpal, KIT_COUNT } from "../../../src/lsdj/rom";
import { listProjects, decompressSlot, encodeLsdsngRaw, savSongName, savSongVersion } from "../../../src/lsdjSav";
import { loadSongToWorking, deleteSongInSav, replaceSongInSav, importAllSongsFromSav } from "../../../src/lsdjSongOps";
import { importSongFiles } from "../../../src/lsdjSongImport";
import { readOverrides, applyOverridesToRom, type LsdjAssetOverride } from "../../../src/lsdjAssetsRole";
import { planLsdprjImport } from "../../../src/lsdjLsdprjImport";
import type { HostBackend } from "../../../src/backend";
import { openPath } from "../../lvgl/openPath";
import { startSystemRender, validSplits, formatDuration } from "../../lvgl/render";
import { saveProjectInteractive } from "../../lvgl/saveProjectInteractive";
import type { FileBrowserOpts } from "../../../src/backend";
import { hasAudioConfig, getAudioDraft, setAudioDraft, applyAudioDraft, audioDraftDirty } from "./audioDraft";
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
  /** Quit the standalone (unsaved-changes guarded). No-op in a DAW (the host owns the window). */
  requestExit: () => void;
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
  const cfg = getAudioDraft() ?? { sampleRate: 48000, blockSize: 2048, outChannels: 2 };
  const rateIdx = Math.max(0, AUDIO_RATES.indexOf(cfg.sampleRate));
  const blockIdx = Math.max(0, AUDIO_BLOCKS.indexOf(cfg.blockSize));
  const chIdx = Math.max(0, AUDIO_CHANNELS.indexOf(cfg.outChannels));
  const dirty = audioDraftDirty();
  return [
    // The cyclers stage a pending value only — the label tracks the draft, but the live device is unchanged.
    cycler("audio-rate", "Sample Rate", AUDIO_RATES.map((r) => `${r} Hz`), rateIdx, (n) => setAudioDraft({ sampleRate: AUDIO_RATES[n] })),
    cycler("audio-block", "Block Size", AUDIO_BLOCKS.map((b) => `${b}`), blockIdx, (n) => setAudioDraft({ blockSize: AUDIO_BLOCKS[n] })),
    cycler("audio-channels", "Out Channels", AUDIO_CHANNEL_NAMES, chIdx, (n) => setAudioDraft({ outChannels: AUDIO_CHANNELS[n] })),
    sep("audio-sep-apply"),
    // Commit the staged rate/block/channels to the device. Greyed (inert) until there's a pending change.
    action("audio-apply", "Apply", () => applyAudioDraft(), !dirty),
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
const SRAM_PATTERNS = ["*.sav", "*.srm"];
const WAV_PATTERNS = ["*.wav"]; // render output

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

// APU flush-window latency presets (ms) for the NES "APU Latency" cycler. The role config accepts any
// value in [0.25, 6.0] (clampedNumber); the menu just offers a few sensible steps. 1.4ms ≈ the default.
const APU_LATENCY_MS = [0.5, 1.0, 1.4, 3.0, 5.0];
const APU_LATENCY_NAMES = ["0.5 ms", "1.0 ms", "1.4 ms", "3.0 ms", "5.0 ms"];

/** Index of the preset nearest `ms`, so the cycler shows the current value even if it's off-grid. */
function nearestApuLatencyIndex(ms: number): number {
  let best = 0;
  for (let i = 1; i < APU_LATENCY_MS.length; i++) {
    if (Math.abs(APU_LATENCY_MS[i] - ms) < Math.abs(APU_LATENCY_MS[best] - ms)) best = i;
  }
  return best;
}

/** The Mesen core-role config (region / removeSpriteLimit / apuLatencyMs), with defaults. The role attaches
 *  to any Mesen system; the knobs are NES-only, so the menu gates the rows on platform === "nes". */
function mesenConfig(sys: SystemView): { region: ConsoleRegion; removeSpriteLimit: boolean; apuLatencyMs: number } {
  const c = (sys.roles.find((r) => r.kind === "mesen")?.config ?? {}) as Record<string, unknown>;
  return {
    region: typeof c.region === "string" ? (c.region as ConsoleRegion) : "auto",
    removeSpriteLimit: c.removeSpriteLimit === true,
    apuLatencyMs: typeof c.apuLatencyMs === "number" ? c.apuLatencyMs : 1.4,
  };
}

// --- child builders -----------------------------------------------------------------------------------
function systemChildren(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const systems = ctx.stores.project.systems;
  // Reset reboots carrying the battery — pathless, reconstructing in place (no live GB_reset). Sits at the
  // top with a separator below it.
  const items: MenuItem[] = [
    action("sys-reset", "Reset", () => void systems.reset(sys.id)),
    // Swap the ROM in place but keep the running battery SRAM (e.g. an LSDj version bump that keeps the
    // song). ROM-only browser; distinct from "Replace Instance", which cold-boots a fresh sav.
    action("sys-swaprom", "Swap ROM (Preserve SRAM)...", () => void ctx.stores.fileSelection.browseSwap(sys.id)),
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
      cycler("sys-nes-apu-latency", "APU Latency", APU_LATENCY_NAMES, nearestApuLatencyIndex(cfg.apuLatencyMs), (n) =>
        systems.setRoleConfig(sys.id, "mesen", { apuLatencyMs: APU_LATENCY_MS[n] }),
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

  // Render to WAV — a background job on a fresh instance built from a COPY of the live SRAM/savestate (never
  // the running core), like `retroplug-cli render`. The menu can close while it runs; progress + cancel show
  // on the system tile. Split/sample-rate/max-duration are picked here (persisted in userConfig) and the one
  // "Render..." action applies them. Only for on-disk ROMs. Split modes gate on platform.
  if (sys.romPath) {
    const userConfig = ctx.stores.userConfig;
    const r = ctx.userConfig.render;
    const splits = validSplits(sys);
    const split = splits.includes(r.split) ? r.split : "mix"; // clamp a stored pins/channels to this platform
    const rateIdx = Math.max(0, RENDER_SAMPLE_RATES.indexOf(r.sampleRate as never));
    const setMaxDur = (delta: number) => userConfig.setRenderMaxDurationSec(r.maxDurationSec + delta);

    const renderChildren: MenuItem[] = [
      cycler("sys-render-split", "Audio Routing", splits.map((s) => SPLIT_LABELS[s]), splits.indexOf(split), (n) =>
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
      action("sys-render-go", "Render...", () =>
        browseThen(
          ctx,
          { title: "Render", patterns: WAV_PATTERNS, saving: true, defaultName: `${romStem || "render"}.wav` },
          (p) =>
            void startSystemRender(
              ctx.stores.backend,
              sys,
              { split, sampleRate: r.sampleRate, maxDurationMs: r.maxDurationSec * 1000 },
              p,
            ),
        ),
      ),
    ];
    items.push(sep("sys-sep-render"), submenu("sys-render", "Render", renderChildren));
  }
  return items;
}

/** The LSDj sync submenu — Mode + Tempo Divisor + Auto Start cyclers. Shown only for a system carrying an lsdj-sync
 *  role (a sniffed LSDj cart). Both edits re-push the DSP kernel structure (setRoleConfig → markDirty →
 *  syncDspFromStore), so they apply to the running behaviour on the next block — no dedicated RPC. */
// --- LSDj asset submenus (Kits / Fonts / Palettes: export or non-destructively replace) --------------
// The overrides never touch the base .gb — they ride the `lsdj-assets` role config and are applied to the
// ROM in memory at construct (see lsdjAssetsRole.ts). The menu lists the BASE ROM's assets (memoised by
// romPath — the tree rebuilds every render) and overlays the override state read live from role config.

type AssetKind = "kit" | "palette" | "font";
interface AssetSlot { slot: number; name: string }
interface LsdjInventory { kit: AssetSlot[]; palette: AssetSlot[]; font: AssetSlot[] }

const ASSET_META: Record<AssetKind, { title: string; patterns: string[]; ext: string }> = {
  kit: { title: "Kits", patterns: ["*.kit"], ext: ".kit" },
  palette: { title: "Palettes", patterns: ["*.lsdpal"], ext: ".lsdpal" },
  font: { title: "Fonts", patterns: ["*.png"], ext: ".png" },
};

// Read + parse the base ROM's asset inventory once per romPath (the menu rebuilds every render).
const lsdjInvCache = new Map<string, LsdjInventory | null>();
function lsdjInventory(backend: HostBackend, romPath: string): LsdjInventory | null {
  if (lsdjInvCache.has(romPath)) return lsdjInvCache.get(romPath) ?? null;
  let inv: LsdjInventory | null = null;
  const bytes = romPath ? backend.readFile(romPath) : null;
  if (bytes) {
    const rom = LsdjRom.fromBytes(bytes);
    if (rom.isLsdj) {
      inv = {
        kit: rom.kits().filter((k) => k.valid).map((k) => ({ slot: k.index, name: k.name() || `Kit ${k.index}` })),
        palette: rom.palettes().map((p) => ({ slot: p.index, name: p.name || `Palette ${p.index}` })),
        font: rom.fonts().map((f) => ({ slot: f.index, name: f.name || `Font ${f.index}` })),
      };
    }
  }
  lsdjInvCache.set(romPath, inv);
  return inv;
}

const lsdjOverrides = (sys: SystemView): LsdjAssetOverride[] =>
  readOverrides(sys.roles.find((r) => r.kind === "lsdj-assets")?.config);

// Export asset `kind`/`slot` to a picked file: the override bytes if replaced (already the file format),
// else the base ROM's asset read straight out via the pure-TS module.
function exportAsset(ctx: MenuContext, sys: SystemView, kind: AssetKind, slot: number, label: string): void {
  const be = ctx.stores.backend;
  const ov = lsdjOverrides(sys).find((o) => o.type === kind && o.slot === slot);
  const defaultName = `${sanitizeName(label)}${ASSET_META[kind].ext}`;
  browseThen(ctx, { title: `Export ${kind} ${slot}`, patterns: ASSET_META[kind].patterns, saving: true, defaultName }, (path) => {
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

// Replace asset `kind`/`slot` from a picked file: validate by trial-applying to the base ROM (import*
// throws on a bad file), record the override (raw file bytes, base64) in role config, and reload so it
// takes effect. NON-DESTRUCTIVE — the base .gb is never written.
function replaceAsset(ctx: MenuContext, sys: SystemView, kind: AssetKind, slot: number): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: `Replace ${kind} ${slot}`, patterns: ASSET_META[kind].patterns }, (path) => {
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
    const overrides = lsdjOverrides(sys).filter((o) => !(o.type === kind && o.slot === slot));
    overrides.push(entry);
    ctx.stores.project.systems.setRoleConfig(sys.id, "lsdj-assets", { overrides });
    ctx.stores.project.systems.reloadSystem(sys.id); // rebuild → onConstruct re-patches the effective ROM
  });
}

function removeOverride(ctx: MenuContext, sys: SystemView, kind: AssetKind, slot: number): void {
  const overrides = lsdjOverrides(sys).filter((o) => !(o.type === kind && o.slot === slot));
  ctx.stores.project.systems.setRoleConfig(sys.id, "lsdj-assets", { overrides });
  ctx.stores.project.systems.reloadSystem(sys.id);
}

// Non-destructively remove a kit from a slot: drop any existing kit override there, and — when the BASE
// ROM has a kit in that slot — record an `erase` override so construct empties it. (A slot present only
// because of a replace override just reverts to base-empty once that override is dropped.)
function deleteKit(ctx: MenuContext, sys: SystemView, slot: number): void {
  const inv = lsdjInventory(ctx.stores.backend, sys.romPath);
  const baseValid = !!inv?.kit.some((k) => k.slot === slot);
  const overrides = lsdjOverrides(sys).filter((o) => !(o.type === "kit" && o.slot === slot));
  if (baseValid) overrides.push({ type: "kit", slot, name: "", erase: true });
  ctx.stores.project.systems.setRoleConfig(sys.id, "lsdj-assets", { overrides });
  ctx.stores.project.systems.reloadSystem(sys.id);
}

// Add a kit from disk into the first empty kit slot (a replace override on an unused slot).
function addKit(ctx: MenuContext, sys: SystemView): void {
  const inv = lsdjInventory(ctx.stores.backend, sys.romPath);
  if (!inv) return;
  const used = new Set(effectiveKits(inv.kit, lsdjOverrides(sys)).map((k) => k.slot));
  let slot = 0;
  while (slot < KIT_COUNT && used.has(slot)) slot++;
  if (slot >= KIT_COUNT) return; // all kit slots full
  replaceAsset(ctx, sys, "kit", slot);
}

// A safe filename fragment (mirrors the CLI's sanitize).
const sanitizeName = (s: string): string => s.replace(/[^A-Za-z0-9._-]/g, "_") || "asset";
const readLsdjRom = (be: HostBackend, romPath: string): LsdjRom | null => {
  const bytes = romPath ? be.readFile(romPath) : null;
  if (!bytes) return null;
  const rom = LsdjRom.fromBytes(bytes);
  return rom.isLsdj ? rom : null;
};

interface KitRow { slot: number; name: string; overridden: boolean }
// The kit slots of the EFFECTIVE ROM (base + overrides): base-valid kits, plus slots added by a replace
// override, minus slots emptied by an erase override. Sorted by slot.
function effectiveKits(base: AssetSlot[], overrides: LsdjAssetOverride[]): KitRow[] {
  const rows = new Map<number, KitRow>();
  for (const s of base) rows.set(s.slot, { slot: s.slot, name: s.name, overridden: false });
  for (const ov of overrides) {
    if (ov.type !== "kit") continue;
    if (ov.erase) rows.delete(ov.slot);
    else rows.set(ov.slot, { slot: ov.slot, name: ov.name || `Kit ${ov.slot}`, overridden: true });
  }
  return [...rows.values()].sort((a, b) => a.slot - b.slot);
}

// One asset item: Export / Replace, plus Delete (kits only) + Remove Override (when overridden).
function assetRow(ctx: MenuContext, sys: SystemView, kind: AssetKind, slot: number, name: string, overridden: boolean): MenuItem {
  const label = `[${slot}] ${name}${overridden ? " *" : ""}`;
  const items: MenuItem[] = [
    action(`lsdj-${kind}-${slot}-export`, "Export...", () => exportAsset(ctx, sys, kind, slot, name)),
    action(`lsdj-${kind}-${slot}-replace`, "Replace from Disk...", () => replaceAsset(ctx, sys, kind, slot)),
  ];
  if (kind === "kit") items.push(action(`lsdj-kit-${slot}-delete`, "Delete", () => deleteKit(ctx, sys, slot)));
  if (overridden) items.push(action(`lsdj-${kind}-${slot}-remove`, "Remove Override", () => removeOverride(ctx, sys, kind, slot)));
  return submenu(`lsdj-${kind}-${slot}`, label, items);
}

// Build the Kits/Fonts/Palettes submenus; empty when the ROM can't be read (e.g. headless). The Kits menu
// leads with an "Add..." item (+ separator); each kit also gets a Delete.
function lsdjAssetMenus(ctx: MenuContext, sys: SystemView): MenuItem[] {
  const inv = lsdjInventory(ctx.stores.backend, sys.romPath);
  if (!inv) return [];
  const overrides = lsdjOverrides(sys);
  const kits = submenu("lsdj-kits", ASSET_META.kit.title, [
    action("lsdj-kit-add", "Add...", () => addKit(ctx, sys)),
    sep("lsdj-kit-add-sep"),
    ...effectiveKits(inv.kit, overrides).map((k) => assetRow(ctx, sys, "kit", k.slot, k.name, k.overridden)),
  ]);
  const others = (["font", "palette"] as AssetKind[]).map((kind) =>
    submenu(`lsdj-${kind}s`, ASSET_META[kind].title, inv[kind].map((s) => {
      const ov = overrides.find((o) => o.type === kind && o.slot === s.slot);
      return assetRow(ctx, sys, kind, s.slot, ov?.name || s.name, !!ov);
    })),
  );
  return [kits, ...others];
}

// --- LSDj Songs submenu (the SAV's 32 saved-song slots: export / replace / delete / add) ---------------
// Songs are the battery, NOT a ROM override: edits act directly on the live SRAM (like LSDj's own FILE
// screen). mutateSavBytes reads the live sav, applies a BYTE-LEVEL transform (never the lossy Song model —
// see lsdjSongOps), writes the resolved .sav, and cold-boots the core from it (loadSram) — durable on disk
// and reflected in the running LSDj. A no-op if there's no readable SRAM or the op returns null.
function mutateSavBytes(ctx: MenuContext, sys: SystemView, fn: (sav: Uint8Array) => Uint8Array | null): void {
  const systems = ctx.stores.project.systems;
  const bytes = systems.readSram(sys.id);
  if (!bytes) return;
  const out = fn(bytes);
  if (!out) return; // malformed / out of space → leave the system untouched
  const target = resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath);
  if (!ctx.stores.backend.writeFileAtomic(target, out)) return;
  systems.loadSram(sys.id, target);
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

// Add a song: a `.lsdsng`/`.lsdprj` into the first free slot (the same importer drag-and-drop uses), or all
// songs from a `.sav`.
function addSongFromDisk(ctx: MenuContext, sys: SystemView): void {
  const be = ctx.stores.backend;
  browseThen(ctx, { title: "Add Song", patterns: ["*.lsdsng", "*.lsdprj", "*.sav"] }, (path) => {
    if (path.toLowerCase().endsWith(".sav")) {
      const data = be.readFile(path);
      if (data) mutateSavBytes(ctx, sys, (sav) => importAllSongsFromSav(sav, data));
    } else {
      importSongFiles(be, ctx.stores.project.systems, sys, [path]);
    }
  });
}

// The Songs submenu: Add… (+ separator) then one row per occupied slot (Export / Replace / Delete).
function lsdjSongMenu(ctx: MenuContext, sys: SystemView): MenuItem {
  const bytes = ctx.stores.project.systems.readSram(sys.id);
  const songs = bytes ? listProjects(bytes) : [];
  const rows: MenuItem[] = songs.map((s) => {
    const name = s.name || `Song ${s.slot}`;
    return submenu(`lsdj-song-${s.slot}`, `[${s.slot}] ${name}`, [
      // Load the slot into working memory + reset the emulator so it boots showing this song.
      action(`lsdj-song-${s.slot}-load`, "Load...", () => mutateSavBytes(ctx, sys, (sav) => loadSongToWorking(sav, s.slot))),
      action(`lsdj-song-${s.slot}-export`, "Export...", () => exportSong(ctx, sys, s.slot, s.name || `song${s.slot}`)),
      action(`lsdj-song-${s.slot}-replace`, "Replace from Disk...", () => replaceSong(ctx, sys, s.slot)),
      {
        id: `lsdj-song-${s.slot}-delete`,
        label: "Delete",
        kind: "prompt" as const,
        keepOpen: true,
        prompt: {
          title: `Delete song "${name}"?`,
          hint: "Enter to delete  |  Esc to cancel",
          confirm: true,
          onConfirm: () => {
            mutateSavBytes(ctx, sys, (sav) => deleteSongInSav(sav, s.slot));
            return null;
          },
        },
      },
    ]);
  });
  return submenu("lsdj-songs", "Songs", [action("lsdj-song-add", "Add...", () => addSongFromDisk(ctx, sys)), sep("lsdj-song-add-sep"), ...rows]);
}

function lsdjChildren(ctx: MenuContext, sys: SystemView, cfg: Record<string, unknown>): MenuItem[] {
  const systems = ctx.stores.project.systems;
  const mode = typeof cfg.mode === "string" ? (cfg.mode as LsdjSyncMode) : "midiSync";
  const divisor = typeof cfg.tempoDivisor === "number" ? cfg.tempoDivisor : 1;
  const autoStart = cfg.autoStart === true;
  return [
    cycler("lsdj-mode", "Mode", LSDJ_MODE_NAMES, Math.max(0, LSDJ_MODE_VALUES.indexOf(mode)), (n) => systems.setRoleConfig(sys.id, "lsdj-sync", { mode: LSDJ_MODE_VALUES[n] })),
    cycler("lsdj-divisor", "Tempo Divisor", LSDJ_DIVISORS.map(String), Math.max(0, LSDJ_DIVISORS.indexOf(divisor)), (n) =>
      systems.setRoleConfig(sys.id, "lsdj-sync", { tempoDivisor: LSDJ_DIVISORS[n] }),
    ),
    // Auto Start taps START on the host transport rise to auto-arm SYNC=MIDI carts (MidiSync /
    // Arduinoboy) — off by default so the modes keep their manual-arm behaviour.
    cycler("lsdj-autostart", "Auto Start", OFF_ON, autoStart ? 1 : 0, (n) =>
      systems.setRoleConfig(sys.id, "lsdj-sync", { autoStart: n === 1 }),
    ),
    sep("lsdj-assets-sep"),
    // Songs: manage the SAV's saved-song slots (export .lsdsng / replace / delete / add).
    lsdjSongMenu(ctx, sys),
    // Kits / Fonts / Palettes: export or non-destructively replace each asset (stored in the project).
    ...lsdjAssetMenus(ctx, sys),
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
    // In-app browser (default) vs the host's OS file dialog. On a host with no OS dialog it just stays in-app.
    cycler("set-native-dialogs", "File Dialogs", ["In-App", "OS Native"], ctx.userConfig.useNativeFileDialogs ? 1 : 0, (n) => userConfig.setUseNativeFileDialogs(n === 1)),
    // Audio device (sample rate / block size) — standalone only, where the SDL host exposes the seam.
    ...(isStandalone() && hasAudioConfig() ? [submenu("set-audio", "Audio", audioSettingsChildren())] : []),
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

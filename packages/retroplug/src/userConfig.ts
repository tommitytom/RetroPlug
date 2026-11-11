// Per-user, machine-global preferences: config.json. The fields the SHELL owns: which keyboard/gamepad binding
// profiles are active, the default zoom for fresh projects, the loose-.sav auto-save preference, the render menu's
// picks and the file-dialog choice. This is the model + its zod validator; parse/serialize live in
// userConfigSerialization.ts and the Backend-backed store in userConfigStore.ts (mirroring the recentList /
// recentSerialization / recentStore triad).
//
// Every OTHER root key is a package's config section (configSections.ts): the audio device, the control surface. The
// shell keeps one it has no contributor for verbatim, so a plugin (which lists none of those packages) writes a
// standalone's blocks back instead of erasing them.
//
// The bindings PROFILE files (bindings/<name>.json) are a separate concern — this owns
// only config.json, holding the active-profile names as plain strings.

import { z } from "zod";
import { clampedInt, stringField, enumField } from "@retroplug/core/configFields";
import type { SplitMode } from "./render";
import { defaultConfigSections, serializeConfigSectionValue, type ConfigSectionEntry } from "./configSections";
import { CONFIG_SECTIONS } from "./generated/packages";

/** The persisted render-menu selections (System > Render): what to split into, the output sample rate, and
 *  the max render length. Global (not per-system) so the single "Render..." action reads the current picks. */
export const RENDER_SPLITS = ["mix", "channels", "pins"] as const;
// Matches the rates the standalone's audio device offers (menuDefs AUDIO_RATES) from 44.1k up. A render is
// offline, so a high rate costs render time and file size rather than risking an underrun; the CLI has
// always taken any rate via --sample-rate, and this is the picker catching up. Additive, so no config
// migration: widening the accepted set can only turn a value the schema used to rewrite into one it keeps.
export const RENDER_SAMPLE_RATES = [44100, 48000, 88200, 96000, 176400, 192000] as const;
export const RENDER_MAX_DURATION_MIN_SEC = 5;
export const RENDER_MAX_DURATION_MAX_SEC = 1800; // 30 min cap
/** What "Render" does when the target file already exists: clobber it, or write to the next free name. */
export const RENDER_ON_EXISTS = ["overwrite", "rename"] as const;
export type RenderOnExists = (typeof RENDER_ON_EXISTS)[number];

export interface RenderSettings {
  split: SplitMode; // clamped to the system's platform when a render actually starts
  sampleRate: number; // one of RENDER_SAMPLE_RATES
  maxDurationSec: number; // bounds every render (LSDj auto-length cap + the fixed-render length)
  outputDir: string; // the Settings "Default Render Dir"; "" = unset → derive from the .sav folder (else the ROM folder)
  onExists: RenderOnExists; // overwrite the target file, or write to the next free "<name>_N"
}

// A missing/garbage `render` block becomes {} so the child fields fill their own defaults (the preprocess
// idiom clampedInt uses, applied one level up).
const renderSchema = z.preprocess(
  (v) => (v && typeof v === "object" && !Array.isArray(v) ? v : {}),
  z.object({
    split: enumField(RENDER_SPLITS, "mix"),
    sampleRate: z.preprocess((v) => (RENDER_SAMPLE_RATES.includes(v as never) ? v : 44100), z.number()),
    maxDurationSec: clampedInt(RENDER_MAX_DURATION_MIN_SEC, RENDER_MAX_DURATION_MAX_SEC, 600), // 10 min
    outputDir: stringField(""),
    onExists: enumField(RENDER_ON_EXISTS, "overwrite"),
  }),
);

/** When the loose sibling <rom>.sav is auto-saved. `OnProjectSave` (the default) writes it at the
 *  moments the user acts — saving the project, and closing a cart — and `Continuous` adds a throttled
 *  idle mirror on top.
 *
 *  There used to be a third mode, `Off`. It never did what it said: the save path wrote regardless, and
 *  the one function that honoured the preference at save time had no callers, so `Off` and
 *  `OnProjectSave` were the same behaviour under two labels. Rather than make `Off` literal - which
 *  would mean a cart's battery could be discarded on teardown by preference - the mode was dropped
 *  (v2→v3 migrates it to `OnProjectSave`). The battery always reaches disk; the only choice is whether
 *  it also happens while you play.
 *
 *  The field is named `sramAutoSave` rather than native's "mirror": that reading comes from the plugin's
 *  side, whereas "auto save" fits the plugin and the standalone equally. Native carries no counterpart
 *  today - the whole policy is TS. */
export const SRAM_AUTO_SAVES = ["OnProjectSave", "Continuous"] as const;
export type SramAutoSave = (typeof SRAM_AUTO_SAVES)[number];

/** config.json — the per-user preferences. */
export interface UserConfig {
  activeKeyboardBindings: string; // names a bindings/<name>.json profile
  activeGamepadBindings: string; // names a bindings/<name>.json profile
  defaultZoom: number; // 1..6; a fresh project with zoom == 0 inherits this
  sramAutoSave: SramAutoSave;
  render: RenderSettings; // System > Render menu selections
  useNativeFileDialogs: boolean; // true = the host's OS file dialog (default); false = the in-app browser
}

/** Validates + defaults + clamps a (possibly partial/stale) config.json object. Strict:
 *  unknown keys are stripped; a missing/bad field takes its default; an out-of-range
 *  defaultZoom clamps to 1..6; an unknown sramAutoSave (including the retired "Off") falls back to
 *  the default - the v2→v3 migration handles it explicitly so the coercion is never load-bearing. */
export const userConfigSchema = z.object({
  activeKeyboardBindings: stringField("default"),
  activeGamepadBindings: stringField("default"),
  defaultZoom: clampedInt(1, 6, 3),
  sramAutoSave: z.enum(SRAM_AUTO_SAVES).catch("OnProjectSave").default("OnProjectSave"),
  render: renderSchema,
  // Defaults ON: the OS picker is what people expect of a desktop app, and a host that provides none leaves
  // the hook unbound so this transparently falls back to the in-app browser (NativeFileDialog::available()).
  // A config.json predating the field therefore adopts the OS dialog; one written since carries its own value.
  useNativeFileDialogs: z.boolean().catch(true).default(true), // additive → no migration
});

export const DEFAULT_USER_CONFIG: UserConfig = userConfigSchema.parse({}) as UserConfig;

/** The root keys the SHELL owns, which no package may contribute a section under. Derived from the schema's own shape
 *  (so a field added above is covered without a second list to forget) plus `schemaVersion`, which is the file's stamp
 *  and belongs to no section. The generated registry refuses a contributor of one of these
 *  (`packages.mjs`'s `SHELL_CONFIG_KEYS`, which a test holds to this list). */
export const CORE_CONFIG_KEYS: readonly string[] = ["schemaVersion", ...Object.keys(userConfigSchema.shape)];

/** config.json as the app holds it: the shell's own fields, and each package's section under its root key. A section
 *  key the product has no contributor for is the raw JSON the file carried. */
export interface ConfigModel {
  readonly core: UserConfig;
  readonly sections: Readonly<Record<string, unknown>>;
  /** Each section's block exactly as the file carried it, by key. A block this session did not set is written back
   *  from here, so an additive field a newer build wrote survives this one (the verbatim-keep rule). */
  readonly raw?: Readonly<Record<string, unknown>>;
  /** The section keys THIS session has set: only their parsed value is written, because only they have a value this
   *  build authored. */
  readonly written?: readonly string[];
}

/** A fresh config: the shell's defaults and each contributor's defaults. */
export function defaultConfig(): ConfigModel {
  return { core: { ...DEFAULT_USER_CONFIG }, sections: defaultConfigSections(), raw: {}, written: [] };
}

/** A config as config.json writes it: the shell's fields in the order the file has always had them, then each section
 *  under its key (a contributor's stamping, or the raw block). The core fields go first so a section can never shadow a
 *  shell key: the schema strips one, and `assertConfigSectionKey` refuses the write that would have made it.
 *
 *  The explicit order is byte-stability: `render` follows `useNativeFileDialogs` here because that is where the shell
 *  always wrote it, and a config.json this build reads and writes back must not move a key. */
export function configFileShape(
  model: ConfigModel,
  contributors: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
): Record<string, unknown> {
  const core = model.core;
  const out: Record<string, unknown> = {
    activeKeyboardBindings: core.activeKeyboardBindings,
    activeGamepadBindings: core.activeGamepadBindings,
    defaultZoom: core.defaultZoom,
    sramAutoSave: core.sramAutoSave,
    useNativeFileDialogs: core.useNativeFileDialogs,
    render: core.render,
  };
  const raw = model.raw ?? {};
  const written = model.written ?? [];
  for (const [key, value] of Object.entries(model.sections)) {
    // A block this session did not set goes back as the file carried it: a contributor's parse drops the fields this
    // build does not know, and config.json is shared by builds of different ages (a release plugin beside a newer
    // standalone). Only a key whose value this session authored is written from its parsed form.
    // As an OWN, enumerable property: a hand-edited `__proto__` file key is data this build keeps (configSections.ts's
    // putConfigSection), and assigning it here would set this object's prototype and drop the key from the write.
    Object.defineProperty(out, key, {
      value: Object.prototype.hasOwnProperty.call(raw, key) && !written.includes(key)
        ? raw[key]
        : serializeConfigSectionValue(key, value, contributors),
      enumerable: true,
      writable: true,
      configurable: true,
    });
  }
  return out;
}

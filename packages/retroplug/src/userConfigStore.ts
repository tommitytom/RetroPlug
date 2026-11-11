// UserConfigStore: config.json as the app uses it. Ties the model + serialization to the
// Backend — reads/writes <configDir>/config.json atomically, fills defaults on first run,
// and fires onChange on a real change. Mirrors RecentStore. Every mutation is a no-op
// (no write, no notify) when it doesn't change the value; setters validate and reject bad
// input (matching native UserConfig::setDefaultZoom / setSramMirror).

import type { HostBackend } from "./backend";
import {
  DEFAULT_USER_CONFIG,
  RENDER_MAX_DURATION_MAX_SEC,
  RENDER_MAX_DURATION_MIN_SEC,
  RENDER_ON_EXISTS,
  RENDER_SAMPLE_RATES,
  RENDER_SPLITS,
  SRAM_AUTO_SAVES,
  type ConfigModel,
  type RenderOnExists,
  type RenderSettings,
  type SramAutoSave,
  type UserConfig,
} from "./userConfig";
import type { SplitMode } from "./render";
import { parseUserConfigResult, serializeUserConfig, USER_CONFIG_SCHEMA } from "./userConfigSerialization";
import { warnStampedNewer } from "./migrate";
import {
  assertConfigSectionKey, configSectionDefaults, configSectionEntryOf, defaultConfigSections, putConfigSection,
  readableConfigSection, type ConfigSectionEntry,
} from "./configSections";
import { readSectionVersion } from "./projectConfig";
import { CONFIG_SECTIONS } from "./generated/packages";

const CONFIG_FILE = "config.json";
const enc = new TextEncoder();
const dec = new TextDecoder();

/** A JSON object, which is what a section's value always is. */
function isObject(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

export class UserConfigStore {
  private current: UserConfig = { ...DEFAULT_USER_CONFIG };
  /** Each package's block of the file, by root key: its contributor's parsed value, or the raw JSON of a key this
   *  product contributes no section for. Kept BESIDE the core fields so a section can never shadow one, and written
   *  back in this order (contributors in product order, then unknown keys in the order they were read). */
  private sections: Record<string, unknown>;
  /** Each section's block exactly as the file carried it: what a section this session did not set is written back as. */
  private raw: Record<string, unknown>;
  /** The sections this session has set: the only blocks written from their parsed value. */
  private written = new Set<string>();
  /** The product's config sections, or a test's: what `section` and `setSection` consult. */
  private readonly configSections: readonly ConfigSectionEntry[];
  /** Section keys whose stamped-newer refusal this session already warned about: one warning per key, not one per
   *  write attempt. */
  private refusedSections = new Set<string>();
  // Latched when config.json turns out to be stamped ahead of this build. Refusing to READ such a
  // file is not enough: `current` is then the defaults, and the next setter would persist those over
  // the newer file, so one Settings press on an older build destroys it. Only "newer" latches —
  // a merely malformed file stays writable so it heals on the next change.
  private readOnly = false;
  // Per-system render-filename OVERRIDES — session-only, deliberately NOT persisted (config.json). The
  // Render menu re-derives the default from the loaded song each time; this just remembers a name the user
  // typed for the current session. Keyed by system id; changing it fires onChange so the menu label repaints.
  private renderFilenames = new Map<number, string>();
  // Per-system render OUTPUT-DIR overrides — session-only, deliberately NOT persisted. The Render menu's
  // "Output Dir" row writes here (never config.json), so it defaults to the Settings "Default Render Dir"
  // (else the .sav / ROM folder) but a per-session change never disturbs that saved default.
  private renderDirs = new Map<number, string>();

  /** `configSections` is the product's registry, or a test's: the sections this store parses, reads and writes. */
  constructor(
    private readonly backend: HostBackend,
    private readonly onChange: () => void = () => {},
    configSections: readonly ConfigSectionEntry[] = CONFIG_SECTIONS,
  ) {
    this.configSections = configSections;
    this.sections = defaultConfigSections(configSections);
    this.raw = {};
  }

  /** Read config.json into memory. A parsed value replaces the current config; a missing
   *  file writes the defaults out (first run, so the user has a file to edit); a malformed
   *  / newer file keeps the current defaults. Safe to call once at startup. */
  load(): void {
    const bytes = this.backend.readFile(this.filePath());
    if (!bytes) {
      this.backend.writeFileAtomic(this.filePath(), enc.encode(this.serialize()));
      return;
    }
    const parsed = parseUserConfigResult(dec.decode(bytes), this.configSections);
    if (parsed.ok) this.adopt(parsed.model);
    else if (parsed.reason === "newer") {
      this.readOnly = true;
      warnStampedNewer(CONFIG_FILE, parsed.stamped, USER_CONFIG_SCHEMA);
    }
  }

  /** Re-read config.json after an external change (the file-watch reaction). A missing /
   *  malformed / newer-stamped file keeps the current value (unlike load(), no first-run
   *  default write); a valid, different config replaces it and fires onChange. Returns
   *  whether it changed. */
  reload(): boolean {
    const bytes = this.backend.readFile(this.filePath());
    if (!bytes) return false; // deleted → keep current
    const parsed = parseUserConfigResult(dec.decode(bytes), this.configSections);
    if (!parsed.ok) {
      // An external edit can introduce a newer stamp mid-session (a second machine syncing the
      // folder), so the latch is set here too — not only at load.
      if (parsed.reason === "newer" && !this.readOnly) {
        this.readOnly = true;
        warnStampedNewer(CONFIG_FILE, parsed.stamped, USER_CONFIG_SCHEMA);
      }
      return false; // malformed / newer → keep current
    }
    this.readOnly = false; // a readable stamp is back on disk (replaced/downgraded), so the hazard is gone
    const after = this.serialize(parsed.model);
    if (after === this.serialize()) return false; // no change
    this.adopt(parsed.model);
    this.onChange();
    return true;
  }

  /** The config as config.json carries it: the shell's fields, then each section under its key. A section write's
   *  callers read the file shape through here, so a package's block and the shell's fields cannot disagree about what
   *  is on disk. */
  config(): UserConfig & Readonly<Record<string, unknown>> {
    return { ...this.current, ...this.sections };
  }

  /** A section as its contributor parses it (raw JSON for a root key no package of this product contributes). A key
   *  that is not a section - one of the shell's own keys, or `__proto__` - is refused, not answered with a shell field;
   *  a block stamped newer than its contributor reads comes back as the contributor's defaults. */
  section<T = Record<string, unknown>>(key: string): T {
    assertConfigSectionKey(key);
    return readableConfigSection<T>(this.sections, key, this.configSections);
  }

  /** Apply a patch to a section through its contributor's `merge`, written through a re-read of the file (two
   *  processes may share it). THROWS when the package has no such section or the contributor refuses the patch, and
   *  does nothing for a block stamped newer than its contributor (one warning, and the block stays byte-identical).
   *  The stamp check and the merge run over the RE-READ block, inside the store's apply callback: the block's truth is
   *  the file, and another process may have moved it since this one read it. */
  setSection(key: string, patch: Readonly<Record<string, unknown>>): boolean {
    assertConfigSectionKey(key);
    const entry = configSectionEntryOf(key, this.configSections);
    if (!entry) throw new Error(`${key} is not a config section this product's packages contribute`);
    return this.apply((model) => {
      const stored = model.sections[key];
      const version = entry.section.version;
      const stamped = version !== undefined && isObject(stored) ? readSectionVersion(stored) : 0;
      if (version !== undefined && stamped > version) {
        if (!this.refusedSections.has(key)) {
          this.refusedSections.add(key);
          console.warn(
            `[config] the section ${key} is stamped schemaVersion ${stamped}, this build reads ${version}: leaving it ` +
              "untouched. A newer build wrote it.",
          );
        }
        return model; // unchanged, so the commit writes nothing and the newer block stays byte-identical
      }
      const current = stored === undefined ? configSectionDefaults(entry) : stored;
      const next = entry.section.merge
        ? entry.section.merge(current, patch)
        : entry.section.schema.parse({ ...(isObject(current) ? current : {}), ...patch });
      const sections: Record<string, unknown> = { ...model.sections };
      putConfigSection(sections, key, next);
      return { ...model, sections, written: [...(model.written ?? []), key] };
    });
  }
  defaultZoom(): number {
    return this.current.defaultZoom;
  }
  sramAutoSave(): SramAutoSave {
    return this.current.sramAutoSave;
  }
  render(): RenderSettings {
    return { ...this.current.render };
  }
  /** Set the active keyboard binding profile (a plain name; profile-name format
   *  validation lands with the profiles increment). Returns whether it changed. */
  setActiveKeyboardBindings(name: string): boolean {
    return this.update((c) => ({ ...c, activeKeyboardBindings: name }));
  }

  /** Set the active gamepad binding profile. Returns whether it changed. */
  setActiveGamepadBindings(name: string): boolean {
    return this.update((c) => ({ ...c, activeGamepadBindings: name }));
  }

  /** Set the default zoom. Rejects a non-integer or out-of-range (1..6) value (returns
   *  false, no change), matching native. */
  setDefaultZoom(zoom: number): boolean {
    if (!Number.isInteger(zoom) || zoom < 1 || zoom > 6) return false;
    return this.update((c) => ({ ...c, defaultZoom: zoom }));
  }

  /** Set the loose-.sav auto-save preference. Rejects an unknown mode. */
  setSramAutoSave(mode: SramAutoSave): boolean {
    if (!SRAM_AUTO_SAVES.includes(mode)) return false;
    return this.update((c) => ({ ...c, sramAutoSave: mode }));
  }

  /** Use the host's OS file dialog instead of the in-app browser (where the host provides one). */
  setUseNativeFileDialogs(on: boolean): boolean {
    return this.update((c) => ({ ...c, useNativeFileDialogs: on }));
  }

  // --- render-menu selections (System > Render) ---

  /** Set the render split mode. Rejects an unknown mode. */
  setRenderSplit(split: SplitMode): boolean {
    if (!RENDER_SPLITS.includes(split)) return false;
    return this.commitRender({ split });
  }

  /** Set the render output sample rate. Rejects a rate not in RENDER_SAMPLE_RATES. */
  setRenderSampleRate(sampleRate: number): boolean {
    if (!RENDER_SAMPLE_RATES.includes(sampleRate as never)) return false;
    return this.commitRender({ sampleRate });
  }

  /** Set the max render duration (seconds), clamped to [MIN, MAX]. Always applies the clamped value. */
  setRenderMaxDurationSec(sec: number): boolean {
    const clamped = Math.max(RENDER_MAX_DURATION_MIN_SEC, Math.min(RENDER_MAX_DURATION_MAX_SEC, Math.round(sec)));
    return this.commitRender({ maxDurationSec: clamped });
  }

  /** Set the persisted Settings "Default Render Dir". Any string ("" = unset → the Render menu derives from
   *  the .sav / ROM folder); a no-op when unchanged. NOT the per-session Render "Output Dir" (see setRenderDir). */
  setRenderOutputDir(dir: string): boolean {
    return this.commitRender({ outputDir: dir });
  }

  /** Set the on-existing-file policy (overwrite the target, or write to the next free name). Rejects an
   *  unknown mode. */
  setRenderOnExists(mode: RenderOnExists): boolean {
    if (!RENDER_ON_EXISTS.includes(mode)) return false;
    return this.commitRender({ onExists: mode });
  }

  // --- session-only render filename override (see renderFilenames above) ---

  /** The user's typed render filename for `systemId` this session, or undefined (→ the caller re-derives). */
  renderFilename(systemId: number): string | undefined {
    return this.renderFilenames.get(systemId);
  }

  /** Remember a typed render filename for `systemId` (session-only, not persisted). Fires onChange so the
   *  menu label repaints. */
  setRenderFilename(systemId: number, name: string): void {
    this.renderFilenames.set(systemId, name);
    this.onChange();
  }

  // --- session-only render output-dir override (see renderDirs above) ---

  /** The user's chosen render Output Dir for `systemId` this session, or undefined (→ the caller falls back
   *  to the Settings default, else the .sav / ROM folder). */
  renderDir(systemId: number): string | undefined {
    return this.renderDirs.get(systemId);
  }

  /** Remember a chosen render Output Dir for `systemId` (session-only, NOT persisted — never touches the
   *  Settings "Default Render Dir"). Fires onChange so the menu label repaints. */
  setRenderDir(systemId: number, dir: string): void {
    this.renderDirs.set(systemId, dir);
    this.onChange();
  }

  private commitRender(patch: Partial<RenderSettings>): boolean {
    return this.update((c) => ({ ...c, render: { ...c.render, ...patch } }));
  }

  /** Apply a change to the SHELL's fields. The package blocks ride along untouched - their raw bytes and the keys this
   *  session wrote - because serialising a contributed block from its parsed value drops every field this build does not
   *  know, and config.json is shared with builds of other ages (the verbatim-keep rule). */
  private update(fn: (cfg: UserConfig) => UserConfig): boolean {
    return this.apply((model) => ({ ...model, core: fn(model.core) }));
  }

  /** Apply `fn` to the config AS IT IS ON DISK, then commit. The file is shared - the DAW plugin, another standalone -
   *  and this process only reads it at launch (only the plugin watches it), so a change computed from the in-memory
   *  copy wrote that whole stale copy back: one Connect here reverted every setting the plugin had changed since, and
   *  the plugin's own watcher then adopted the reverts live. This is why a section is patched over the re-read copy
   *  too: a package's block and the shell's fields come from the one file. */
  private apply(fn: (model: ConfigModel) => ConfigModel): boolean {
    this.syncFromDisk();
    return this.commit(fn(this.model()));
  }

  /** The config as the store holds it, with the file's raw blocks and the keys this session set. */
  private model(): ConfigModel {
    return { core: this.current, sections: this.sections, raw: this.raw, written: [...this.written] };
  }

  /** Adopt what is on disk now, as reload() does: a valid, different file replaces the in-memory copy (and notifies); a
   *  newer-stamped one latches read-only; missing or malformed keeps what we have. */
  private syncFromDisk(): void {
    const bytes = this.backend.readFile(this.filePath());
    if (!bytes) return;
    const parsed = parseUserConfigResult(dec.decode(bytes), this.configSections);
    if (!parsed.ok) {
      if (parsed.reason === "newer" && !this.readOnly) {
        this.readOnly = true;
        warnStampedNewer(CONFIG_FILE, parsed.stamped, USER_CONFIG_SCHEMA);
      }
      return;
    }
    this.readOnly = false;
    if (this.serialize(parsed.model) === this.serialize()) return;
    this.adopt(parsed.model);
    this.onChange();
  }

  /** The file's bytes for a model, this one when none is given. */
  private serialize(model: ConfigModel = this.model()): string {
    return serializeUserConfig(model, this.configSections);
  }

  /** Take a parsed model as the current one. */
  private adopt(model: ConfigModel): void {
    this.current = model.core;
    this.sections = { ...model.sections };
    this.raw = { ...(model.raw ?? {}) };
    this.written = new Set(model.written ?? []);
  }

  private filePath(): string {
    return `${this.backend.configDir()}/${CONFIG_FILE}`;
  }

  // Adopt `next` if it differs from the current config: persist atomically + notify.
  private commit(next: ConfigModel): boolean {
    if (this.readOnly) return false; // a newer build wrote this file; ours is not the version that gets to edit it
    const after = this.serialize(next);
    if (after === this.serialize()) return false; // genuine no-op
    // Adopted only once it is on disk. A failed write (on Windows, another process holding the file open without
    // FILE_SHARE_DELETE fails the rename) must not show the change all session and lose it at the next launch.
    if (!this.backend.writeFileAtomic(this.filePath(), enc.encode(after))) return false;
    this.adopt(next);
    this.onChange();
    return true;
  }
}

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
  type RenderOnExists,
  type RenderSettings,
  type SramAutoSave,
  type UserConfig,
} from "./userConfig";
import type { SplitMode } from "./render";
import { parseUserConfig, serializeUserConfig } from "./userConfigSerialization";

const CONFIG_FILE = "config.json";
const enc = new TextEncoder();
const dec = new TextDecoder();

export class UserConfigStore {
  private current: UserConfig = { ...DEFAULT_USER_CONFIG };
  // Per-system render-filename OVERRIDES — session-only, deliberately NOT persisted (config.json). The
  // Render menu re-derives the default from the loaded song each time; this just remembers a name the user
  // typed for the current session. Keyed by system id; changing it fires onChange so the menu label repaints.
  private renderFilenames = new Map<number, string>();
  // Per-system render OUTPUT-DIR overrides — session-only, deliberately NOT persisted. The Render menu's
  // "Output Dir" row writes here (never config.json), so it defaults to the Settings "Default Render Dir"
  // (else the .sav / ROM folder) but a per-session change never disturbs that saved default.
  private renderDirs = new Map<number, string>();

  constructor(private readonly backend: HostBackend, private readonly onChange: () => void = () => {}) {}

  /** Read config.json into memory. A parsed value replaces the current config; a missing
   *  file writes the defaults out (first run, so the user has a file to edit); a malformed
   *  / newer file keeps the current defaults. Safe to call once at startup. */
  load(): void {
    const bytes = this.backend.readFile(this.filePath());
    if (!bytes) {
      this.backend.writeFileAtomic(this.filePath(), enc.encode(serializeUserConfig(this.current)));
      return;
    }
    const parsed = parseUserConfig(dec.decode(bytes));
    if (parsed) this.current = parsed;
  }

  /** Re-read config.json after an external change (the file-watch reaction). A missing /
   *  malformed / newer-stamped file keeps the current value (unlike load(), no first-run
   *  default write); a valid, different config replaces it and fires onChange. Returns
   *  whether it changed. */
  reload(): boolean {
    const bytes = this.backend.readFile(this.filePath());
    if (!bytes) return false; // deleted → keep current
    const parsed = parseUserConfig(dec.decode(bytes));
    if (!parsed) return false; // malformed / newer → keep current
    if (serializeUserConfig(parsed) === serializeUserConfig(this.current)) return false; // no change
    this.current = parsed;
    this.onChange();
    return true;
  }

  /** A copy of the current config. */
  config(): UserConfig {
    return { ...this.current };
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
    return this.commit({ ...this.current, activeKeyboardBindings: name });
  }

  /** Set the active gamepad binding profile. Returns whether it changed. */
  setActiveGamepadBindings(name: string): boolean {
    return this.commit({ ...this.current, activeGamepadBindings: name });
  }

  /** Set the default zoom. Rejects a non-integer or out-of-range (1..6) value (returns
   *  false, no change), matching native. */
  setDefaultZoom(zoom: number): boolean {
    if (!Number.isInteger(zoom) || zoom < 1 || zoom > 6) return false;
    return this.commit({ ...this.current, defaultZoom: zoom });
  }

  /** Set the loose-.sav auto-save preference. Rejects an unknown mode. */
  setSramAutoSave(mode: SramAutoSave): boolean {
    if (!SRAM_AUTO_SAVES.includes(mode)) return false;
    return this.commit({ ...this.current, sramAutoSave: mode });
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
    return this.commit({ ...this.current, render: { ...this.current.render, ...patch } });
  }

  private filePath(): string {
    return `${this.backend.configDir()}/${CONFIG_FILE}`;
  }

  // Adopt `next` if it differs from the current config: persist atomically + notify.
  private commit(next: UserConfig): boolean {
    const after = serializeUserConfig(next);
    if (after === serializeUserConfig(this.current)) return false; // genuine no-op
    this.current = next;
    this.backend.writeFileAtomic(this.filePath(), enc.encode(after));
    this.onChange();
    return true;
  }
}

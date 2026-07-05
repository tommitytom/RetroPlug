// UserConfigStore: config.json as the app uses it. Ties the model + serialization to the
// Backend — reads/writes <configDir>/config.json atomically, fills defaults on first run,
// and fires onChange on a real change. Mirrors RecentStore. Every mutation is a no-op
// (no write, no notify) when it doesn't change the value; setters validate and reject bad
// input (matching native UserConfig::setDefaultZoom / setSramMirror).

import type { Backend } from "./backend";
import { DEFAULT_USER_CONFIG, SRAM_AUTO_SAVES, type SramAutoSave, type UserConfig } from "./userConfig";
import { parseUserConfig, serializeUserConfig } from "./userConfigSerialization";

const CONFIG_FILE = "config.json";
const enc = new TextEncoder();
const dec = new TextDecoder();

export class UserConfigStore {
  private current: UserConfig = { ...DEFAULT_USER_CONFIG };

  constructor(private readonly backend: Backend, private readonly onChange: () => void = () => {}) {}

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

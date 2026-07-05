// BindingsStore: the bindings/<name>.json profiles as the app manages them. Ties the
// model + serialization to the Backend (read/write/rename/delete/list under
// <configDir>/bindings) and to UserConfigStore (which names the active profiles). Ports
// native UserConfig's profile CRUD: load / save / rename / delete, name validation,
// enumeration, the first-run default, and the synthesized "resolved" bindings.
//
// The config.json side (active names, zoom, sram) lives in UserConfigStore; renaming or
// resolving profiles reads/updates the active names through it.

import type { Backend } from "./backend";
import type { UserConfigStore } from "./userConfigStore";
import { defaultBindingMap, type BindingMap } from "./bindingMap";
import { parseBindingMap, serializeBindingMap } from "./bindingSerialization";
import { joinPath, stem, extensionLower } from "./pathUtil";

const BINDINGS_DIR = "bindings";
const RESERVED_PROFILE_STEM = "config"; // would collide with config.json
const enc = new TextEncoder();
const dec = new TextDecoder();

/** A valid profile name: non-empty, only [A-Za-z0-9_-], and not the reserved `config`
 *  stem. Pure — exposed so a UI can validate without attempting a write (native
 *  UserConfig::isValidProfileName). */
export function isValidProfileName(name: string): boolean {
  if (!name || name === RESERVED_PROFILE_STEM) return false;
  return /^[A-Za-z0-9_-]+$/.test(name);
}

export class BindingsStore {
  constructor(
    private readonly backend: Backend,
    private readonly userConfig: UserConfigStore,
    private readonly onChange: () => void = () => {},
  ) {}

  /** First run: write bindings/default.json from defaultBindingMap() if it's absent, so
   *  the active "default" profile resolves and shows up in the list. */
  ensureDefaults(): void {
    const p = this.profilePath("default");
    if (!this.backend.fileExists(p)) {
      this.backend.writeFileAtomic(p, enc.encode(serializeBindingMap(defaultBindingMap())));
    }
  }

  /** Every profile name under bindings/ (each `*.json` stem), sorted. */
  availableProfiles(): string[] {
    return this.backend
      .listDir(this.bindingsDir())
      .filter((n) => extensionLower(n) === ".json")
      .map((n) => stem(n))
      .sort();
  }

  /** Read + parse one profile, or null when the name is invalid / the file is
   *  missing / malformed / stamped newer than us. */
  loadProfile(name: string): BindingMap | null {
    if (!isValidProfileName(name)) return null;
    const bytes = this.backend.readFile(this.profilePath(name));
    if (!bytes) return null;
    return parseBindingMap(dec.decode(bytes));
  }

  /** Overwrite (or create) bindings/<name>.json, forcing its embedded name to match the
   *  filename. Returns false on an invalid name / write failure. */
  saveProfile(name: string, map: BindingMap): boolean {
    if (!isValidProfileName(name)) return false;
    const ok = this.backend.writeFileAtomic(this.profilePath(name), enc.encode(serializeBindingMap({ ...map, name })));
    if (ok) this.onChange();
    return ok;
  }

  /** Rename a profile file. Refuses an invalid name, a missing source, or an existing
   *  destination (no clobber). Rewrites the file's embedded `name` to match (cosmetic),
   *  and repoints config.json's active refs if the renamed profile was active. */
  renameProfile(oldName: string, newName: string): boolean {
    if (!isValidProfileName(oldName) || !isValidProfileName(newName)) return false;
    if (oldName === newName) return true;
    const srcPath = this.profilePath(oldName);
    const dstPath = this.profilePath(newName);
    if (!this.backend.fileExists(srcPath)) return false;
    if (this.backend.fileExists(dstPath)) return false; // refuse to clobber

    const map = this.loadProfile(oldName); // rewrite the embedded name only if it parses
    if (map) this.backend.writeFileAtomic(srcPath, enc.encode(serializeBindingMap({ ...map, name: newName })));
    if (!this.backend.rename(srcPath, dstPath)) return false;

    const active = this.userConfig.config();
    if (active.activeKeyboardBindings === oldName) this.userConfig.setActiveKeyboardBindings(newName);
    if (active.activeGamepadBindings === oldName) this.userConfig.setActiveGamepadBindings(newName);
    this.onChange();
    return true;
  }

  /** Delete a profile file. Refuses an invalid name, the currently-active keyboard or
   *  gamepad profile (switch first), or a missing file. */
  deleteProfile(name: string): boolean {
    if (!isValidProfileName(name)) return false;
    const active = this.userConfig.config();
    if (name === active.activeKeyboardBindings || name === active.activeGamepadBindings) return false;
    const p = this.profilePath(name);
    if (!this.backend.fileExists(p)) return false;
    if (!this.backend.deleteFile(p)) return false;
    this.onChange();
    return true;
  }

  /** The active bindings: `.keyboard` from the active keyboard profile, `.gamepad` from
   *  the active gamepad profile (they may be the same file), each falling back to the
   *  default when its profile is absent/unreadable. Computed on demand. */
  resolvedBindings(): BindingMap {
    const active = this.userConfig.config();
    const def = defaultBindingMap();
    const kb = this.loadProfile(active.activeKeyboardBindings);
    const gp = this.loadProfile(active.activeGamepadBindings);
    return {
      name: active.activeKeyboardBindings,
      keyboard: kb ? kb.keyboard : def.keyboard,
      gamepad: gp ? gp.gamepad : def.gamepad,
    };
  }

  private bindingsDir(): string {
    return joinPath(this.backend.configDir(), BINDINGS_DIR);
  }
  private profilePath(name: string): string {
    return joinPath(this.bindingsDir(), name + ".json");
  }
}

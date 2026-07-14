// SystemsStore: the live systems list as the app owns it. TS is the source of truth
// for order, ids-as-handles, focus, and dirty; the Backend is a narrow emulator-
// lifecycle service (build / clone / reload / drop) that takes CONCRETE paths TS has
// already resolved. All the orchestration lives here: classify a ROM (TS-side, from
// its header), disambiguate a sav suffix against the live list, resolve a paired-sav
// override, pick a sibling ROM, and apply the load/add/replace + focus rules.
//
// Mirrors RecentStore's shape (constructor(backend, onChange), view(), mutators,
// no-op-guarded change signal). Reproduces PluginRpcService's constructSystem /
// duplicateSystem / reload orchestration and the DSP list handlers
// (PluginDSP.cpp:406-458), with every path derived by the pure kernels.

import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";
import { resolveSavPath, siblingSavPath, siblingRplgPath, nextFreeSavSuffix } from "./savPaths";
import {
  type SystemEntry,
  findById,
  appendEntry,
  removeById,
  replaceById,
  isSuffixOwned,
  resolveSavOverride,
  pickSiblingRom,
  nextFocusAfterRemove,
} from "./systemsList";
import { type CommonSettings, DEFAULT_COMMON_SETTINGS, clampGain, commonSettingsSchema } from "./systemSettings";
import type { RoleRegistry, RoleInstance } from "./systemRoles";
import { roleConfigForNative, LsdjSyncMode } from "./settingsEnums";

// How much ROM header to read for the role providers (title lives at 0x134).
const ROLE_HEADER_LEN = 0x150;

// TS owns the system-id counter (native never allocates). Module-scoped so it's ONE id space per
// control-plane JS context — which is 1:1 with the native Project in every host (a plugin instance has
// its own JS context + Project; a native test file shares one host + Project across its cases). Ids are
// opaque handles; the snapshot registry uses 0 as its free-slot sentinel, so they start at 1.
let nextSystemId = 1;
function allocSystemId(): number {
  return nextSystemId++;
}

// New SRAM seeds a blank battery: a generous 128 KiB of zeros (a safe upper bound across GB/NES/GBA save
// sizes). Each core's onActivate truncates or zero-pads it to the cart's real battery size (SameBoy via
// GB_save_battery_size, Mesen via the NesSaveRam region), so any non-empty all-zero buffer blanks the SRAM.
const BLANK_SRAM_BYTES = 0x20000;

/** Classify a ROM's platform from its header only — the one place ROM bytes enter TS, and just
 *  the first `ROM_SNIFF_LEN` of them. Native never classifies. */
export function classifyRom(backend: HostBackend, romPath: string): Platform | "unknown" {
  return detectPlatform(backend.readFilePrefix(romPath, ROM_SNIFF_LEN) ?? new Uint8Array());
}

/** A system as the UI sees it: identity + live `focused`/`missing` flags + the
 *  universal settings + the generic roles (the UI iterates roles + registry
 *  descriptors — no backend/LSDj fields are hardcoded here). */
export interface SystemView {
  id: number;
  platform: Platform;
  core: Core;
  romPath: string;
  savPath: string; // the override ("" = derived from suffix)
  savSuffix: number;
  embedded: boolean;
  battery: boolean; // the cart has battery-backed save memory (a real .sav target)
  focused: boolean;
  missing: boolean;
  settings: CommonSettings;
  roles: RoleInstance[];
}

/** loadRom outcome: defer to a sibling project, a built system, or a failure. */
export type LoadResult = { deferredProject: string } | { system: number } | null;

export class SystemsStore {
  private entries: SystemEntry[] = [];
  private focusedId = 0;
  private dirty = false;
  private onFocusChange: () => void = () => {};

  constructor(
    private readonly backend: ControlPlaneBackend,
    private readonly onChange: () => void = () => {},
    private readonly registry?: RoleRegistry,
  ) {}

  /** Snapshot for the UI, with live focus + missing flags. */
  view(): SystemView[] {
    return this.entries.map((e) => ({
      id: e.id,
      platform: e.platform,
      core: e.core,
      romPath: e.romPath,
      savPath: e.savPath,
      savSuffix: e.savSuffix,
      embedded: e.embeddedRom !== "",
      battery: e.battery,
      focused: e.id === this.focusedId,
      missing: e.romPath !== "" && e.embeddedRom === "" && !this.backend.fileExists(e.romPath),
      settings: e.settings ?? DEFAULT_COMMON_SETTINGS,
      roles: e.roles ?? [],
    }));
  }

  systems(): SystemEntry[] {
    return this.entries.slice();
  }
  focused(): number {
    return this.focusedId;
  }
  isDirty(): boolean {
    return this.dirty;
  }

  /** Install the observer fired on a focus change (setFocus). A NON-dirtying UI signal — focus is
   *  transient (not persisted), so it re-renders the tiles without marking the project dirty or
   *  re-projecting the DSP (distinct from the structural onChange). */
  setOnFocusChange(fn: () => void): void {
    this.onFocusChange = fn;
  }

  /** Focus system `id` when it exists. Returns whether focus changed. Transient UI state: notifies for
   *  a re-render but does not mark the project dirty. */
  setFocus(id: number): boolean {
    if (id === this.focusedId) return false;
    if (!this.entries.some((e) => e.id === id)) return false;
    this.focusedId = id;
    this.onFocusChange();
    return true;
  }

  /** Move focus to the next (`dir=1`) or previous (`dir=-1`) instance in grid order, wrapping around. The
   *  "cycle instances" app action. No-op with fewer than two instances; a stale/absent focus starts at the
   *  front. Reuses setFocus, so it's transient + fires the same re-render signal. */
  focusNext(dir: 1 | -1): boolean {
    const n = this.entries.length;
    if (n < 2) return false; // n===0 would make % 0 NaN; a single instance has nowhere to go
    let idx = this.entries.findIndex((e) => e.id === this.focusedId);
    if (idx < 0) idx = 0; // no live focus → start at the front
    return this.setFocus(this.entries[(idx + dir + n) % n].id); // +n so dir=-1 doesn't go negative in JS
  }

  /** Append a new instance of `romPath`, disambiguating its sav suffix against the
   *  live list. Returns the new id, or null when the ROM is unknown/unreadable. */
  addSystem(romPath: string, opts?: { explicitSav?: string }): number | null {
    const suffix = this.freeSuffix(romPath);
    const built = this.construct(romPath, "", suffix, opts?.explicitSav);
    if (!built) return null;
    const wasEmpty = this.entries.length === 0;
    this.entries = appendEntry(this.entries, built.entry);
    if (wasEmpty) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Load `romPath`, replacing the focused tile (or adopting into an empty project).
   *  With no paired save, defers to a sibling `<rom>.rplg` when one exists. */
  loadRom(romPath: string, opts?: { explicitSav?: string }): LoadResult {
    if (!opts?.explicitSav) {
      const rplg = siblingRplgPath(romPath);
      if (this.backend.fileExists(rplg)) return { deferredProject: rplg };
    }
    const id = this.loadInPlace(romPath, "", opts?.explicitSav);
    return id === null ? null : { system: id };
  }

  /** Load the binary-baked mGB (no file, no dialog), replacing the focused tile. */
  loadMgb(): number | null {
    return this.loadInPlace("", "mgb", undefined);
  }

  /** Swap a specific system for `romPath`, in place. */
  replaceSystem(id: number, romPath: string, opts?: { explicitSav?: string }): number | null {
    if (!findById(this.entries, id)) return null;
    const built = this.construct(romPath, "", 0, opts?.explicitSav, id);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, built.entry);
    if (this.focusedId === id) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Swap system `id`'s ROM for `romPath` in place, carrying its LIVE battery SRAM forward — the ROM
   *  change that keeps your save (e.g. bumping LSDj to a new version without losing the song). Unlike
   *  replaceSystem (fresh boot, sav from disk), this seeds the running SRAM as the new cart's cold-boot
   *  battery. The new ROM is classified afresh, so platform/core/roles/battery follow it; identity + focus
   *  are preserved. The auto-save target follows the new ROM (its sibling `<rom>.sav`, suffix 0), so — like
   *  New SRAM / Load SRAM — the carried battery is what's saved there on the next battery write. Null when
   *  `id` is absent or the ROM won't classify/build. */
  swapRom(id: number, romPath: string): number | null {
    if (!findById(this.entries, id)) return null;
    const sramBytes = this.backend.readSram(id) ?? undefined;
    const built = this.construct(romPath, "", 0, undefined, id, sramBytes);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, built.entry);
    if (this.focusedId === id) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Clone a system's LIVE state into a fresh appended instance with its own suffix. Orchestrated in
   *  TS: pull the source's savestate from the registry (it includes SRAM), then build an INDEPENDENT
   *  core seeded from those bytes — the source's role config crosses as the construct settings blob so
   *  the clone boots the same model the savestate was captured under. No native duplicate method. */
  duplicateSystem(id: number): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const state = this.backend.readState(id);
    if (!state) {
      console.warn(`[systems] duplicateSystem(${id}) failed (no published state) — no instance added`);
      return null;
    }
    const suffix = this.freeSuffix(src.romPath);
    const savPath = src.embeddedRom ? null : resolveSavPath(src.romPath, suffix, "");
    const systemRole = src.roles.find((r) => r.kind === src.core);
    const newId = allocSystemId();
    const ok = this.backend.constructSystem({
      romPath: src.romPath,
      platform: src.platform,
      core: src.core,
      embeddedRom: src.embeddedRom,
      savPath,
      statePath: null,
      stateBytes: state,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, newId);
    if (!ok) {
      console.warn(`[systems] duplicateSystem(${id}) failed (construct returned false) — no instance added`);
      return null;
    }
    this.entries = appendEntry(this.entries, {
      id: newId,
      platform: src.platform,
      core: src.core,
      romPath: src.romPath,
      savPath: "",
      savSuffix: suffix,
      embeddedRom: src.embeddedRom,
      battery: src.battery, // same ROM as the source
      settings: { ...src.settings },
      roles: src.roles.map((r) => ({ kind: r.kind, config: { ...r.config } })),
    });
    return this.committed(newId);
  }

  /** Drop a system; refocus the front if it was focused. False when absent. */
  removeSystem(id: number): boolean {
    if (!findById(this.entries, id)) return false;
    this.backend.removeSystem(id);
    this.entries = removeById(this.entries, id);
    this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
    this.markDirty();
    return true;
  }

  /** Rebuild a system's ROM from disk, carrying its battery SRAM forward, swapping in place with a new
   *  id and preserving identity + focus. Orchestrated in TS: pull SRAM from the registry, then cold-boot
   *  the ROM with it (no savestate) via a replaceId construct. No native reload method. */
  reloadSystem(id: number): number | null {
    if (!findById(this.entries, id)) return null;
    return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined });
  }

  /** Reconstruct system `id` in place from seed bytes: capture the source's spec, build a fresh core
   *  under a new id that swaps the old one (replaceId), and keep identity + focus. The seed (sramBytes
   *  = cold-boot battery, stateBytes = boot from savestate) overrides what native would read from disk;
   *  savPath stays the auto-save target. The shared body of reload / loadState / loadSram — the in-place
   *  twin of duplicateSystem (which appends). */
  private rebuildInPlace(id: number, seed: { sramBytes?: Uint8Array; stateBytes?: Uint8Array }): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const systemRole = src.roles.find((r) => r.kind === src.core);
    const newId = allocSystemId();
    const ok = this.backend.constructSystem({
      romPath: src.romPath,
      platform: src.platform,
      core: src.core,
      embeddedRom: src.embeddedRom,
      savPath: src.embeddedRom ? null : resolveSavPath(src.romPath, src.savSuffix, src.savPath),
      statePath: null,
      sramBytes: seed.sramBytes,
      stateBytes: seed.stateBytes,
      replaceId: id,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, newId);
    if (!ok) return null;
    this.entries = replaceById(this.entries, id, { ...src, id: newId });
    if (this.focusedId === id) this.focusedId = newId;
    return this.committed(newId);
  }

  /** Dump system `id`'s live savestate to `path`. The registry read is safe while the audio thread runs.
   *  False when nothing has been published for the id, or the write fails. A disk dump — no project-state
   *  change (like export). */
  saveState(id: number, path: string): boolean {
    const bytes = this.backend.readState(id);
    if (!bytes) return false;
    return this.backend.writeFileAtomic(path, bytes);
  }

  /** Dump system `id`'s battery SRAM to `path`. False when the id has no SRAM published, or the write fails. */
  saveSram(id: number, path: string): boolean {
    const bytes = this.backend.readSram(id);
    if (!bytes) return false;
    return this.backend.writeFileAtomic(path, bytes);
  }

  /** Load a savestate file into system `id`: reconstruct the core in place, booted from those bytes
   *  (reconstructs rather than live-injecting). Null when the file is unreadable or the build
   *  fails. */
  loadState(id: number, path: string): number | null {
    const bytes = this.backend.readFile(path);
    if (!bytes) return null;
    return this.rebuildInPlace(id, { stateBytes: bytes });
  }

  /** Load a battery SRAM file into system `id`: cold-boot the ROM in place with those bytes (as reload
   *  does with the carried battery). Null when the file is unreadable or the build fails. */
  loadSram(id: number, path: string): number | null {
    const bytes = this.backend.readFile(path);
    if (!bytes) return null;
    return this.rebuildInPlace(id, { sramBytes: bytes });
  }

  /** Reboot system `id` in place, carrying its live battery SRAM forward — a hardware-style reset: the
   *  save persists, the running state is dropped. Reconstructs from the ROM (the same engine
   *  as reload) rather than a live GB_reset. Null when `id` is absent. */
  reset(id: number): number | null {
    if (!findById(this.entries, id)) return null;
    return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined });
  }

  /** Wipe system `id`'s battery SRAM (a fresh cartridge) and cold-boot it in place. Seeds an all-zero
   *  battery — native truncates/zero-pads it to the cart's real size — so the live core reads blank SRAM;
   *  the on-disk `.sav` follows on the next battery save. Null when `id` is absent. */
  newSram(id: number): number | null {
    if (!findById(this.entries, id)) return null;
    return this.rebuildInPlace(id, { sramBytes: new Uint8Array(BLANK_SRAM_BYTES) });
  }

  /** The sibling ROM for a picked `.sav`, or null — the pairing helper. */
  resolveSiblingRom(savPath: string): string | null {
    return pickSiblingRom(
      savPath,
      (p) => this.backend.fileExists(p),
      (p) => classifyRom(this.backend, p),
    );
  }

  // --- per-system settings + roles ----------------------------------------

  /** Set a system's audio gain (dB, clamped); applies to the live emulator. */
  setGain(id: number, db: number): boolean {
    return this.applySetting(id, "gainDb", clampGain(db));
  }

  /** Toggle reload-on-ROM-change; applies to the live emulator. */
  setReloadOnRomChange(id: number, on: boolean): boolean {
    return this.applySetting(id, "reloadOnRomChange", on);
  }

  /** Edit a role's config (validated/clamped via its RoleType). A "system" role's
   *  config is applied to the live emulator; a "feature" role's config is pure TS
   *  (its behaviour is the deferred script future). False when the role is absent. */
  setRoleConfig(id: number, roleKind: string, partial: Record<string, unknown>): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    const idx = e.roles.findIndex((r) => r.kind === roleKind);
    if (idx < 0) return false;
    const rt = this.registry?.roleType(roleKind);
    const merged = { ...e.roles[idx].config, ...partial };
    const config = rt ? rt.schema.parse(merged) : merged;
    if (rt?.category === "system") this.backend.applyRoleConfig(id, roleKind, roleConfigForNative(roleKind, config));
    const roles = e.roles.slice();
    roles[idx] = { kind: roleKind, config };
    this.entries = replaceById(this.entries, id, { ...e, roles });
    // A feature role stays pure TS, EXCEPT lsdj-sync's serial-out capture gate — arm/disarm it on a
    // MIDIOUT mode change (the decoder itself runs in the kernel via the re-projection below).
    if (roleKind === "lsdj-sync") this.syncSerialOutCapture(id);
    this.markDirty();
    return true;
  }

  /** Link a freshly-created instance to the one it was created from: the child joins the parent's link group,
   *  and if the parent is ungrouped (0) the parent is promoted to group 1 so the pair is linked. A no-op unless
   *  BOTH are SameBoy (GB) systems — link groups are a Game Boy link-cable concept. Routes through
   *  setRoleConfig, so native-apply + dirty + re-projection are handled. */
  inheritLinkGroup(childId: number, parentId: number): void {
    const parentSb = findById(this.entries, parentId)?.roles.find((r) => r.kind === "sameboy");
    const childSb = findById(this.entries, childId)?.roles.find((r) => r.kind === "sameboy");
    if (!parentSb || !childSb) return;
    const g = (parentSb.config as { linkGroupId?: number }).linkGroupId ?? 0;
    const target = g > 0 ? g : 1;
    if (g === 0) this.setRoleConfig(parentId, "sameboy", { linkGroupId: target }); // promote the lone parent
    this.setRoleConfig(childId, "sameboy", { linkGroupId: target }); // child joins the parent's group
  }

  // A universal-setting change: emulator-apply + update + dirty. False when absent.
  private applySetting(id: number, key: keyof CommonSettings, value: number | boolean): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    this.backend.applySystemSetting(id, key, value);
    this.entries = replaceById(this.entries, id, {
      ...e,
      settings: { ...e.settings, [key]: value } as CommonSettings,
    });
    this.markDirty();
    return true;
  }

  // --- project-load rebuild seam ------------------------------------------
  // clear()/adopt() are QUIET: no dirty/onChange, since a load isn't a user edit
  // (the ProjectStore sets dirty explicitly around load/new).

  /** Tear down every system + reset the list/focus (for `new` + before a load). */
  clear(): void {
    for (const e of this.entries) this.backend.removeSystem(e.id);
    this.entries = [];
    this.focusedId = 0;
  }

  /** Reconstruct one system from a serialized config entry, preserving its EXACT
   *  savSuffix + savPath-override (no free-suffix reassignment). Appends; focuses the
   *  first. Returns the new id, or null when the ROM won't classify. `blobs` (present
   *  only for a zip-import) seed the emulator's SRAM/savestate from the archive instead
   *  of from disk; `savPath` stays the auto-save target. */
  adopt(
    config: {
      core?: Core; // persisted since the v2 project schema (else auto-derived from platform)
      romPath?: string;
      savPath?: string;
      savSuffix?: number;
      embeddedRom?: string;
      settings?: Partial<CommonSettings>;
      roles?: RoleInstance[];
    },
    blobs?: { sramBytes?: Uint8Array; stateBytes?: Uint8Array },
  ): number | null {
    const embeddedRom = config.embeddedRom ?? "";
    const romPath = config.romPath ?? "";
    const savSuffix = config.savSuffix ?? 0;
    const override = config.savPath ?? "";
    let platform: Platform;
    if (embeddedRom) {
      platform = "gb";
    } else {
      const fmt = classifyRom(this.backend, romPath);
      if (fmt === "unknown") return null;
      platform = fmt;
    }
    // Prefer the persisted core (v2+); fall back to auto-derive for a pre-migration/hand-built entry.
    const core = config.core ?? defaultCoreFor(platform);
    const savPath = embeddedRom ? null : resolveSavPath(romPath, savSuffix, override);
    // Stored roles win; a config that omits them re-attaches defaults. Known before the build so a
    // role's load-time hook can seed the spec (e.g. an empty LSDj sav) before instantiation.
    const roles = config.roles && config.roles.length ? config.roles : this.defaultRoles(core, platform, romPath, embeddedRom);
    // The core-config "system" role (kind === core) carries the emulator config; pass it as the
    // construct-time settings blob so a loaded non-default model/highpass is applied AT build, not via
    // a post-construct restart that would nuke the just-restored savestate.
    const systemRole = config.roles?.find((r) => r.kind === core);
    const id = allocSystemId();
    const spec = this.applyConstructHooks({
      romPath,
      platform,
      core,
      embeddedRom,
      savPath,
      statePath: null,
      sramBytes: blobs?.sramBytes,
      stateBytes: blobs?.stateBytes,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, roles);
    if (!this.backend.constructSystem(spec, id)) return null;
    // Stored settings/roles win; a config that omits them re-attaches defaults.
    const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
    const wasEmpty = this.entries.length === 0;
    const battery = this.detectBattery(romPath, embeddedRom, platform);
    this.entries = appendEntry(this.entries, { id, platform, core, romPath, savPath: override, savSuffix, embeddedRom, battery, settings, roles });
    if (wasEmpty) this.focusedId = id;
    this.syncSerialOutCapture(id); // a loaded project may carry a system already in MIDIOUT mode
    return id;
  }

  // --- internals ----------------------------------------------------------

  // Replace the focused tile (or adopt into an empty project), focusing the result.
  private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
    const empty = this.entries.length === 0;
    const target = empty ? undefined : this.effectiveFocus();
    const built = this.construct(romPath, embeddedRom, 0, explicitSav, target);
    if (!built) return null;
    this.entries = empty
      ? appendEntry(this.entries, built.entry)
      : replaceById(this.entries, target as number, built.entry);
    this.focusedId = built.id;
    return this.committed(built.id);
  }

  // Classify (unless embedded), resolve the concrete savPath, and build via native.
  // Returns the new id + the thin entry to record, or null when rejected/failed.
  private construct(
    romPath: string,
    embeddedRom: string,
    suffix: number,
    explicitSav: string | undefined,
    replaceId?: number,
    sramBytes?: Uint8Array, // seed the cold-boot battery (swap-ROM-preserve-SRAM); undefined = native reads from disk
  ): { id: number; entry: SystemEntry } | null {
    let platform: Platform;
    if (embeddedRom) {
      platform = "gb"; // embedded ROMs are always Game Boy
    } else {
      const fmt = classifyRom(this.backend, romPath);
      if (fmt === "unknown") return null;
      platform = fmt;
    }
    const core = defaultCoreFor(platform);
    const override = resolveSavOverride(romPath, suffix, explicitSav ?? "", (p) =>
      this.backend.canonicalize(p),
    );
    const savPath = embeddedRom ? null : resolveSavPath(romPath, suffix, override);
    const id = allocSystemId();
    // Roles are known before the build (a pure function of core/platform/header), so a role's
    // load-time hook can seed the spec — e.g. an empty LSDj sav — before the core is instantiated.
    const roles = this.defaultRoles(core, platform, romPath, embeddedRom);
    const spec = this.applyConstructHooks(
      { romPath, platform, core, embeddedRom, savPath, statePath: null, replaceId, sramBytes },
      roles,
    );
    if (!this.backend.constructSystem(spec, id)) return null;
    return {
      id,
      entry: {
        id,
        platform,
        core,
        romPath,
        savPath: override,
        savSuffix: suffix,
        embeddedRom,
        battery: this.detectBattery(romPath, embeddedRom, platform),
        settings: { ...DEFAULT_COMMON_SETTINGS },
        roles,
      },
    };
  }

  // The default roles for a freshly-built system: the core's config role + any feature
  // roles the registry's providers suggest for this ROM's header. Empty when no registry
  // is wired (back-compat) or for an embedded ROM (no file to sniff).
  private defaultRoles(core: Core, platform: Platform, romPath: string, embeddedRom: string): RoleInstance[] {
    if (!this.registry) return [];
    const header =
      romPath && !embeddedRom ? this.backend.readFilePrefix(romPath, ROLE_HEADER_LEN) ?? new Uint8Array() : new Uint8Array();
    return this.registry.defaultRoles(core, platform, header, embeddedRom);
  }

  // Whether this cart has battery-backed save memory, derived from the ROM header (embedded/missing → false).
  // Not serialized — re-derived at every build so a loaded project reflects the on-disk ROM. Gates the UI's
  // "Save SRAM" affordances (a battery-less cart would only write a stray empty .sav).
  private detectBattery(romPath: string, embeddedRom: string, platform: Platform): boolean {
    if (!romPath || embeddedRom) return false;
    const header = this.backend.readFilePrefix(romPath, ROLE_HEADER_LEN) ?? new Uint8Array();
    return romHasBattery(header, platform);
  }

  // Fold each attached role's load-time hook over the tentative spec, letting a role seed data that
  // is otherwise absent (e.g. a fresh LSDj ROM gets a valid empty sav so it skips its self-test).
  // Additive by contract — a hook returns the spec unchanged when the data it would seed is present.
  // The backend satisfies ConstructCaps structurally (savFromJson + fileExists), so it's the caps arg.
  private applyConstructHooks(spec: ConstructSpec, roles: RoleInstance[]): ConstructSpec {
    if (!this.registry) return spec;
    let s = spec;
    for (const r of roles) {
      const rt = this.registry.roleType(r.kind);
      if (rt?.onConstruct) s = rt.onConstruct(s, this.backend);
    }
    return s;
  }

  // The free suffix for a new instance of `romPath`: live-list ownership + on-disk.
  private freeSuffix(romPath: string): number {
    return nextFreeSavSuffix(
      romPath,
      (n) => isSuffixOwned(this.entries, romPath, n),
      (n) => this.backend.fileExists(siblingSavPath(romPath, n)),
    );
  }

  // The focused id, falling back to the front when focus is stale (DSP LoadRom rule).
  private effectiveFocus(): number {
    return findById(this.entries, this.focusedId) ? this.focusedId : this.entries[0].id;
  }

  private markDirty(): void {
    this.dirty = true;
    this.onChange();
  }

  // Finish a successful mutation: mark dirty + notify, return the id.
  private committed(id: number): number {
    this.syncSerialOutCapture(id); // a freshly (re)built core defaults to unarmed — re-establish the gate
    this.markDirty();
    return id;
  }

  // Arm/disarm native serial-out capture (LSDj MI.OUT) for a system from its lsdj-sync mode. Both
  // LSDj→host MIDI-out modes need it: MidiOut (SYNC=MI.OUT) and MasterSync (SYNC=LSDJ). Called wherever
  // a core is (re)built (committed/adopt — a fresh core defaults to unarmed) and when the mode changes
  // (setRoleConfig). A no-op for a non-LSDj system (no lsdj-sync role → nothing to arm).
  private syncSerialOutCapture(id: number): void {
    const lsdj = findById(this.entries, id)?.roles.find((r) => r.kind === "lsdj-sync");
    if (!lsdj) return;
    const mode = (lsdj.config as { mode?: LsdjSyncMode }).mode;
    this.backend.setSerialOutCapture(id, mode === LsdjSyncMode.MidiOut || mode === LsdjSyncMode.MasterSync);
  }
}

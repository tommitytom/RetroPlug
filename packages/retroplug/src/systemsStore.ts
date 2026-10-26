// SystemsStore: the live systems list as the app owns it. TS is the source of truth
// for order, ids-as-handles, focus, and dirty; the Backend is a narrow emulator-
// lifecycle service (build a core, and place what has been built) that takes CONCRETE
// paths TS has already resolved. All the orchestration lives here: classify a ROM (TS-side, from
// its header), disambiguate a sav suffix against the live list, resolve a paired-sav
// override, pick a sibling ROM, and apply the load/add/replace + focus rules.
//
// Mirrors RecentStore's shape (constructor(backend, onChange), view(), mutators,
// no-op-guarded change signal). Reproduces the legacy native service's constructSystem /
// duplicateSystem / reload orchestration and the DSP list handlers
// (PluginDSP.cpp:406-458), with every path derived by the pure kernels.

import type { ConstructSpec, ControlPlaneBackend, HostBackend, SerialOutCapture } from "./backend";
import type { GraphSystem } from "./graph/buildGraph";
import { EngineGraph } from "./engineGraph";
import { detectPlatform, romHasBattery, ROM_SNIFF_LEN, SEGA_SNIFF_LEN, defaultCoreFor, type Platform, type Core } from "./platform";

import { resolveSavPath, siblingSavPath, siblingRplgPath, nextFreeSavSuffix, siblingSavCandidates } from "./savPaths";
import { flushBatteryForTeardown, type SramTarget } from "./sramAutoSave";
import { extensionLower } from "./pathUtil";
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
import { isSystemStage, type RoleRegistry, type RoleInstance } from "./systemRoles";
import { blipToasterChip, needsChipLabelScan, BLIPTOASTER_CHIP_SCAN_LEN } from "./bliptoaster/romDetect";
import { roleConfigForNative, LsdjSyncMode } from "./settingsEnums";

// How much ROM header to read for the role providers (title lives at 0x134).
const ROLE_HEADER_LEN = 0x150;

/** What the role providers get to look at. Read in TIERS, the same shape `classifyRom` uses, because two
 *  cases need far more than a header and neither is the common one:
 *
 *  - **Sega always.** No title field, and the header itself sits at the END of a bank. smsggdj's marker
 *    is at $3640, so a ROLE_HEADER_LEN read would leave the provider nothing to match on and force
 *    `sms-sync` onto every SMS cart - which it must not, since it drives Player 2's button lines.
 *  - **A BlipToaster ROM predating the SIG chip tag.** Newer ones declare their build in the SIG block,
 *    inside the short prefix; older ones name it only in the on-screen label their linker pins inside the
 *    PRG region, and that is the ONLY thing separating the two mapper-69 builds (bliptoaster/romDetect).
 *    So the deep read is the fallback path, not the normal one, and no other NES cart pays it.
 *
 *  One read at construct, not on the classify path. */
function readRoleHeader(backend: RoleHeaderCaps, platform: Platform, romPath: string): Uint8Array {
  if (platform === "sms" || platform === "gg") return backend.readFilePrefix(romPath, SEGA_SNIFF_LEN) ?? new Uint8Array();

  const short = backend.readFilePrefix(romPath, ROLE_HEADER_LEN) ?? new Uint8Array();
  if (platform !== "nes" || !needsChipLabelScan(short)) return short;
  return backend.readFilePrefix(romPath, BLIPTOASTER_CHIP_SCAN_LEN) ?? short;
}

interface RoleHeaderCaps {
  readFilePrefix(path: string, length: number): Uint8Array | null;
}

// TS owns the system-id counter (native never allocates). Module-scoped so it's ONE id space per
// control-plane JS context, which is 1:1 with the native Engine in every host (a plugin instance has
// its own JS context + Engine; a native test file shares one host + Engine across its cases). Ids are
// opaque handles; the snapshot registry uses 0 as its free-slot sentinel, so they start at 1.
let nextSystemId = 1;
function allocSystemId(): number {
  return nextSystemId++;
}

// New SRAM seeds a blank battery: a generous 128 KiB of zeros (a safe upper bound across GB/NES/GBA save
// sizes). Each core's onActivate truncates or zero-pads it to the cart's real battery size (SameBoy via
// GB_save_battery_size, Mesen via the NesSaveRam region), so any non-empty all-zero buffer blanks the SRAM.
const BLANK_SRAM_BYTES = 0x20000;

/** Classify a ROM's platform from its header only - the one place ROM bytes enter TS. Native never
 *  classifies.
 *
 *  Two tiers, because the Nintendo headers sit at the START of the file while a Sega 8-bit header sits
 *  at the end of the first bank ($7FF0, or $81F0 with a copier header). Reading that far on every
 *  classify would make GB/NES/GBA - the overwhelmingly common case - pay 33 KB to find a logo in the
 *  first 0x134 bytes. So the deep read happens only for a file the cheap tier already declined, which
 *  is SMS/GG ROMs and genuine non-ROMs. `readFilePrefix` truncates to the real file size, so asking for
 *  more than a file holds is safe. */
export function classifyRom(backend: HostBackend, romPath: string): Platform | "unknown" {
  const head = backend.readFilePrefix(romPath, ROM_SNIFF_LEN) ?? new Uint8Array();
  const fast = detectPlatform(head);
  if (fast !== "unknown") return fast;
  const deep = backend.readFilePrefix(romPath, SEGA_SNIFF_LEN) ?? new Uint8Array();
  return detectPlatform(deep, extensionLower(romPath));
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
  // A store on its own publishes its systems at the default (stereo) routing, with no project stages (so no MIDI
  // routing), through a graph of its own; the ProjectStore that owns one replaces this with its own publish, which
  // adds the project's routing and kernel structure.
  private ownGraph: EngineGraph | null = null;
  private onGraphChange: (rearmTap: boolean) => void = (rearmTap) => {
    this.ownGraph ??= new EngineGraph(this.backend);
    this.ownGraph.publish({ systems: this.graphSystems(), routing: "stereo", project: [] }, { rearmTap });
  };
  // Whether a core teardown writes the battery to disk first. Off only for an offline render.
  private batteryFlush = true;

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
  /** The systems as the engine's graph places them, in slot order (graph/buildGraph.ts), each with the roles the
   *  DSP kernel runs for it as its pipeline. */
  graphSystems(): GraphSystem[] {
    return this.entries.map((e) => ({ id: e.id, ...e.built, pipeline: this.kernelPipeline(e.roles) }));
  }

  /** `roles`, in order, without those the DSP kernel does not run: the core's config role and the features with no
   *  DSP behaviour (asset overrides, a ROM's facts). The kernel skips those itself, but in the graph's document they
   *  would make every model, palette or asset edit a new document. */
  private kernelPipeline(roles: RoleInstance[]): RoleInstance[] {
    return roles.filter((r) => this.runsInKernel(r.kind));
  }

  // Whether the kernel runs a role of `kind`, by the kernel's own test (isSystemStage). A store without a registry
  // cannot tell, so it hands the kernel every role it holds (a loaded project's), for the kernel's registry to
  // decide, and an edit to any of them publishes.
  private runsInKernel(kind: string): boolean {
    return this.registry ? isSystemStage(this.registry.roleType(kind)) : true;
  }
  focused(): number {
    return this.focusedId;
  }
  /** The system anything project-wide speaks for - the focused one, else the first. undefined when empty.
   *  The recents name / song and the song watcher all read this one instance. */
  primary(): SystemEntry | undefined {
    return this.entries.find((e) => e.id === this.focusedId) ?? this.entries[0];
  }
  isDirty(): boolean {
    return this.dirty;
  }

  /** Install the observer fired on a focus change (setFocus). A NON-dirtying UI signal — focus is
   *  transient (not persisted), so it re-renders the tiles without marking the project dirty or
   *  publishing the graph (distinct from the structural onChange). */
  setOnFocusChange(fn: () => void): void {
    this.onFocusChange = fn;
  }

  /** Install the hook that publishes the engine's graph (the ProjectStore's, which adds the routing and the kernel's
   *  project stages; without one the store publishes its systems at the default stereo routing itself). Called
   *  once at the end of every edit that changes what the graph places or what the kernel runs for a system, BEFORE
   *  the structural onChange: `rearmTap` for an edit that builds or drops a core, which re-arms the NES tap. */
  setOnGraphChange(fn: (rearmTap: boolean) => void): void {
    this.onGraphChange = fn;
  }

  /** Turn the before-teardown battery flush off for this store. For a throwaway headless session — an
   *  offline render builds systems, loads savestates into them and drops them, and an export must not
   *  write into the user's save folder as a side effect. The plugin and the standalone never call this. */
  setBatteryFlush(on: boolean): void {
    this.batteryFlush = on;
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
    const prev = findById(this.entries, id);
    if (!prev) return null;
    this.flushBeforeDrop(prev);
    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), opts?.explicitSav);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, built.entry);
    if (this.focusedId === id) this.focusedId = built.id;
    return this.committed(built.id);
  }

  /** Swap system `id`'s ROM for `romPath` in place, carrying its LIVE battery SRAM forward — the ROM
   *  change that keeps your save (e.g. bumping LSDj to a new version without losing the song). Unlike
   *  replaceSystem (fresh boot, sav from disk), this seeds the running SRAM as the new cart's cold-boot
   *  battery. The new ROM is classified afresh, so platform/core/roles/battery follow it; identity + focus
   *  are preserved. The auto-save target follows the new ROM (its sibling `<rom>.sav`, at a suffix
   *  disambiguated against the live list — see replacementSuffix; it is NOT unconditionally 0, which used
   *  to collide two instances of one ROM onto a single file), so — like
   *  New SRAM / Load SRAM — the carried battery is what's saved there on the next battery write. Null when
   *  `id` is absent or the ROM won't classify/build. */
  swapRom(id: number, romPath: string): number | null {
    const prev = findById(this.entries, id);
    if (!prev) return null;
    const sramBytes = this.backend.readSram(id) ?? undefined;
    this.flushBeforeDrop(prev); // the OLD cart's sav — the carried battery lands on the new ROM's
    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, sramBytes);
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
    // Through the load-time role hooks, as rebuildInPlace is: the clone boots the effective ROM its role
    // config describes (asset overrides folded in), the image the source's savestate expects.
    const spec = this.applyConstructHooks({
      romPath: src.romPath,
      platform: src.platform,
      core: src.core,
      embeddedRom: src.embeddedRom,
      savPath,
      statePath: null,
      stateBytes: state,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, src.roles);
    const built = this.backend.buildCore(spec, newId);
    if (!built) {
      console.warn(`[systems] duplicateSystem(${id}) failed (the core did not build) - no instance added`);
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
      built,
    });
    return this.committed(newId);
  }

  /** Drop a system; refocus the front if it was focused. False when absent. */
  removeSystem(id: number): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    this.flushBeforeDrop(e);
    this.entries = removeById(this.entries, id);
    this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
    this.onGraphChange(true);
    this.markDirty();
    return true;
  }

  /** Rebuild a system's ROM from disk, carrying its battery SRAM forward, swapping in place with a new
   *  id and preserving identity + focus. Orchestrated in TS: pull SRAM from the registry, then cold-boot
   *  the ROM with it (no savestate) into the same slot. No native reload method. */
  reloadSystem(id: number): number | null {
    const prev = findById(this.entries, id);
    if (!prev) return null;
    this.flushBeforeDrop(prev);
    return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined });
  }

  /** Reconstruct system `id` in place from seed bytes: capture the source's spec, build a fresh core
   *  under a new id that takes the old one's slot, and keep identity + focus. The seed (sramBytes
   *  = cold-boot battery, stateBytes = boot from savestate) overrides what native would read from disk.
   *  `savPathOverride` repoints the auto-save target (New SRAM…); omitted, the system keeps its own. The
   *  shared body of reload / loadState / loadSram / newSramAs — the in-place twin of duplicateSystem
   *  (which appends). */
  private rebuildInPlace(
    id: number,
    seed: { sramBytes?: Uint8Array; stateBytes?: Uint8Array },
    savPathOverride?: string,
  ): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const override = savPathOverride ?? src.savPath;
    const systemRole = src.roles.find((r) => r.kind === src.core);
    const newId = allocSystemId();
    // Route through the load-time role hooks too, so a reload re-applies e.g. LSDj asset overrides
    // (the patched effective ROM) — not just the fresh-load path.
    const spec = this.applyConstructHooks({
      romPath: src.romPath,
      platform: src.platform,
      core: src.core,
      embeddedRom: src.embeddedRom,
      savPath: src.embeddedRom ? null : resolveSavPath(src.romPath, src.savSuffix, override),
      statePath: null,
      sramBytes: seed.sramBytes,
      stateBytes: seed.stateBytes,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, src.roles);
    const built = this.backend.buildCore(spec, newId);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, { ...src, id: newId, savPath: override, built });
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

  /** Read system `id`'s live battery SRAM (race-free snapshot). Null when nothing is published. Exposes the
   *  emulator-facet read to callers holding only the store (e.g. the LSDj Songs menu decoding the sav). */
  readSram(id: number): Uint8Array | null {
    return this.backend.readSram(id);
  }

  /** Read system `id`'s live work RAM (the same race-free published snapshot `readSram` serves, for the
   *  other region). Null when nothing is published. Needed by consoles whose WORKING SONG lives outside
   *  the battery - smsggdj keeps its in work RAM - so the Songs menu can tell whether a battery edit's
   *  cold boot would throw away real work. */
  readRam(id: number): Uint8Array | null {
    return this.backend.readRam(id);
  }

  /** Poke bytes into system `id`'s live work RAM, at the same offsets `readRam` returns. Queued to the
   *  audio thread, so it lands between blocks and is safe while the core plays; false when the id is
   *  unknown or the write would run past the region. What a tracker whose working song lives OUTSIDE
   *  the battery (smsggdj) uses to load a song without rewriting the `.sav` or rebooting the cart. */
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean {
    return this.backend.writeRam(id, offset, bytes);
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
    const prev = findById(this.entries, id);
    if (prev) this.flushBeforeDrop(prev, path);
    return this.rebuildInPlace(id, { stateBytes: bytes });
  }

  /** Load a battery SRAM file into system `id`: cold-boot the ROM in place with those bytes AND repoint the
   *  system's auto-save target to that file, so from now on it saves back to where it was loaded from (the
   *  override is persisted in the project). A real ROM repoints; the embedded synth keeps its (null) target.
   *  The override collapses to "" when the pick is just the ROM's natural sibling. Null when the file is
   *  unreadable, `id` is absent, or the build fails. */
  loadSram(id: number, path: string): number | null {
    const bytes = this.backend.readFile(path);
    if (!bytes) return null;
    const src = findById(this.entries, id);
    if (!src) return null;
    // `path` is named, so a caller that just WROTE it (a Songs-menu edit, a song import) is skipped —
    // its live battery is the pre-edit copy and flushing would undo the save. See flushBatteryForTeardown.
    this.flushBeforeDrop(src, path);
    const override = src.embeddedRom
      ? undefined // embedded synth has no on-disk save target to repoint
      : resolveSavOverride(src.romPath, src.savSuffix, path, (p) => this.backend.canonicalize(p));
    return this.rebuildInPlace(id, { sramBytes: bytes }, override);
  }

  /** Reboot system `id` in place, carrying its live battery SRAM forward — a hardware-style reset: the
   *  save persists, the running state is dropped. Reconstructs from the ROM (the same engine
   *  as reload) rather than a live GB_reset. Null when `id` is absent. */
  reset(id: number): number | null {
    const prev = findById(this.entries, id);
    if (!prev) return null;
    this.flushBeforeDrop(prev);
    return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined });
  }

  /** Wipe system `id`'s battery SRAM (a fresh cartridge) and cold-boot it in place. Seeds an all-zero
   *  battery — native truncates/zero-pads it to the cart's real size — so the live core reads blank SRAM;
   *  the on-disk `.sav` follows on the next battery save. Null when `id` is absent. */
  newSram(id: number): number | null {
    if (!findById(this.entries, id)) return null;
    return this.rebuildInPlace(id, { sramBytes: new Uint8Array(BLANK_SRAM_BYTES) });
  }

  /** "New SRAM…": cold-boot system `id` on a blank battery and repoint its auto-save target to the
   *  user-picked `savPath`, then materialise that file — a fresh cartridge written to a NEW file the user
   *  named. The old target is not left behind: the battery the cart was RUNNING is flushed to it first, so
   *  the promise is "your current save lands in the old file, never the blank one" rather than "the old
   *  file is untouched" — leaving it untouched meant silently discarding unsaved play on the way to a
   *  fresh cartridge. The ROM is unchanged, so the system's settings/roles are preserved. Null for an
   *  absent id or the embedded synth (no on-disk save). */
  newSramAs(id: number, savPath: string): number | null {
    const src = findById(this.entries, id);
    if (!src || src.embeddedRom) return null;
    this.flushBeforeDrop(src, savPath); // the old target keeps the CURRENT battery, never the blank one
    // Normalise the pick the way construct does: "" when it's just the natural sibling, else the raw path.
    const override = resolveSavOverride(src.romPath, src.savSuffix, savPath, (p) => this.backend.canonicalize(p));
    const newId = this.rebuildInPlace(id, { sramBytes: new Uint8Array(BLANK_SRAM_BYTES) }, override);
    if (newId === null) return null;
    // Materialise the fresh save at the SAME path the core will auto-save to — resolveSavPath(…override),
    // NOT the raw pick. They diverge when the pick normalises to "" (e.g. a duplicate instance, savSuffix≥2,
    // whose pick equals the plain <rom>.sav): the target is <rom>-N.sav, so writing the raw pick would blank
    // a sibling instance's real .sav. Keying both off the same resolve makes that impossible.
    const savTarget = resolveSavPath(src.romPath, src.savSuffix, override);
    const live = this.backend.readSram(newId);
    if (live) this.backend.writeFileAtomic(savTarget, live);
    return newId;
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
    // A Game Boy's link group is the one system-role field that is also wiring: the cable follows it, so only a
    // change of its value publishes (a model or palette edit leaves the document as it was).
    const linkGroupId = (config as { linkGroupId?: unknown }).linkGroupId;
    const linkGroup = typeof linkGroupId === "number" ? linkGroupId : 0;
    const rewired = roleKind === "sameboy" && e.built.type === "rp.sameboy.gb" && linkGroup !== e.built.linkGroup;
    const built = rewired ? { ...e.built, linkGroup } : e.built;
    this.entries = replaceById(this.entries, id, { ...e, roles, built });
    // A feature role stays pure TS, EXCEPT lsdj-sync's serial-out capture gate — arm/disarm it on a
    // MIDIOUT mode change (the decoder itself runs in the kernel, which the publish below hands the new mode).
    if (roleKind === "lsdj-sync") this.syncSerialOutCapture(id);
    // A role the kernel runs is the system's pipeline, which the document carries, so its edit publishes; an edit the
    // document does not carry (a model or palette, an asset override) leaves it as it was.
    if (rewired || this.runsInKernel(roleKind)) this.onGraphChange(false);
    this.markDirty();
    return true;
  }

  /** Link a freshly-created instance to the one it was created from: the child joins the parent's link group,
   *  and if the parent is ungrouped (0) the parent is promoted to group 1 so the pair is linked. A no-op unless
   *  BOTH are SameBoy (GB) systems — link groups are a Game Boy link-cable concept. Routes through
   *  setRoleConfig, so native-apply + dirty + the publish are handled. */
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

  /** Tear down every system + reset the list/focus (for `new` + before a load). Publishes the empty graph
   *  (a load builds its systems only after their predecessors are gone, so it never holds both), but no
   *  onChange: the caller notifies once it is done. */
  clear(): void {
    const had = this.entries.length > 0;
    for (const e of this.entries) this.flushBeforeDrop(e);
    this.entries = [];
    this.focusedId = 0;
    if (had) this.onGraphChange(true);
  }

  /** Reconstruct one system from a serialized config entry, preserving its EXACT
   *  savSuffix + savPath-override (no free-suffix reassignment). Appends; focuses the
   *  first. Returns the new id, or null when the ROM won't classify. `blobs` (present
   *  only for a zip-import) seed the emulator's SRAM/savestate from the archive instead
   *  of from disk; `savPath` stays the auto-save target.
   *
   *  QUIET ON PURPOSE: unlike every other mutation this does NOT markDirty() or fire onChange. It
   *  does publish the graph, which carries the system's kernel pipeline, so the core and its roles
   *  run at once (a project load holds that back and publishes once its systems are all built; see
   *  projectStore.load, which then fires ONE onSystemsChange rather than N). */
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
    // Stored roles win, but a role kind they PREDATE is added (see withMissingFeatureRoles); a config
    // that omits them entirely re-attaches defaults. Known before the build so a role's load-time hook
    // can seed the spec (e.g. an empty LSDj sav) before instantiation.
    const roles = config.roles && config.roles.length
      ? this.withMissingFeatureRoles(config.roles, core, platform, romPath, embeddedRom)
      : this.defaultRoles(core, platform, romPath, embeddedRom);
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
    // Stored settings/roles win; a config that omits them re-attaches defaults. Parsed, like the battery read,
    // before the build, so a throw here leaves no core behind.
    const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
    const battery = this.detectBattery(romPath, embeddedRom, platform);
    const built = this.backend.buildCore(spec, id); // a loaded system already in MIDIOUT mode is built capturing
    if (!built) return null;
    const wasEmpty = this.entries.length === 0;
    this.entries = appendEntry(this.entries, { id, platform, core, romPath, savPath: override, savSuffix, embeddedRom, battery, settings, roles, built });
    if (wasEmpty) this.focusedId = id;
    this.onGraphChange(true);
    return id;
  }

  // --- internals ----------------------------------------------------------

  // Replace the focused tile (or adopt into an empty project), focusing the result.
  private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
    const empty = this.entries.length === 0;
    const target = empty ? undefined : this.effectiveFocus();
    const built = this.construct(romPath, embeddedRom, this.replacementSuffix(target, romPath), explicitSav);
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
    let override = resolveSavOverride(romPath, suffix, explicitSav ?? "", (p) =>
      this.backend.canonicalize(p),
    );
    // With no explicit pick, auto-adopt an existing `<rom>.srm` battery sibling when there's no `<rom>.sav`
    // (some NES/risa saves use the .srm extension) — so we load from and auto-save back to it, rather than
    // deriving a stray new `<rom>.sav` next to it.
    if (!override && !explicitSav && !embeddedRom) override = this.adoptExistingSavSibling(romPath, suffix);
    const savPath = embeddedRom ? null : resolveSavPath(romPath, suffix, override);
    const id = allocSystemId();
    // Roles are known before the build (a pure function of core/platform/header), so a role's
    // load-time hook can seed the spec — e.g. an empty LSDj sav — before the core is instantiated.
    const roles = this.defaultRoles(core, platform, romPath, embeddedRom);
    const spec = this.applyConstructHooks({ romPath, platform, core, embeddedRom, savPath, statePath: null, sramBytes }, roles);
    const battery = this.detectBattery(romPath, embeddedRom, platform); // before the build: a throw leaves no core
    const built = this.backend.buildCore(spec, id);
    if (!built) return null;
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
        battery,
        settings: { ...DEFAULT_COMMON_SETTINGS },
        roles,
        built,
      },
    };
  }

  // The battery save to adopt for a freshly-built system when the caller didn't pick one: keep deriving
  // `<rom>.sav` (return "" = no override) if it exists or nothing does; adopt a non-.sav sibling (e.g.
  // `<rom>.srm`) as an explicit override only when the .sav is absent but that alternative is present.
  private adoptExistingSavSibling(romPath: string, suffix: number): string {
    const [savCand, ...alts] = siblingSavCandidates(romPath, suffix);
    if (this.backend.fileExists(savCand)) return ""; // <rom>.sav present → derive as usual
    for (const cand of alts) if (this.backend.fileExists(cand)) return cand; // e.g. <rom>.srm → adopt it
    return ""; // nothing on disk → default to <rom>.sav for new saves
  }

  // The default roles for a freshly-built system: the core's config role + any feature
  // roles the registry's providers suggest for this ROM's header. Empty when no registry
  // is wired (back-compat) or for an embedded ROM (no file to sniff).
  //
  private defaultRoles(core: Core, platform: Platform, romPath: string, embeddedRom: string): RoleInstance[] {
    if (!this.registry) return [];
    const header =
      romPath && !embeddedRom ? readRoleHeader(this.backend, platform, romPath) : new Uint8Array();
    return this.registry.defaultRoles(core, platform, header, embeddedRom);
  }

  /** `stored`, plus any FEATURE role today's providers suggest for this ROM whose kind is missing.
   *
   *  Without this a project can never acquire a role that shipped after it was saved: the stored list
   *  won wholesale, so providers only ran for a project storing none. That is not a hypothetical - an
   *  smsggdj `.rplg` written before `sms-sync` existed loaded, booted, armed, showed WAIT and ignored
   *  the DAW transport forever, with nothing in the UI or logs naming the cause. Every future feature
   *  role would have inherited the same trap, and `risa-sync` already had.
   *
   *  Safe because a stored list is NOT a curated selection. `setRoleConfig` only edits a role's config;
   *  there is no removeRole anywhere in the codebase, so a stored list is simply what the providers
   *  produced when the project was created. Adding what they suggest now cannot override a user's
   *  decision, because no such decision is expressible. **If a "remove role" affordance is ever added
   *  that stops being true** - this would resurrect a removed role - and it will need a tombstone in the
   *  project schema. There is nothing to tombstone today.
   *
   *  Three deliberate limits:
   *
   *  - **Feature roles only.** The core-config role (kind === core) is skipped, because adopt reads the
   *    construct-time `settings` blob from the STORED roles: synthesising one here would start passing
   *    schema defaults where `undefined` is passed today and native applies its own.
   *  - **A stored role is never touched**, so a configured sync mode or a kit override survives.
   *  - **Appended, not merged in place.** `graphSystems` keeps this array's order for the DSP
   *    pipeline, and `defaultRoles` itself emits core-role-then-providers, so appending
   *    reproduces the order a freshly added system would have.
   *
   *  A stored kind that is no longer registered is left alone rather than pruned: the pipeline leaves
   *  out kinds the registry does not know, so it is inert, and dropping it would silently
   *  discard state belonging to a build the user might go back to.
   *
   *  No schema change and no version bump: the file is read as written, and the augmented list only
   *  reaches disk if the user saves - at which point the project self-heals. */
  private withMissingFeatureRoles(
    stored: RoleInstance[],
    core: Core,
    platform: Platform,
    romPath: string,
    embeddedRom: string,
  ): RoleInstance[] {
    if (!this.registry) return stored;
    const have = new Set(stored.map((r) => r.kind));
    // `kind !== core` IS the system-role test: defaultRoles emits it only via systemRoleFor(core), so
    // this excludes exactly that one entry. Testing `category === "feature"` instead would look more
    // explicit and be subtly worse - a provider suggesting a kind whose RoleType is not registered
    // would be dropped here while a freshly added system still received it, so the two paths would
    // disagree for a partially-wired registry.
    const missing = this.defaultRoles(core, platform, romPath, embeddedRom).filter(
      (r) => r.kind !== core && !have.has(r.kind),
    );
    return this.withDerivedRoleFacts(missing.length ? [...stored, ...missing] : stored, romPath, embeddedRom);
  }

  /** Re-derive the role-config fields that are FACTS ABOUT THE ROM FILE rather than user choices, so a
   *  loaded project reflects the ROM on disk today. The same reasoning as `battery` below, which is
   *  re-derived at every build instead of being serialized - the difference is only that these live
   *  inside a role config, so they cannot simply be left out of the file.
   *
   *  Today that is exactly one field: `bliptoaster.chip`, which expansion build the ROM is. It decides
   *  which CC set the DAW parameter map exposes (parameterMap.ts), and overwriting it repairs the two
   *  cases a stored value gets wrong: a project written before the field existed (which would read as
   *  the 2A03 core and silently show none of the expansion lanes), and a ROM swapped for a different
   *  build at the same path. It is deliberately NOT covered by "a stored role is never touched" above:
   *  that rule protects configuration, and this is not configuration.
   *
   *  Also the repair path for a project stored while the chip was read off the ambiguous mapper: an S5B
   *  ROM saved then reads back as "2a03", and re-deriving from the deeper prefix corrects it in place. */
  private withDerivedRoleFacts(roles: RoleInstance[], romPath: string, embeddedRom: string): RoleInstance[] {
    const i = roles.findIndex((r) => r.kind === "bliptoaster");
    if (i < 0 || !romPath || embeddedRom) return roles;

    // The same tiered read `defaultRoles` gives the providers. A `bliptoaster` role means the cart is
    // NES; passing a shorter prefix would "correct" an untagged S5B ROM back onto the ambiguous mapper
    // fallback on every load.
    const header = readRoleHeader(this.backend, "nes", romPath);
    if (!header.length) return roles;

    const chip = blipToasterChip(header);
    const cfg = roles[i].config as { chip?: string } | undefined;
    if (cfg?.chip === chip) return roles;

    const out = [...roles];
    out[i] = { ...roles[i], config: { ...(cfg ?? {}), chip } };
    return out;
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
    let s = spec;
    if (this.registry)
      for (const r of roles) {
        const rt = this.registry.roleType(r.kind);
        if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
      }
    // A core is built with its serial-out capture already armed, so no command has to chase it.
    const capture = serialOutCaptureFor(roles);
    return capture ? { ...s, serialOutCapture: capture } : s;
  }

  // Write a system's live battery to its own `.sav` before its core is destroyed. Every teardown path
  // funnels through here so "closing a cart never loses its battery" is one rule rather than nine.
  // `named` is the file the operation itself named (see flushBatteryForTeardown) — omit it when the
  // operation names no file. A no-op for a store with the flush disabled (an offline render).
  private flushBeforeDrop(sys: SramTarget, named?: string): void {
    if (!this.batteryFlush) return;
    flushBatteryForTeardown(this.backend, sys, named);
  }

  // The free suffix for a new instance of `romPath`: live-list ownership + on-disk.
  // `excludeId` is a system that is about to STOP EXISTING (it is being replaced), so the slot it
  // currently owns is not a conflict for its own successor.
  private freeSuffix(romPath: string, excludeId?: number): number {
    const live = excludeId === undefined ? this.entries : this.entries.filter((e) => e.id !== excludeId);
    return nextFreeSavSuffix(
      romPath,
      (n) => isSuffixOwned(live, romPath, n),
      (n) => this.backend.fileExists(siblingSavPath(romPath, n)),
    );
  }

  // The sav suffix for a system replacing `replaceId`. Replacing in place is NOT the same question as
  // adding: hard-coding 0 here dropped a duplicate onto the original's `<rom>.sav`, and both then wrote
  // the same file. But excluding the outgoing id is not enough on its own either - nextFreeSavSuffix also
  // skips a suffix whose file exists on disk, and for an UNCHANGED ROM that file is the outgoing
  // instance's own save, so it would migrate to a fresh suffix and strand the battery it has been
  // writing all session. Hence: same ROM keeps its slot; a different ROM disambiguates without counting
  // the instance it displaces.
  private replacementSuffix(replaceId: number | undefined, romPath: string): number {
    if (replaceId === undefined) return this.freeSuffix(romPath);
    const prev = findById(this.entries, replaceId);
    if (prev && prev.romPath === romPath) return prev.savSuffix;
    return this.freeSuffix(romPath, replaceId);
  }

  // The focused id, falling back to the front when focus is stale (DSP LoadRom rule).
  private effectiveFocus(): number {
    return findById(this.entries, this.focusedId) ? this.focusedId : this.entries[0].id;
  }

  private markDirty(): void {
    this.dirty = true;
    this.onChange();
  }

  // Finish a successful mutation that built a core: publish the graph that places it, mark dirty + notify,
  // return the id.
  private committed(id: number): number {
    this.onGraphChange(true);
    this.markDirty();
    return id;
  }

  // Set native serial-out capture for a running system from its lsdj-sync mode (serialOutCaptureFor). Called
  // only when the mode changes (setRoleConfig): a core that is built or rebuilt carries its capture in the build
  // spec instead (applyConstructHooks). A no-op for a non-LSDj system (no lsdj-sync role → nothing to arm).
  private syncSerialOutCapture(id: number): void {
    const roles = findById(this.entries, id)?.roles;
    if (!roles?.some((r) => r.kind === "lsdj-sync")) return;
    this.backend.setSerialOutCapture(id, serialOutCaptureFor(roles));
  }
}

// How a system with these roles captures its serial out: both LSDj-to-host MIDI-out modes need it. MidiOut
// (SYNC=MI.OUT) drives, playing the Arduinoboy that clocks LSDj as the serial slave; MasterSync (SYNC=LSDJ) only
// listens to the clock LSDj sends as the master. Null for no capture.
function serialOutCaptureFor(roles: RoleInstance[]): SerialOutCapture | null {
  const mode = (roles.find((r) => r.kind === "lsdj-sync")?.config as { mode?: LsdjSyncMode } | undefined)?.mode;
  if (mode === LsdjSyncMode.MidiOut) return "drive";
  if (mode === LsdjSyncMode.MasterSync) return "listen";
  return null;
}

// SystemsStore: the live systems list as the app owns it. TS is the source of truth
// for order, ids-as-handles, focus, and dirty; the Backend is a narrow emulator-
// lifecycle service (build / clone / reload / drop) that takes CONCRETE paths TS has
// already resolved. All the orchestration lives here: classify a ROM (TS-side, from
// its header), disambiguate a sav suffix against the live list, resolve a paired-sav
// override, pick a sibling ROM, and apply the load/add/replace + focus rules.
//
// Mirrors RecentStore's shape (constructor(backend, onChange), view(), mutators,
// no-op-guarded change signal). Reproduces the legacy native service's constructSystem /
// duplicateSystem / reload orchestration and the DSP list handlers
// (PluginDSP.cpp:406-458), with every path derived by the pure kernels.

import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
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
import type { RoleRegistry, RoleInstance } from "./systemRoles";
import { blipToasterChip, needsChipLabelScan, BLIPTOASTER_CHIP_SCAN_LEN } from "./bliptoaster/romDetect";
import { roleConfigForNative, LsdjSyncMode } from "./settingsEnums";
import { resolveSongCatalog, resolveTracker } from "./tracker/trackerIntegration";
import type { TrackedSlot, WorkingSongImage } from "./tracker/songCatalog";

// Frames the slot watcher waits for a landed carry's queued writes to appear before it trusts the cart
// again; they normally appear on the next audio block, so this only bounds a cart that moved on.
const LANDING_FRAMES = 120;
// Frames a clean working song that matched no saved slot is re-checked before the verdict sticks (~1 s):
// long enough to outlast a battery that lags work RAM by a block or two, short enough to cost nothing.
const UNMATCHED_RECHECKS = 60;
// How long after a carry lands an export still ships the working song beside the savestate: longer than
// the savestate's 0.5 s republish interval, with margin for a slow block thread.
const LANDED_EXPORT_MS = 2000;

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
  // Whether a core teardown writes the battery to disk first. Off only for an offline render.
  private batteryFlush = true;
  // Working songs being carried across a reboot, keyed by the NEW system id - see carryWorkingSong.
  // No time budget: a carry is keyed by a system id, which dies with its system, so one that waits can
  // only ever land in the cart it was made for. (A budget used to drop it after ~15 s of UI frames, which
  // a host that stops processing audio - the cart frozen mid-boot - spent without the cart moving at all.)
  private carries = new Map<number, { song: WorkingSongImage }>();
  // Each system's last OBSERVED clean working song: its change signal, whether it matched a saved slot,
  // and how many more frames an unmatched one is re-checked - see observeWorkingSongs.
  private observed = new Map<number, { signal: number; matched: boolean; retries: number }>();
  // A carry's writes are queued, so for a block or so the cart still holds the blank song it booted into.
  // Until the restored song shows up the CARRIED song is the working song: the watcher must not read the
  // blank as an in-cart NEW, and Save, the unsaved prompt and an export must not read it as the song.
  // id -> the restored song, the change signal of the BLANK it is replacing, frames waited. Keyed on the
  // blank, not on the restored song: the window is exactly "RAM still shows the blank", so the moment
  // anything else is there - the restore, or an edit already made on top of it - the cart speaks for
  // itself again. Keyed on the restored song instead, an edit landing with the restore kept the window
  // open, and a Save in it stored the pre-edit song and marked the newer one saved.
  private landing = new Map<number, { song: WorkingSongImage; blank: number; frames: number }>();
  // Each system's observed `edited` alongside its signal (see observeWorkingSongs).
  private observedEdited = new Map<number, boolean>();
  // When each system's last carry landed - see exportableWorkingSong.
  private landedAt = new Map<number, number>();
  // Old id -> the id its in-place replacement was built under - see currentId.
  private successors = new Map<number, number>();

  constructor(
    private readonly backend: ControlPlaneBackend,
    private readonly onChange: () => void = () => {},
    private readonly registry?: RoleRegistry,
  ) {}

  /** The id system `id` goes by NOW. Every in-place rebuild - reset, reload, Load SRAM / State, a ROM
   *  swap or replace - builds the replacement under a NEW id in the same slot, so anything holding the old
   *  one (the instance menu anchored to it) follows it here instead of holding a dead id. `id` itself when
   *  it is still live; null when it is gone for good (removed, or its project closed). */
  currentId(id: number): number | null {
    let cur = id;
    for (let hops = 0; hops <= this.successors.size; hops++) {
      if (findById(this.entries, cur)) return cur;
      const next = this.successors.get(cur);
      if (next === undefined) return null;
      cur = next;
    }
    return null;
  }

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
   *  re-projecting the DSP (distinct from the structural onChange). */
  setOnFocusChange(fn: () => void): void {
    this.onFocusChange = fn;
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
    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), opts?.explicitSav, id);
    if (!built) return null;
    this.entries = replaceById(this.entries, id, built.entry);
    this.successors.set(id, built.id);
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
    // The working song rides along too, for the same reason the battery does - a version bump that keeps
    // your save should keep your song. Dropped at settle if the new ROM is not a cart that can take it.
    const song = this.takeWorkingSong(prev);
    const built = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, id, sramBytes);
    if (!built) return null;
    this.carryWorkingSong(built.id, song);
    this.entries = replaceById(this.entries, id, built.entry);
    this.successors.set(id, built.id);
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
    const e = findById(this.entries, id);
    if (!e) return false;
    this.flushBeforeDrop(e);
    this.backend.removeSystem(id);
    this.entries = removeById(this.entries, id);
    this.carries.delete(id);
    this.observed.delete(id);
    this.observedEdited.delete(id);
    this.landing.delete(id);
    this.landedAt.delete(id);
    this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
    this.markDirty();
    return true;
  }

  /** Rebuild a system's ROM from disk, carrying its battery SRAM forward, swapping in place with a new
   *  id and preserving identity + focus. Orchestrated in TS: pull SRAM from the registry, then cold-boot
   *  the ROM with it (no savestate) via a replaceId construct. No native reload method. */
  reloadSystem(id: number): number | null {
    const prev = findById(this.entries, id);
    if (!prev) return null;
    this.flushBeforeDrop(prev);
    const song = this.takeWorkingSong(prev);
    return this.carryWorkingSong(this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }), song);
  }

  /** Reconstruct system `id` in place from seed bytes: capture the source's spec, build a fresh core
   *  under a new id that swaps the old one (replaceId), and keep identity + focus. The seed (sramBytes
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
      replaceId: id,
      settings: systemRole ? JSON.stringify(roleConfigForNative(systemRole.kind, systemRole.config)) : undefined,
    }, src.roles);
    const ok = this.backend.constructSystem(spec, newId);
    if (!ok) return null;
    // The tracked slot does NOT survive a rebuild: Load State / Load SRAM / New SRAM / a Songs-menu edit
    // all replace or renumber what the cart holds, and a stale slot let a Save write a restored song over
    // the unrelated one it had named. Reset and reload, which keep the song, hand the slot on with it in
    // the carried image (see carryWorkingSong).
    this.entries = replaceById(this.entries, id, { ...src, id: newId, savPath: override, workingSlot: undefined });
    this.successors.set(id, newId);
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

  /** Poke bytes into system `id`'s live battery, at the offsets `readSram` returns, with no reboot. Lands
   *  between blocks like `writeRam`; `readSram` reflects it once it has. What saving a work-RAM working
   *  song (smsggdj) into the cart uses, so the cart and its `.sav` agree without a cold boot. */
  writeSram(id: number, offset: number, bytes: Uint8Array, expect?: Uint8Array): boolean {
    return this.backend.writeSram(id, offset, bytes, expect);
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
    const song = this.takeWorkingSong(prev);
    return this.carryWorkingSong(this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }), song);
  }

  // --- the working song across a reboot ------------------------------------
  //
  // Reset and a ROM-watch reload carry the BATTERY forward, which for LSDj and risa carries the working
  // song with it - so on those consoles neither reboot costs the user their song. smsggdj keeps its
  // working song in work RAM and boots blank, so the same two reboots used to erase it, silently. They
  // now carry it explicitly: lifted out of the old core as an image before the teardown, parked under
  // the new id, and written back once the new cart is past its boot (the write has to wait - `song_new`
  // blanks the block on the way up). Load SRAM / Load State / New SRAM are NOT carries: replacing what
  // the cart holds is what the user asked for, so those confirm instead.

  /** The working song `id` holds that lives outside its battery, from a carry still in flight if there is
   *  one (the live cart is still booting and holds nothing yet), else from live work RAM. Null for a
   *  console whose working song is in the battery, or a cart with nothing readable. */
  workingSongImage(id: number): WorkingSongImage | null {
    const pending = this.carries.get(id);
    if (pending) return pending.song;
    const e = findById(this.entries, id);
    const catalog = e ? resolveSongCatalog(e.roles) : undefined;
    if (!catalog?.readWorkingSong) return null;
    const ram = this.backend.readRam(id);
    const song = ram ? catalog.readWorkingSong(ram) : null;
    const land = this.landing.get(id);
    if (land && (!song || catalog.fingerprintOf?.(song) === land.blank)) return land.song;
    return song && e!.workingSlot ? { ...song, slot: e!.workingSlot } : song;
  }

  /** Record that system `id`'s working song is the saved song at `index` - it was just loaded from there,
   *  or saved into it - as the battery holds that slot NOW. What a later Save writes back over instead of
   *  appending a copy. Stored on the system, so it is persisted with the project and survives a reset or
   *  a savestate restore; it is bookkeeping, not an edit, so it does not dirty the project. */
  noteSongSlot(id: number, index: number): void {
    const e = findById(this.entries, id);
    const sram = e ? this.backend.readSram(id) : null;
    const slot = e && sram ? (resolveSongCatalog(e.roles)?.trackSlot?.(sram, index) ?? null) : null;
    this.setWorkingSlot(id, slot ?? undefined);
  }

  /** Keep each tracked slot true to what the cart did ITSELF, every frame (the UI calls it per frame).
   *  Only an UNEDITED song says anything: the cart clears its unsaved flag exactly when its working song
   *  is replaced wholesale - its own LOAD, SAVE, NEW. Then the song either matches a saved slot (track
   *  it) or matches none (a NEW song, or one from outside the battery: track nothing, so Save appends).
   *  It has to run this often, not on the half-second song poll: an in-cart NEW followed by an edit
   *  inside one poll interval kept the OLD slot, and Save then wrote the new song over the old one.
   *  Cheap while nothing changes: a work-RAM snapshot and a fingerprint, the battery read only when the
   *  clean song's content moves. */
  observeWorkingSongs(): void {
    for (const e of this.entries) {
      if (this.carries.has(e.id)) continue; // the booting cart holds nothing of its own yet
      const catalog = resolveSongCatalog(e.roles);
      if (!catalog?.readWorkingSong || !catalog.slotOfCleanSong || !catalog.fingerprintOf) continue;
      const ram = this.backend.readRam(e.id);
      const song = ram ? catalog.readWorkingSong(ram) : null;
      if (!song) continue;
      const fp = catalog.fingerprintOf(song);
      const land = this.landing.get(e.id);
      if (land) {
        // Still the pre-restore blank: wait for the carry's writes (bounded, in case the cart moved on).
        if (fp === land.blank && ++land.frames < LANDING_FRAMES) continue;
        this.landing.delete(e.id);
      }
      const seen = this.observed.get(e.id);
      // An unchanged song is settled once it matched a slot. One that matched NONE is re-checked for a
      // while: a cart that saves clears its unsaved flag in the same routine that writes its battery, and
      // reading the two a block apart once took a just-saved song for a NEW one and dropped its slot for
      // good, because the verdict was cached.
      const same = seen && seen.signal === fp && this.observedEdited.get(e.id) === song.edited;
      if (same && (seen.matched || seen.retries-- <= 0)) continue;
      const sram = this.backend.readSram(e.id);
      if (!sram) continue;
      // Only an UNEDITED song moves the tracking. An edited one says nothing about which slot it is - even
      // byte-identical to a saved song: that is also what one frame of an edit passing through an older
      // copy's value looks like, and taking it as a load moved the tracking onto the old copy for good, so
      // Save overwrote it. The cart's own loads clear its flag a few blocks after writing the song, and
      // THAT observation retargets.
      if (song.edited) {
        this.observed.set(e.id, { signal: fp, matched: true, retries: 0 });
        this.observedEdited.set(e.id, true);
        continue;
      }
      let slot = catalog.slotOfCleanSong(sram, { ...song, slot: e.workingSlot });
      // Clean, in no slot, but still the tracked song with only what PLAYBACK rewrites changed (a `T`
      // command rewrites the active groove inside the block, and never sets the flag): keep the tracking.
      // Dropped, the next edit and Save appended a copy instead of writing back.
      if (!slot && e.workingSlot && catalog.isPlaybackDrift?.(sram, e.workingSlot, song)) slot = e.workingSlot;
      if (!same) this.observed.set(e.id, { signal: fp, matched: !!slot, retries: UNMATCHED_RECHECKS });
      else seen!.matched = !!slot;
      this.observedEdited.set(e.id, false);
      this.setWorkingSlot(e.id, slot ?? undefined);
    }
  }

  /** Set (or clear) a system's tracked slot - quietly: no dirty, no change signal (nothing renders it). */
  private setWorkingSlot(id: number, slot: TrackedSlot | undefined): void {
    const e = findById(this.entries, id);
    if (e) this.entries = replaceById(this.entries, id, { ...e, workingSlot: slot });
    for (const held of [this.carries.get(id), this.landing.get(id)]) if (held) held.song = { ...held.song, slot };
  }

  /** The song a carry for `id` is holding has just been saved into the battery, so when it lands it
   *  must land clean rather than re-raising the unsaved flag it was captured with. */
  markCarriedSongSaved(id: number): void {
    for (const held of [this.carries.get(id), this.landing.get(id)]) if (held) held.song = { ...held.song, edited: false };
  }

  hasCarries(): boolean {
    return this.carries.size > 0;
  }

  /** True while a carry for `id` is pending OR has landed but its queued writes have not shown up in work
   *  RAM yet - the window in which the cart's RAM is not its song. A song request waits it out. */
  isRestoringSong(id: number): boolean {
    return this.carries.has(id) || this.landing.has(id);
  }

  /** The working song system `id` is still waiting to have put back. */
  pendingCarry(id: number): WorkingSongImage | null {
    return this.carries.get(id)?.song ?? null;
  }

  /** The working song a project export must ship BESIDE the savestate, because the savestate cannot be
   *  trusted to hold it: while a carry is pending (the savestate is the blank booting cart), and for a
   *  short while after it lands - the savestate is republished only every ~0.5 s, so for that long it can
   *  still be the blank cart. In that tail it is the CURRENT song, read live, so an edit made since the
   *  carry landed is shipped rather than reverted. Null the rest of the time. */
  exportableWorkingSong(id: number): WorkingSongImage | null {
    const pending = this.pendingCarry(id);
    if (pending) return pending;
    const at = this.landedAt.get(id);
    if (at === undefined) return null;
    if (Date.now() - at > LANDED_EXPORT_MS) {
      this.landedAt.delete(id);
      return null;
    }
    return this.workingSongImage(id);
  }

  /** Write every parked working song whose cart has finished booting back into it. Called once per frame
   *  by the UI while any are parked; a carry whose system has gone is dropped. */
  settleCarries(): void {
    for (const [id, c] of this.carries) {
      const e = findById(this.entries, id);
      const catalog = e ? resolveSongCatalog(e.roles) : undefined;
      const tracker = e ? resolveTracker(e.roles) : undefined;
      if (!e || !catalog || !tracker?.restoreWorkingSong) {
        this.carries.delete(id);
        continue;
      }
      const ram = this.backend.readRam(id) ?? undefined;
      if (!catalog.workingSongReady?.(ram)) continue;
      this.carries.delete(id);
      this.setWorkingSlot(id, c.song.slot);
      const blank = ram ? catalog.readWorkingSong?.(ram) : null;
      if (catalog.fingerprintOf && blank) this.landing.set(id, { song: { ...c.song }, blank: catalog.fingerprintOf(blank), frames: 0 });
      this.landedAt.set(id, Date.now());
      const rom = e.romPath ? this.backend.readFile(e.romPath) : null;
      const writes = rom ? tracker.restoreWorkingSong(rom, c.song, ram) : null;
      if (!writes) {
        console.warn(`[systems] working song for system ${id} dropped: this build has no layout to restore it into`);
        continue;
      }
      for (const w of writes) if (!this.backend.writeRam(id, w.offset, w.bytes)) break;
    }
  }

  /** Lift the working song out of `e` ahead of its teardown. A carry still pending for it wins: a second
   *  reset inside the boot window would otherwise lift the blank song the cart is still booting into. */
  private takeWorkingSong(e: SystemEntry): WorkingSongImage | null {
    const song = this.workingSongImage(e.id);
    this.carries.delete(e.id);
    // Flagged edited but byte-identical to a saved song: a song the cart has just LOADED and not yet
    // marked clean (it clears the flag a few blocks after writing the song) - or an edit that happens to
    // match a saved copy. Either way nothing is lost by carrying it clean, and carried edited it would
    // land "unsaved" forever on a cart that will never clear that flag. The slot is NOT moved here: once
    // it lands clean, the watcher's first observation retargets it from the cart's own state.
    const catalog = resolveSongCatalog(e.roles);
    const sram = song?.edited ? this.backend.readSram(e.id) : null;
    if (song && sram && catalog?.workingSongImageDirty && !catalog.workingSongImageDirty(sram, song)) {
      return { ...song, edited: false };
    }
    return song;
  }

  private carryWorkingSong(newId: number | null, song: WorkingSongImage | null): number | null {
    if (newId !== null && song) this.carries.set(newId, { song });
    return newId;
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
    for (const e of this.entries) {
      this.flushBeforeDrop(e);
      this.backend.removeSystem(e.id);
    }
    this.entries = [];
    this.focusedId = 0;
    this.carries.clear();
    this.successors.clear();
    this.observed.clear();
    this.observedEdited.clear();
    this.landing.clear();
    this.landedAt.clear();
  }

  /** Reconstruct one system from a serialized config entry, preserving its EXACT
   *  savSuffix + savPath-override (no free-suffix reassignment). Appends; focuses the
   *  first. Returns the new id, or null when the ROM won't classify. `blobs` (present
   *  only for a zip-import) seed the emulator's SRAM/savestate from the archive instead
   *  of from disk; `savPath` stays the auto-save target.
   *
   *  QUIET ON PURPOSE: unlike every other mutation this does NOT markDirty(), so the
   *  store→DSP projection does not run. A project load adopts N systems and then fires
   *  ONE onSystemsChange (see projectStore.load), rather than re-projecting N times.
   *  A standalone caller must therefore trigger a sync itself - any markDirty-ing call
   *  (e.g. setRoleConfig) will do - or the system boots and renders VIDEO while the DSP
   *  knows nothing about it and it produces NO AUDIO. */
  adopt(
    config: {
      core?: Core; // persisted since the v2 project schema (else auto-derived from platform)
      romPath?: string;
      savPath?: string;
      savSuffix?: number;
      embeddedRom?: string;
      settings?: Partial<CommonSettings>;
      roles?: RoleInstance[];
      workingSlot?: TrackedSlot;
    },
    // `song`: a working song the exported project was still carrying across a reboot (see
    // ProjectStore.buildArchive) - parked again on the adopted system, to land once its cart is up.
    blobs?: { sramBytes?: Uint8Array; stateBytes?: Uint8Array; song?: WorkingSongImage },
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
    if (!this.backend.constructSystem(spec, id)) return null;
    // Stored settings/roles win; a config that omits them re-attaches defaults.
    const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
    const wasEmpty = this.entries.length === 0;
    const battery = this.detectBattery(romPath, embeddedRom, platform);
    const workingSlot = config.workingSlot ? { ...config.workingSlot } : undefined;
    this.entries = appendEntry(this.entries, { id, platform, core, romPath, savPath: override, savSuffix, embeddedRom, battery, settings, roles, workingSlot });
    if (wasEmpty) this.focusedId = id;
    this.syncSerialOutCapture(id); // a loaded project may carry a system already in MIDIOUT mode
    if (blobs?.song) this.carryWorkingSong(id, { ...blobs.song, slot: blobs.song.slot ?? workingSlot });
    return id;
  }

  // --- internals ----------------------------------------------------------

  // Replace the focused tile (or adopt into an empty project), focusing the result.
  private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
    const empty = this.entries.length === 0;
    const target = empty ? undefined : this.effectiveFocus();
    const built = this.construct(romPath, embeddedRom, this.replacementSuffix(target, romPath), explicitSav, target);
    if (!built) return null;
    this.entries = empty
      ? appendEntry(this.entries, built.entry)
      : replaceById(this.entries, target as number, built.entry);
    if (!empty) this.successors.set(target as number, built.id);
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
   *  - **Appended, not merged in place.** `kernelProjection` maps this array straight to the DSP
   *    pipeline in order, and `defaultRoles` itself emits core-role-then-providers, so appending
   *    reproduces the order a freshly added system would have.
   *
   *  A stored kind that is no longer registered is left alone rather than pruned: `DspKernel` already
   *  skips unknown kinds when building the pipeline, so it is inert, and dropping it would silently
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
    if (!this.registry) return spec;
    let s = spec;
    for (const r of roles) {
      const rt = this.registry.roleType(r.kind);
      if (rt?.onConstruct) s = rt.onConstruct(s, this.backend, r.config);
    }
    return s;
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

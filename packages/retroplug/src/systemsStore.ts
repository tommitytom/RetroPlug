// SystemsStore: the live systems list as the app owns it. TS is the source of truth
// for order, ids-as-handles, focus, and dirty; the Backend is a narrow service that puts the byte
// resources a system boots from and places the graph a document builds (docs/modular-graph-phase-c3-design.md
// section 6), and every path it is handed is CONCRETE - TS resolved it. All the orchestration lives here:
// classify a ROM (TS-side, from its header), disambiguate a sav suffix against the live list, resolve a
// paired-sav override, pick a sibling ROM, boot a system from its resources, and apply the
// load/add/replace + focus rules.
//
// Mirrors RecentStore's shape (constructor(backend, onChange), view(), mutators,
// no-op-guarded change signal). Reproduces the legacy native service's constructSystem /
// duplicateSystem / reload orchestration and the DSP list handlers
// (PluginDSP.cpp:406-458), with every path derived by the pure kernels.

import type { ConstructSpec, ControlPlaneBackend, HostBackend } from "./backend";
import {
  droppedKnobs,
  exportModeOf,
  linkGroupOf,
  systemKnobs,
  systemNodeId,
  type GraphSystem,
} from "./graph/buildGraph";
import { bootSystem, ResourcePool, systemHandles } from "./graph/systemResources";
import { canonicalJson } from "./graph/canonicalJson";
import { EngineGraph } from "./engineGraph";
import {
  detectPlatform,
  romHasBattery,
  ROM_SNIFF_LEN,
  SEGA_SNIFF_LEN,
  defaultCoreFor,
  systemNodeTypeFor,
  type Platform,
  type Core,
} from "./platform";

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
import { protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
import { blipToasterChip, needsChipLabelScan, BLIPTOASTER_CHIP_SCAN_LEN } from "./bliptoaster/romDetect";

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
  if (platform === "sms" || platform === "gg")
    return backend.readFilePrefix(romPath, SEGA_SNIFF_LEN) ?? new Uint8Array();

  const short = backend.readFilePrefix(romPath, ROLE_HEADER_LEN) ?? new Uint8Array();
  if (platform !== "nes" || !needsChipLabelScan(short)) return short;
  return backend.readFilePrefix(romPath, BLIPTOASTER_CHIP_SCAN_LEN) ?? short;
}

interface RoleHeaderCaps {
  readFilePrefix(path: string, length: number): Uint8Array | null;
}

/** The role schema's parse of a stored role config: its fields as this build reads them, each defaulted, clamped or
 *  rounded. A load holds the stored config against it to note a key no build reads (SystemsStore.adopt); the stored
 *  config itself stays as written, so a load in a build that knows a newer label keeps it. A role this registry does
 *  not register has no schema to parse by, and comes back as it is. */
function parsedRoleConfig(registry: RoleRegistry | undefined, role: RoleInstance): Record<string, unknown> {
  const rt = registry?.roleType(role.kind);
  return rt ? (rt.schema.parse(role.config) as Record<string, unknown>) : role.config;
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
  /** The handles on the engine's resources every live entry's node names (graph/systemResources.ts): the ROM a
   *  rebuild keeps, the ROM a duplicate shares, and the seeds each boot put. */
  private readonly pool: ResourcePool;
  private onFocusChange: () => void = () => {};
  // A store on its own publishes its systems at the default (stereo) routing, with no MIDI routing and no
  // controller, through a graph of its own; the ProjectStore that owns one replaces this with its own publish, which
  // adds the project's audio and MIDI routing and its controller.
  private ownGraph: EngineGraph | null = null;
  private onGraphChange: (rearmTap: boolean) => boolean = (rearmTap) => {
    this.ownGraph ??= new EngineGraph(this.backend);
    return this.ownGraph.publish({ systems: this.graphSystems(), routing: "stereo" }, { rearmTap });
  };
  // A live param edit updates the graph's model without publishing, so a later publish (a render's shape among them)
  // builds the value the node runs rather than the stale one the last document carried.
  private onLiveParam: (systems: GraphSystem[]) => void = (systems) => {
    this.ownGraph?.systemsMovedLive(systems);
  };
  // Whether a core teardown writes the battery to disk first. Off only for an offline render.
  private batteryFlush = true;
  /** The ids a rebuild replaced, A -> B, so `reset` written against the pre-C3 contract still reaches the system (see
   *  reset): a chain A -> B -> C follows to the live C. Only `reset` reads it. An entry goes when the live system its
   *  chain ends in does (remove, discard, clear), and the whole map is capped, because a session that resets one system
   *  a thousand times would otherwise keep a thousand dead ids. */
  private readonly replacedBy = new Map<number, number>();

  constructor(
    private readonly backend: ControlPlaneBackend,
    private readonly onChange: () => void = () => {},
    private readonly registry?: RoleRegistry,
  ) {
    this.pool = new ResourcePool(backend);
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
  /** The systems as the engine's graph places them, in slot order (graph/buildGraph.ts), each with the roles that
   *  have a protocol node, in order, as its pipeline, its core-config role's config and its gain: the features with no
   *  DSP behaviour (asset overrides, a ROM's facts) stay out of the document, so an asset edit leaves it as it was, and
   *  so does a role whose kind is already placed or whose node would write an input an earlier role's does (one writer
   *  per input, graph/protocolNodes.ts). The core config reaches the document as its system node's knobs
   *  (systemKnobs): its setting fields as live settings, a NES's expansion volume, like the gain, as a param, and its
   *  construct-time fields beside them for the node's build. The node's resources are the entry's handles. The same
   *  with a registry or without one. */
  graphSystems(): GraphSystem[] {
    return this.entries.map((e) => {
      const coreConfig = e.roles.find((r) => r.kind === e.core)?.config;
      const system: GraphSystem = {
        id: e.id,
        platform: e.platform,
        type: systemNodeTypeFor(e.platform),
        linkGroup: linkGroupOf(coreConfig),
        exportMode: exportModeOf(coreConfig),
        resources: e.resources,
        romName: e.romName,
        pipeline: protocolPipeline(e.roles),
        gainDb: e.settings.gainDb,
      };
      return coreConfig ? { ...system, coreConfig } : system;
    });
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

  /** Install the hook that publishes the engine's graph (the ProjectStore's, which adds the routing, the MIDI
   *  routing and the controller; without one the store publishes its systems at the default stereo routing itself).
   *  Called once at the end of every edit that changes what the graph places for a system, BEFORE the structural
   *  onChange: `rearmTap` for an edit that builds or drops a core, which re-arms the NES tap. False when the engine
   *  refused the document, which an edit reads to put its model back (commitEdit). */
  setOnGraphChange(fn: (rearmTap: boolean) => boolean): void {
    this.onGraphChange = fn;
  }

  /** Install the hook that keeps the graph's model in step with a live param edit (the ProjectStore's, which owns
   *  the graph; without one the store's own graph takes it). A param set live sends no document, but the next one a
   *  shape change builds must carry what the node runs. */
  setOnLiveParam(fn: (systems: GraphSystem[]) => void): void {
    this.onLiveParam = fn;
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
    const prepared = this.construct(romPath, "", suffix, opts?.explicitSav);
    if (!prepared) return null;
    const wasEmpty = this.entries.length === 0;
    if (!this.commitEdit("add", {
      entries: appendEntry(this.entries, prepared.entry),
      focusedId: wasEmpty ? prepared.entry.id : this.focusedId,
      put: prepared.handles,
    })) return null;
    this.markDirty();
    return prepared.entry.id;
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
    const prepared = this.construct(romPath, "", this.replacementSuffix(id, romPath), opts?.explicitSav);
    if (!prepared) return null;
    return this.replaceWith(id, prev, prepared);
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
    const prepared = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, sramBytes);
    if (!prepared) return null;
    return this.replaceWith(id, prev, prepared);
  }

  /** Put `prepared` in system `id`'s slot, replacing `prev`: the system's identity and focus are kept, its resources
   *  given back only once the document that stops naming them has planned. Null when that document was refused. */
  private replaceWith(id: number, prev: SystemEntry, prepared: Prepared): number | null {
    const focused = this.focusedId === id;
    if (!this.commitEdit("replace", {
      entries: replaceById(this.entries, id, prepared.entry),
      focusedId: focused ? prepared.entry.id : this.focusedId,
      put: prepared.handles,
      drop: systemHandles(prev.resources),
    })) return null;
    this.rememberReplacement(id, prepared.entry.id);
    this.markDirty();
    return prepared.entry.id;
  }

  /** Clone a system's LIVE state into a fresh appended instance with its own suffix. Orchestrated in
   *  TS: pull the source's savestate from the registry (it includes SRAM), then boot an INDEPENDENT
   *  core seeded from those bytes: the source's role config reaches the clone's node, so the clone
   *  boots the same model the savestate was captured under, and from the source's own ROM resource,
   *  which the two then share. No native duplicate method. */
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
    }, src.roles);
    const prepared = this.boot(spec, {
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
    }, { rom: src.resources.rom });
    if (!prepared) {
      console.warn(`[systems] duplicateSystem(${id}) failed (a resource would not put) - no instance added`);
      return null;
    }
    const entries = appendEntry(this.entries, prepared.entry);
    if (!this.commitEdit("duplicate", { entries, put: prepared.handles })) return null;
    this.markDirty();
    return newId;
  }

  /** Drop a system; refocus the front if it was focused. False when absent. */
  removeSystem(id: number): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    this.flushBeforeDrop(e);
    const entries = removeById(this.entries, id);
    if (!this.commitEdit("remove", {
      entries,
      focusedId: nextFocusAfterRemove(entries, id, this.focusedId),
      drop: systemHandles(e.resources),
    })) return false;
    this.forgetReplacementsEndingIn(id);
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
    // A reload reads the ROM from disk again (the file watcher is a caller), so its resource is put fresh rather than
    // shared with the core being replaced.
    return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }, { rereadRom: true });
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
    opts: { savPathOverride?: string; roles?: RoleInstance[]; rereadRom?: boolean } = {},
  ): number | null {
    const src = findById(this.entries, id);
    if (!src) return null;
    const override = opts.savPathOverride ?? src.savPath;
    const roles = opts.roles ?? src.roles;
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
    }, roles);
    // The same ROM boots again, so the new node names the ROM resource the old one did: the boot takes a second
    // handle on it (and puts its own where the bytes moved, an asset role's new image among them), and the old
    // entry's handle goes with the old entry once the new document has planned.
    const prepared = this.boot(spec, {
      ...src,
      id: newId,
      savPath: override,
      roles,
    }, { rom: opts.rereadRom === true ? undefined : src.resources.rom });
    if (!prepared) return null;
    return this.replaceWith(id, src, prepared);
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
    return this.rebuildInPlace(id, { sramBytes: bytes }, { savPathOverride: override });
  }

  /** Reboot system `id` in place, carrying its live battery SRAM forward — a hardware-style reset: the
   *  save persists, the running state is dropped. Reconstructs from the ROM (the same engine
   *  as reload) rather than a live GB_reset. Null when `id` is absent.
   *
   *  `id` may be one a rebuild replaced (the store's `replacedBy` map): a system's id does not survive a rebuild, and
   *  the pre-C3 SDK told a caller to follow a construct-time `setRoleConfig` with `reset(id)` to learn the id of the
   *  system that carried the new settings. `setRoleConfig` now rebuilds itself, but a reset of the old id keeps
   *  working: it resets what that id became, following a chain of rebuilds to the live system, and answers the new
   *  id. Only reset follows the map - every other verb treats a replaced id as gone, so a stale handle cannot remove
   *  or edit a system it never named. */
  reset(id: number): number | null {
    const live = this.liveEntryFor(id);
    if (!live) return null;
    this.flushBeforeDrop(live);
    return this.rebuildInPlace(live.id, { sramBytes: this.backend.readSram(live.id) ?? undefined });
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
    const newId = this.rebuildInPlace(
      id,
      { sramBytes: new Uint8Array(BLANK_SRAM_BYTES) },
      { savPathOverride: override },
    );
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

  /** Set a system's audio gain (dB, clamped): its node's `gain` param, set live (backend.setParam, at the next block)
   *  with no publish. The next document, whatever publishes it, carries the same value. */
  setGain(id: number, db: number): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    const gainDb = clampGain(db);
    this.entries = replaceById(this.entries, id, { ...e, settings: { ...e.settings, gainDb } });
    this.backend.setParam(systemNodeId(id), "gain", gainDb);
    this.onLiveParam(this.graphSystems());
    this.markDirty();
    return true;
  }

  /** Toggle reload-on-ROM-change: TS's alone, read by the file watcher (fileWatcher.ts), so nothing native hears it. */
  setReloadOnRomChange(id: number, on: boolean): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    this.entries = replaceById(this.entries, id, { ...e, settings: { ...e.settings, reloadOnRomChange: on } });
    this.markDirty();
    return true;
  }

  /** Edit a role's config (validated/clamped via its RoleType). False when the role is absent. What reaches the
   *  engine is what the published graph carries. A role with a protocol node is placed with its config as that node's
   *  settings, so its edit publishes (an lsdj-sync's mode among them, and with it the capture its Game Boy's node
   *  runs). The core-config role (kind === core) is its system node's knobs (systemKnobs): an edit moving a setting
   *  field publishes, and one moving only a param (a NES's expansion volume) sets it live (backend.setParam) with no
   *  publish. One moving a CONSTRUCT-TIME field (the SD root, the FIFO, the N8 link, the export mode, FM) rebuilds the
   *  system: those are read once, when its node builds its core, so the store runs the reset path itself - a new id,
   *  the same ROM resource, the live battery carried - and the new value is in the document that replaces the old one.
   *  A Game Boy's link group is wiring, so a change of its value publishes. Any other role (an asset override, a ROM's
   *  facts) never reaches the engine. False, the system as it was, when the rebuild could not be placed. */
  setRoleConfig(id: number, roleKind: string, partial: Record<string, unknown>): boolean {
    const e = findById(this.entries, id);
    if (!e) return false;
    const idx = e.roles.findIndex((r) => r.kind === roleKind);
    if (idx < 0) return false;
    const rt = this.registry?.roleType(roleKind);
    const was = e.roles[idx].config;
    const merged = { ...was, ...partial };
    const config = rt ? rt.schema.parse(merged) : merged;
    const roles = e.roles.slice();
    roles[idx] = { kind: roleKind, config };
    const type = systemNodeTypeFor(e.platform);
    // The core config's knobs before and after, both through systemKnobs: before over the stored config as written,
    // which is what the running document carried, and after over the edit's schema parse. A construct-time field is
    // written in full with the node's default where the stored config holds no value it takes, so a sparse config
    // names the same values a fresh one does; a stored value the schema repairs becomes one the node takes, which is
    // a different value from the one the live core was built with, and takes the rebuild it needs. A setting field that
    // moved is a document of its own, a param that moved is set live unless that document carries it anyway, and a
    // construct-time field that moved is a new system.
    const before = systemKnobs({ type, coreConfig: was });
    const after = systemKnobs({ type, coreConfig: config });
    const linkGroupMoved = roleKind === e.core && linkGroupOf(was) !== linkGroupOf(config);
    const params: [string, number][] = [];
    if (roleKind === e.core) {
      for (const [param, value] of Object.entries(after.params))
        if (before.params[param] !== value) params.push([param, value]);
    }
    if (roleKind === e.core && canonicalJson(before.construct) !== canonicalJson(after.construct)) {
      // A construct-time edit IS a reset, and a reset carries the battery: the same rows the System menu's Reset runs.
      this.flushBeforeDrop(e);
      return this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }, { roles }) !== null;
    }
    const entries = replaceById(this.entries, id, { ...e, roles });
    const settingMoved = roleKind === e.core && canonicalJson(before.settings) !== canonicalJson(after.settings);
    if (linkGroupMoved || settingMoved || protocolNodeOf(roleKind) !== undefined) {
      if (!this.commitEdit("setRoleConfig", { entries }, false)) return false;
    } else {
      this.entries = entries;
      if (params.length > 0) {
        for (const [param, value] of params) this.backend.setParam(systemNodeId(id), param, value);
        this.onLiveParam(this.graphSystems());
      }
    }
    this.markDirty();
    return true;
  }

  /** Link a freshly-created instance to the one it was created from: the child joins the parent's link group,
   *  and if the parent is ungrouped (0) the parent is promoted to group 1 so the pair is linked. A no-op unless
   *  BOTH are SameBoy (GB) systems — link groups are a Game Boy link-cable concept. Routes through
   *  setRoleConfig, so dirty and the publish are handled. */
  inheritLinkGroup(childId: number, parentId: number): void {
    const parentSb = findById(this.entries, parentId)?.roles.find((r) => r.kind === "sameboy");
    const childSb = findById(this.entries, childId)?.roles.find((r) => r.kind === "sameboy");
    if (!parentSb || !childSb) return;
    const g = (parentSb.config as { linkGroupId?: number }).linkGroupId ?? 0;
    const target = g > 0 ? g : 1;
    if (g === 0) this.setRoleConfig(parentId, "sameboy", { linkGroupId: target }); // promote the lone parent
    this.setRoleConfig(childId, "sameboy", { linkGroupId: target }); // child joins the parent's group
  }

  // --- project-load rebuild seam ------------------------------------------
  // clear()/adopt() are QUIET: no dirty/onChange, since a load isn't a user edit
  // (the ProjectStore sets dirty explicitly around load/new).

  /** Tear down every system + reset the list/focus (for `new` + before a load). Publishes the empty graph
   *  (a load builds its systems only after their predecessors are gone, so it never holds both), but no
   *  onChange: the caller notifies once it is done. Every system's resources go once that document has planned. */
  clear(): void {
    const had = this.entries.length > 0;
    for (const e of this.entries) this.flushBeforeDrop(e);
    const dropped = this.entries.flatMap((e) => systemHandles(e.resources));
    this.entries = [];
    this.focusedId = 0;
    // Every chain ends in a system that no longer exists, so no old id can name anything a later document places.
    this.replacedBy.clear();
    if (had) {
      // A refused empty document leaves the old plan running and the entries already gone, so nothing else will ever
      // release these handles: warn, then give them back anyway. The old plan keeps its own pins until it is reclaimed.
      if (!this.onGraphChange(true))
        console.warn(
          "[systems] clear: the engine refused the empty document; its systems' resources are released anyway",
        );
      for (const id of dropped) this.pool.drop(id);
    }
  }

  /** Drop system `id` from the list without a document of its own, answering the entry it was (or undefined when it
   *  was not placed): what a project load does with the system the engine refused, so the document that stops naming
   *  it is the load's next publish. Its resources go at once: that document never planned, so no instance pinned them,
   *  and no later document names them. */
  discard(id: number): SystemEntry | undefined {
    const e = findById(this.entries, id);
    if (!e) return undefined;
    this.entries = removeById(this.entries, id);
    this.focusedId = nextFocusAfterRemove(this.entries, id, this.focusedId);
    for (const handle of systemHandles(e.resources)) this.pool.drop(handle);
    this.forgetReplacementsEndingIn(id);
    return e;
  }

  /** Reconstruct one system from a serialized config entry, preserving its EXACT
   *  savSuffix + savPath-override (no free-suffix reassignment). Appends; focuses the
   *  first. Returns the new id, or null when the ROM won't classify. `blobs` (present
   *  only for a zip-import) seed the emulator's SRAM/savestate from the archive instead
   *  of from disk; `savPath` stays the auto-save target.
   *
   *  QUIET ON PURPOSE: unlike every other mutation this does NOT markDirty() or fire onChange. It
   *  does publish the graph, which carries the system's protocol nodes, so the core and its roles
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
    // Stored roles win, each config that reaches a protocol node parsed first (parsedForTheirNodes), but a
    // role kind they PREDATE is added (see withMissingFeatureRoles), and of two that collide the one this ROM
    // calls for is placed (suggestedFirst); a config that omits them entirely re-attaches defaults. Known
    // before the build so a role's load-time hook can seed the spec (e.g. an empty LSDj sav) before
    // instantiation. What the load repairs or leaves out is noted here and logged once the core has built,
    // so a failed build says nothing about a system that was never added.
    const rom = romPath || `the embedded ${embeddedRom}`;
    const notes: string[] = [];
    const suggested = this.defaultRoles(core, platform, romPath, embeddedRom);
    const stored = config.roles?.length ? this.parsedForTheirNodes(config.roles, core, suggested, rom, notes) : null;
    const roles = stored
      ? this.suggestedFirst(this.withMissingFeatureRoles(stored, core, suggested, romPath, embeddedRom), suggested)
      : suggested;
    // A loaded system's roles can hold two whose nodes write one input, or a kind twice (graph/protocolNodes.ts,
    // beside PROTOCOL_NODES), and every publish leaves the later one out of its pipeline, quietly; this notes it
    // for the log, which is written once the core has built, where the roles enter.
    protocolPipeline(roles, (r) =>
      notes.push(
        r.input === null
          ? `[systems] ${rom}: its second ${r.kind} role is not placed`
          : `[systems] ${rom}: its ${r.kind} role is not placed: its node would write ${r.input}, ` +
              `which its ${r.by} role's already does`,
      ),
    );
    // The core-config "system" role (kind === core) carries the emulator config, and it rides the document as its
    // system node's settings: a loaded non-default model/highpass is applied at the core's build (or live, for C2's
    // knobs), never by a post-construct restart that would nuke the just-restored savestate. Parsed before the boot, so
    // a throw here leaves nothing put, and once: the note below holds the stored config against it.
    const systemRole = config.roles?.find((r) => r.kind === core);
    const parsed = systemRole ? parsedRoleConfig(this.registry, systemRole) : undefined;
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
    }, roles);
    // Stored settings/roles win; a config that omits them re-attaches defaults. Parsed, like the battery read,
    // before the build, so a throw here leaves no core behind.
    const settings = commonSettingsSchema.parse(config.settings ?? {}) as CommonSettings;
    const battery = this.detectBattery(romPath, embeddedRom, platform);
    const prepared = this.boot(spec, {
      id,
      platform,
      core,
      romPath,
      savPath: override,
      savSuffix,
      embeddedRom,
      battery,
      settings,
      roles,
    });
    if (!prepared) return null; // its LSDj capture, if any, comes with the document that places it
    // The stored core config stays as written, so the system node leaves out a value its node would refuse (or a
    // param's value that is not a finite number), and a key no build reads; its own default applies to each. Say which:
    // the stored config and the document then disagree.
    if (systemRole) {
      const refused = droppedKnobs(systemNodeTypeFor(platform), systemRole.config);
      const reported = new Set(refused.map(([field]) => field));
      // `gameGear` is written by buildGraph from the platform, not read from the stored config, so a Game Gear
      // document carries it whether or not the stored config does (and the role schema knows no such key).
      const supplied = platform === "gg" ? "gameGear" : null;
      const changes = [
        ...refused.map(([field, value]) => `dropped ${field} ${JSON.stringify(value)}`),
        ...Object.entries(systemRole.config)
          .filter(
            ([field]) =>
              field !== supplied && !reported.has(field) && !Object.prototype.hasOwnProperty.call(parsed, field),
          )
          .map(([field, value]) => `dropped ${field} ${JSON.stringify(value)}`),
      ];
      if (changes.length)
        notes.push(
          `[systems] ${rom}: its stored ${core} config holds a value this build does not read as written: ` +
            changes.join(", "),
        );
    }
    for (const note of notes) console.warn(note);
    const wasEmpty = this.entries.length === 0;
    if (!this.commitEdit("adopt", {
      entries: appendEntry(this.entries, prepared.entry),
      focusedId: wasEmpty ? id : this.focusedId,
      put: prepared.handles,
    })) return null;
    return id;
  }

  // --- internals ----------------------------------------------------------

  // Replace the focused tile (or adopt into an empty project), focusing the result.
  private loadInPlace(romPath: string, embeddedRom: string, explicitSav?: string): number | null {
    const empty = this.entries.length === 0;
    const target = empty ? undefined : this.effectiveFocus();
    const prepared = this.construct(romPath, embeddedRom, this.replacementSuffix(target, romPath), explicitSav);
    if (!prepared) return null;
    if (empty) {
      const placed = this.commitEdit("load", {
        entries: appendEntry(this.entries, prepared.entry),
        focusedId: prepared.entry.id,
        put: prepared.handles,
      });
      if (!placed) return null;
      this.markDirty();
    } else {
      const prev = findById(this.entries, target as number);
      if (!prev || this.replaceWith(target as number, prev, prepared) === null) return null;
    }
    this.focusedId = prepared.entry.id;
    return prepared.entry.id;
  }

  // Classify (unless embedded), resolve the concrete savPath, and boot from the ROM and seeds.
  // Returns the entry to record with the handles its boot took, or null when rejected/failed.
  private construct(
    romPath: string,
    embeddedRom: string,
    suffix: number,
    explicitSav: string | undefined,
    sramBytes?: Uint8Array, // seed the cold-boot battery (swap-ROM-preserve-SRAM); undefined = read the .sav from disk
  ): Prepared | null {
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
    // Roles are known before the boot (a pure function of core/platform/header), so a role's
    // load-time hook can seed the spec — e.g. an empty LSDj sav — before the core is instantiated.
    const roles = this.defaultRoles(core, platform, romPath, embeddedRom);
    const spec = this.applyConstructHooks(
      { romPath, platform, core, embeddedRom, savPath, statePath: null, sramBytes },
      roles,
    );
    const battery = this.detectBattery(romPath, embeddedRom, platform); // before the boot: a throw leaves nothing put
    return this.boot(spec, {
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
    });
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

  /** `stored` with each config that reaches a protocol node parsed by its role's schema, as an edit's and a fresh
   *  system's already are. A node's settings are strict, and the planner refuses the whole document for one node's, so
   *  a value the file holds that this build does not read (a newer build's added mode, which is additive and so no
   *  version bump; a v2 file's out-of-range mode index, which the v3 step leaves for this schema; a hand edit) would
   *  silence the project. A field the schema's helpers repair (configSchema.ts: an unknown enum defaults, an integer
   *  clamps) is repaired. A config the schema refuses outright becomes what this ROM's providers suggest for the role
   *  (`suggested`: a Game Gear smsggdj's sms-sync is "gg", where the schema's default is "sms"), or the schema's
   *  defaults when they suggest none. A config it accepts keeps the value of every field the schema reads and gains
   *  only the defaults its node would fill anyway, but loses a key the schema does not read (a zod object strips it,
   *  as the node's settings ignore it), so the next save no longer carries it. Each repair and dropped key is noted in
   *  `notes`, which adopt logs once the core has built.
   *
   *  Only the roles with a protocol node: the core's config role must stay as written (withMissingFeatureRoles says
   *  why), and a role with no node never reaches the graph. A kind this registry does not register has no schema to
   *  parse by here, and stays as it is. */
  private parsedForTheirNodes(
    stored: RoleInstance[],
    core: Core,
    suggested: readonly RoleInstance[],
    rom: string,
    notes: string[],
  ): RoleInstance[] {
    const registry = this.registry;
    if (!registry) return stored;
    const has = (o: object, k: string): boolean => Object.prototype.hasOwnProperty.call(o, k);
    return stored.map((r) => {
      const rt = r.kind !== core && protocolNodeOf(r.kind) !== undefined ? registry.roleType(r.kind) : undefined;
      if (!rt) return r;
      let config: Record<string, unknown>;
      try {
        config = rt.schema.parse(r.config);
      } catch {
        const suggestion = suggested.find((d) => d.kind === r.kind)?.config;
        config = suggestion ? { ...suggestion } : rt.schema.parse({});
        notes.push(
          `[systems] ${rom}: its stored ${r.kind} config ${JSON.stringify(r.config)} is not one this build reads; ` +
            `using ${suggestion ? "what this ROM calls for" : "the defaults"}, ${JSON.stringify(config)}`,
        );
        return { kind: r.kind, config };
      }
      const given = r.config;
      const changes = [
        ...Object.keys(config)
          .filter((k) => has(given, k) && JSON.stringify(given[k]) !== JSON.stringify(config[k]))
          .map((k) => `repaired ${k} ${JSON.stringify(given[k])} to ${JSON.stringify(config[k])}`),
        ...Object.keys(given).filter((k) => !has(config, k)).map((k) => `dropped ${k}`),
      ];
      const what = `[systems] ${rom}: its stored ${r.kind} config holds what this build does not read`;
      if (changes.length) notes.push(`${what}: ${changes.join(", ")}`);
      return { kind: r.kind, config };
    });
  }

  /** `roles` with each role this ROM's providers suggest (`suggested`, defaultRoles) moved ahead of the earliest role
   *  they do not suggest whose node writes an input its own node writes, so of two that collide the pipeline places
   *  the protocol today's ROM calls for (graph/protocolNodes.ts: of two writers of one input, the first is placed).
   *  Met when the file at a ROM path changes builds: a project stored with mgb whose path now holds LSDj gains
   *  lsdj-sync as a missing feature role, and first-wins would keep running mGB's protocol on an LSDj cart, whose menu
   *  then shows sync controls that do nothing (and the reverse on an mGB cart). Every other role keeps its place, so
   *  with no collision, or none the ROM settles (no registry, or neither role suggested), the order is as stored and
   *  the first wins. A save writes the moved order, which is harmless: the suggested role goes ahead of one a fresh
   *  system on this ROM would not have at all. */
  private suggestedFirst(roles: RoleInstance[], suggested: readonly RoleInstance[]): RoleInstance[] {
    const wanted = new Set(suggested.map((r) => r.kind));
    const out = roles.slice();
    for (let i = 0; i < out.length; ++i) {
      if (!wanted.has(out[i].kind)) continue;
      const writes = protocolWrites(out[i].kind);
      const collides = (q: RoleInstance) => protocolWrites(q.kind).some((w) => writes.includes(w));
      const j = out.findIndex((q, k) => k < i && !wanted.has(q.kind) && collides(q));
      if (j >= 0) out.splice(j, 0, ...out.splice(i, 1));
    }
    return out;
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
   *  - **Feature roles only.** The core-config role (kind === core) is skipped: its config is the emulator's, which
   *    a system node reads as its settings (graph/buildGraph.ts systemKnobs), and synthesising one here would
   *    write schema defaults into a project that stores none, where the node's own defaults already apply.
   *  - **A stored role is never touched** here, so a configured sync mode or a kit override survives
   *    (adopt has parsed one with a protocol node by its schema first, which keeps the value of every field
   *    the schema reads).
   *  - **Appended, not merged in place.** `graphSystems` keeps this array's order for the DSP
   *    pipeline, and `defaultRoles` itself emits core-role-then-providers, so appending
   *    reproduces the order a freshly added system would have. (adopt then moves a suggested
   *    role ahead of a stale one it collides with: suggestedFirst.)
   *
   *  A stored kind this registry does not register is left alone rather than pruned (and unparsed, as
   *  there is no schema to parse it by): dropping it would silently discard state belonging to a build
   *  the user might go back to. What the graph places is the protocol-node table's, not the registry's
   *  (graph/protocolNodes.ts): a kind with no node, a retired one among them, places nothing, so it is
   *  inert; a kind with a node is placed whether this registry registers it or not.
   *
   *  No schema change and no version bump: nothing migrates the file, and the augmented list only
   *  reaches disk if the user saves - at which point the project self-heals. */
  private withMissingFeatureRoles(
    stored: RoleInstance[],
    core: Core,
    suggested: readonly RoleInstance[],
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
    const missing = suggested.filter((r) => r.kind !== core && !have.has(r.kind));
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

  /** How many replaced ids the store remembers. A chain older than this can lose a middle link and then answer null,
   *  which is what a caller saw for any id it had not just used; the cap is what keeps a long editing session from
   *  growing the map with every reset. */
  private static readonly REPLACED_LIMIT = 128;

  /** Remember that a rebuild replaced `oldId` with `newId`, dropping the oldest entries past the limit (a Map iterates
   *  in insertion order, so that is the first key). */
  private rememberReplacement(oldId: number, newId: number): void {
    this.replacedBy.set(oldId, newId);
    for (const key of this.replacedBy.keys()) {
      if (this.replacedBy.size <= SystemsStore.REPLACED_LIMIT) break;
      this.replacedBy.delete(key);
    }
  }

  /** The live entry `id` became, following a chain of rebuilds; the entry `id` itself when it was never replaced. */
  private liveEntryFor(id: number): SystemEntry | undefined {
    return findById(this.entries, this.chainEnd(id));
  }

  // The last id of a rebuild chain, without asking the entries (a caller may be removing that system).
  private chainEnd(id: number): number {
    let cur = id;
    for (let next = this.replacedBy.get(cur); next !== undefined; next = this.replacedBy.get(cur)) cur = next;
    return cur;
  }

  /** Forget every chain that ends in `id`, which is about to stop being live: the ids that named it, and it. */
  private forgetReplacementsEndingIn(id: number): void {
    const dead: number[] = [];
    for (const oldId of this.replacedBy.keys()) if (this.chainEnd(oldId) === id) dead.push(oldId);
    for (const oldId of dead) this.replacedBy.delete(oldId);
  }

  // Boot `spec` and make the entry it becomes: the one place a store boot goes through, so every verb puts its
  // resources the same way, takes the same handles and reports a put's refusal the same way. `rest` is the entry's
  // identity and settings, everything but the ROM's name and the resources, which the boot answers.
  private boot(
    spec: ConstructSpec,
    rest: Omit<SystemEntry, "romName" | "resources">,
    opts: { rom?: number } = {},
  ): Prepared | null {
    const booted = bootSystem(this.pool, spec, opts);
    if ("error" in booted) {
      console.warn(`[systems] ${rest.romPath || `the embedded ${rest.embeddedRom}`}: ${booted.error}`);
      return null;
    }
    return {
      entry: { ...rest, romName: booted.romName, resources: booted.resources },
      handles: booted.handles,
    };
  }

  // Record an edit and publish the document that places it. A refused document puts the list and the focus back,
  // gives back what the edit put, and sends the restored model again so the next edit starts from the graph that
  // runs (EngineGraph keeps a refused document's systems as its model). The resources an edit REPLACES are released
  // only once its document has planned: a dropped resource cannot be bound by a new instance, and a refused
  // document's nodes are built afresh.
  private commitEdit(
    what: string,
    edit: { entries: SystemEntry[]; focusedId?: number; put?: number[]; drop?: number[] },
    rearmTap = true,
  ): boolean {
    const entries = this.entries;
    const focusedId = this.focusedId;
    this.entries = edit.entries;
    if (edit.focusedId !== undefined) this.focusedId = edit.focusedId;
    if (this.onGraphChange(rearmTap)) {
      for (const id of edit.drop ?? []) this.pool.drop(id);
      return true;
    }
    console.warn(`[systems] ${what}: the engine refused the document; the edit is rolled back`);
    this.entries = entries;
    this.focusedId = focusedId;
    for (const id of edit.put ?? []) this.pool.drop(id);
    if (!this.onGraphChange(rearmTap))
      console.warn(`[systems] ${what}: the restored model was refused too; the engine keeps the previous plan`);
    return false;
  }
}

/** A system TS has booted, its resources put and its handles held, but not yet placed: the entry it becomes and the
 *  handles its boot took, which an edit gives back when its document is refused. */
interface Prepared {
  entry: SystemEntry;
  handles: number[];
}


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
  cartNodeId,
  drivenCartSystem,
  drivesRealCartOf,
  droppedKnobs,
  exportModeOf,
  linkGroupOf,
  systemKnobs,
  systemNodeId,
  type GraphSystem,
} from "./graph/buildGraph";
import { bootSystem, ResourcePool, systemHandles } from "./graph/systemResources";
import { canonicalJson } from "@retroplug/core/canonicalJson";
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
import type { RoleRegistry, RoleInstance } from "@retroplug/core/systemRoles";
import { protocolNodeOf, protocolPipeline, protocolWrites } from "./graph/protocolNodes";
import { blipToasterChip, needsChipLabelScan, BLIPTOASTER_CHIP_SCAN_LEN } from "./bliptoaster/romDetect";
import { resolveSongCatalog, resolveTracker } from "./trackerRegistry";
import type { TrackedSlot, WorkingSongImage } from "./tracker/songCatalog";

// Frames the slot watcher waits for a landed carry's queued writes to appear before it trusts the cart
// again; they normally appear on the next audio block, so this only bounds a cart that moved on.
const LANDING_FRAMES = 120;
// Frames a clean working song that matched no saved slot is re-checked before the verdict sticks (~1 s):
// long enough to outlast a battery that lags work RAM by a block or two, short enough to cost nothing.
const UNMATCHED_RECHECKS = 60;

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
  /** The handles on the engine's resources every live entry's node names (graph/systemResources.ts): every boot puts
   *  its own ROM and seeds, and a system holds one handle on each until it goes. */
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
  // Working songs being carried across a reboot, keyed by the NEW system id - see carryWorkingSong.
  // No time budget: a carry is keyed by a system id, which dies with its system, so one that waits can
  // only ever land in the cart it was made for. (A budget used to drop it after ~15 s of UI frames, which
  // a host that stops processing audio - the cart frozen mid-boot - spent without the cart moving at all.)
  private carries = new Map<number, { song: WorkingSongImage }>();
  // Each system's last OBSERVED clean working song: its change signal, whether it matched a saved slot,
  // and how many more frames an unmatched one is re-checked - see observeWorkingSongs.
  private observed = new Map<number, { signal: number; dir: number; matched: boolean; retries: number }>();
  // A carry's writes are queued, so for a block or so the cart still holds the blank song it booted into.
  // Until the restored song shows up the CARRIED song is the working song: the watcher must not read the
  // blank as an in-cart NEW, and Save, the unsaved prompt and an export must not read it as the song.
  // id -> the restored song, the change signal of the BLANK it is replacing, frames waited. Keyed on the
  // blank, not on the restored song: the window is exactly "RAM still shows the blank", so the moment
  // anything else is there - the restore, or an edit already made on top of it - the cart speaks for
  // itself again. Keyed on the restored song instead, an edit landing with the restore kept the window
  // open, and a Save in it stored the pre-edit song and marked the newer one saved.
  private landing = new Map<number, { song: WorkingSongImage; blank: number; blankEdited: boolean; frames: number }>();
  // Each system's observed `edited` alongside its signal (see observeWorkingSongs).
  private observedEdited = new Map<number, boolean>();
  // Old id -> the id its in-place replacement was built under, for ANY in-place change (a rebuild, a ROM swap or
  // replace, a load over the focused tile) - see currentId. Unlike `replacedBy`, which only `reset` follows and only
  // a rebuild records, this follows a system the user still sees in that slot; capped like it.
  private readonly successors = new Map<number, number>();
  // A battery a project load must write over its system's restored savestate once the engine places it (see adopt).
  private readonly heldSram = new Map<number, Uint8Array>();

  constructor(
    private readonly backend: ControlPlaneBackend,
    private readonly onChange: () => void = () => {},
    private readonly registry?: RoleRegistry,
  ) {
    this.pool = new ResourcePool(backend);
  }

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
    // The working song rides along too, for the same reason the battery does - a version bump that keeps
    // your save should keep your song. Dropped at settle if the new ROM is not a cart that can take it.
    const song = this.takeWorkingSong(prev);
    const prepared = this.construct(romPath, "", this.replacementSuffix(id, romPath), undefined, sramBytes);
    if (!prepared) return null;
    return this.carryWorkingSong(this.replaceWith(id, prev, prepared), song);
  }

  /** Put `prepared` in system `id`'s slot, replacing `prev`: the slot and the focus are kept, its resources given
   *  back only once the document that stops naming them has planned. Null when that document was refused. The
   *  `replacedBy` map `reset` follows is `rebuildInPlace`'s, since that is the in-place change that keeps the
   *  system's identity; a caller here may be swapping in a different game. The `successors` map `currentId` follows
   *  is recorded here, for every in-place change: what the user sees in that slot is the new system either way. */
  private replaceWith(id: number, prev: SystemEntry, prepared: Prepared): number | null {
    const focused = this.focusedId === id;
    if (!this.commitEdit("replace", {
      entries: replaceById(this.entries, id, prepared.entry),
      focusedId: focused ? prepared.entry.id : this.focusedId,
      put: prepared.handles,
      drop: systemHandles(prev.resources),
    })) return null;
    this.rememberSuccessor(id, prepared.entry.id);
    this.forgetWorkingSongState(id);
    this.markDirty();
    return prepared.entry.id;
  }

  /** Clone a system's LIVE state into a fresh appended instance with its own suffix. Orchestrated in
   *  TS: pull the source's savestate from the registry (it includes SRAM), then boot an INDEPENDENT
   *  core seeded from those bytes: the source's role config reaches the clone's node, so the clone
   *  boots the same model the savestate was captured under, and the ROM is read again from its source,
   *  as every boot reads its own. No native duplicate method. */
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
      battery: src.battery, // the same source, read again
      settings: { ...src.settings },
      roles: src.roles.map((r) => ({ kind: r.kind, config: { ...r.config } })),
    });
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
    this.forgetWorkingSongState(id);
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
    const song = this.takeWorkingSong(prev);
    return this.carryWorkingSong(this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }), song);
  }

  /** Reconstruct system `id` in place from seed bytes: capture the source's spec, build a fresh core
   *  under a new id that takes the old one's slot, and keep identity + focus. The seed (sramBytes
   *  = cold-boot battery, stateBytes = boot from savestate) overrides what native would read from disk.
   *  `savPathOverride` repoints the auto-save target (New SRAM…); omitted, the system keeps its own. The
   *  shared body of reload / loadState / loadSram / newSramAs: the in-place twin of duplicateSystem
   *  (which appends), and every boot reads its ROM again from its source. This is also the one place that
   *  records `old -> new` for `reset`: it rebuilds a system rather than swapping in another one, so it is the
   *  only in-place change the pre-C3 `reset(oldId)` contract follows. */
  private rebuildInPlace(
    id: number,
    seed: { sramBytes?: Uint8Array; stateBytes?: Uint8Array },
    opts: { savPathOverride?: string; roles?: RoleInstance[] } = {},
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
    // Every rebuild reads the ROM again from its source (a fresh put by path, embedded marker or the asset roles'
    // folded bytes), so a file a user rebuilt on disk is what Reset, New SRAM, Load SRAM, Load State and a
    // construct-time edit boot. The old entry's handles go with the old entry once the new document has planned.
    // The tracked slot does NOT survive a rebuild: Load State / Load SRAM / New SRAM / a Songs-menu edit
    // all replace or renumber what the cart holds, and a stale slot let a Save write a restored song over
    // the unrelated one it had named. Reset and reload, which keep the song, hand the slot on with it in
    // the carried image (see carryWorkingSong).
    const prepared = this.boot(spec, {
      ...src,
      id: newId,
      savPath: override,
      roles,
      workingSlot: undefined,
    });
    if (!prepared) return null;
    const rebuilt = this.replaceWith(id, src, prepared);
    if (rebuilt === null) return null;
    this.rememberReplacement(id, rebuilt);
    return rebuilt;
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
    const song = this.takeWorkingSong(live);
    return this.carryWorkingSong(
      this.rebuildInPlace(live.id, { sramBytes: this.backend.readSram(live.id) ?? undefined }),
      song,
    );
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
    if (land && (!song || (catalog.fingerprintOf?.(song) === land.blank && song.edited === land.blankEdited))) return land.song;
    return song && e!.workingSlot ? { ...song, slot: e!.workingSlot } : song;
  }

  /** Record that system `id`'s working song is the saved song at `index` - it was just loaded from there,
   *  or saved into it - as the battery holds that slot NOW. What a later Save writes back over instead of
   *  appending a copy. Stored on the system, so it is persisted with the project and survives a reset or
   *  a savestate restore; it is bookkeeping, not an edit, so it does not dirty the project. */
  noteSongSlot(id: number, index: number): void {
    const e = findById(this.entries, id);
    const sram = e ? this.backend.readSram(id) : null;
    const catalog = e ? resolveSongCatalog(e.roles) : undefined;
    const slot = e && sram ? (catalog?.trackSlot?.(sram, index) ?? null) : null;
    this.setWorkingSlot(id, slot ?? undefined);
    // The battery just moved by OUR hand (a host save): rebase the watcher's directory baseline, so its next
    // look does not take this change for the CART saving - which it lets an exact match win over drift.
    const seen = this.observed.get(id);
    if (seen && sram && catalog?.directorySignal) seen.dir = catalog.directorySignal(sram);
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
        if (fp === land.blank && song.edited === land.blankEdited && ++land.frames < LANDING_FRAMES) continue;
        this.landing.delete(e.id);
        // Landed as written: the song is where it was meant to go - track that slot and settle this state.
        // An edited song is trusted to its slot (it is the host's own record of where it came from); a clean
        // one only while that slot still holds it, or holds it but for drift that happened before the
        // reboot - else it is a song the cart had just loaded from elsewhere, and normal matching decides.
        const intended = land.song.slot;
        if (fp === catalog.fingerprintOf(land.song) && song.edited === land.song.edited && intended && this.slotStillFits(e.id, intended, song)) {
          this.setWorkingSlot(e.id, intended);
          this.observed.set(e.id, { signal: fp, dir: catalog.directorySignal?.(this.backend.readSram(e.id) ?? new Uint8Array(0)) ?? 0, matched: true, retries: 0 });
          this.observedEdited.set(e.id, song.edited);
          continue;
        }
      }
      const sram = this.backend.readSram(e.id);
      if (!sram) continue;
      // The battery's directory is part of the question too: a song the cart SAVES to a new row without
      // being edited changes the battery and not the working song, and keying on the song alone left the
      // tracking on the old row - where the next host Save then wrote, over the original.
      const dir = catalog.directorySignal?.(sram) ?? 0;
      const seen = this.observed.get(e.id);
      // An unchanged song is settled once it matched a slot. One that matched NONE is re-checked for a
      // while: a cart that saves clears its unsaved flag in the same routine that writes its battery, and
      // reading the two a block apart once took a just-saved song for a NEW one and dropped its slot for
      // good, because the verdict was cached.
      const same = seen && seen.signal === fp && seen.dir === dir && this.observedEdited.get(e.id) === song.edited;
      if (same && (seen.matched || seen.retries-- <= 0)) continue;
      // Only an UNEDITED song moves the tracking. An edited one says nothing about which slot it is - even
      // byte-identical to a saved song: that is also what one frame of an edit passing through an older
      // copy's value looks like, and taking it as a load moved the tracking onto the old copy for good, so
      // Save overwrote it. The cart's own loads clear its flag a few blocks after writing the song, and
      // THAT observation retargets.
      if (song.edited) {
        this.observed.set(e.id, { signal: fp, dir, matched: true, retries: 0 });
        this.observedEdited.set(e.id, true);
        continue;
      }
      // Still the tracked song with only what PLAYBACK rewrites changed (a `T` command rewrites the active
      // groove inside the block, and never sets the flag): keep the tracking - and keep it even when that
      // drift happens to make the song byte-identical to ANOTHER saved slot (an older version that differed
      // only in tempo), which a plain match took for an in-cart load, moving Save onto the older version.
      // Unless the battery's directory moved too: then the cart SAVED (the drifted song, to a new row), and
      // the exact match is where it went.
      const dirMoved = !!seen && seen.dir !== dir;
      const drift = !dirMoved && e.workingSlot && catalog.isPlaybackDrift?.(sram, e.workingSlot, song) ? e.workingSlot : null;
      const slot = drift ?? catalog.slotOfCleanSong(sram, { ...song, slot: e.workingSlot });
      if (!same) this.observed.set(e.id, { signal: fp, dir, matched: !!slot, retries: UNMATCHED_RECHECKS });
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

  /** Host writes that put `song` into system `id`'s work RAM have been QUEUED: until they show up, `song`
   *  is the working song as far as Save, the unsaved prompt, an export and a song request are concerned -
   *  not whatever the cart still holds. Used for a carry landing and for a host Load: a Save in the gap
   *  after a Load (the audio thread stalled, the writes not applied) stored the very song the user had just
   *  agreed to discard, over its own saved slot. The window is "RAM unchanged from when the writes were
   *  queued" (content AND unsaved flag), so any other change - the writes landing, or the cart moving on -
   *  ends it. None at all when RAM already equals `song`: there is nothing to wait out, and a window keyed
   *  on the song's own content swallowed the cart's next same-content LOAD. */
  expectSong(id: number, song: WorkingSongImage, ramNow?: Uint8Array): void {
    const e = findById(this.entries, id);
    const catalog = e ? resolveSongCatalog(e.roles) : undefined;
    if (!catalog?.fingerprintOf || !catalog.readWorkingSong) return;
    const ram = ramNow ?? this.backend.readRam(id) ?? undefined;
    const before = ram ? catalog.readWorkingSong(ram) : null;
    if (!before) return;
    const blank = catalog.fingerprintOf(before);
    // `song.slot` is where this song belongs - the carried slot, or the slot a Load is loading. It becomes
    // the tracking when the writes are seen to land (observeWorkingSongs), or now if RAM already holds the
    // song. Leaving it to the watcher's own matching instead let an exact twin elsewhere win: an explicit
    // Load of row 0 onto a cart already holding row 0's content kept tracking another row, and a session
    // reopened from a stale-transport savestate lost the drift relation and retargeted to a twin.
    if (blank === catalog.fingerprintOf(song) && before.edited === song.edited) {
      if (song.slot && this.slotStillFits(id, song.slot, song)) this.setWorkingSlot(id, song.slot);
      return;
    }
    this.landing.set(id, { song: { ...song }, blank, blankEdited: before.edited, frames: 0 });
  }

  /** Whether `slot` is still where `song` belongs: always for an edited song (the host's own record of where
   *  it came from), and for a clean one while that slot holds it - or holds it but for groove drift that
   *  happened before a reboot (a restore does not carry the transport flag, so the content alone decides). */
  private slotStillFits(id: number, slot: TrackedSlot, song: WorkingSongImage): boolean {
    if (song.edited) return true;
    const e = findById(this.entries, id);
    const catalog = e ? resolveSongCatalog(e.roles) : undefined;
    const sram = this.backend.readSram(id);
    if (!catalog || !sram) return false;
    return catalog.slotOfCleanSong?.(sram, { ...song, slot })?.index === slot.index || !!catalog.isPlaybackDrift?.(sram, slot, song, true);
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

  /** The working song a project export must ship BESIDE the savestate, for a console that keeps it in work
   *  RAM: always, read live (or the carry still to land). The savestate is republished only every ~0.5 s,
   *  while the battery, the tracked slot and this song are all current - so a session saved in that lag
   *  reopened as a mix of moments: a song saved since but reading clean over a battery that predated the
   *  save (silent loss), or a stale song tracked to a slot it never came from (Save overwrote it). Shipped
   *  live with the battery written over the savestate's on load, the reopened cart is one moment again. */
  exportableWorkingSong(id: number): WorkingSongImage | null {
    return this.pendingCarry(id) ?? this.workingSongImage(id);
  }

  /** Write each battery a project load adopted for a system whose graph it held back (adopt's `freshSram`) over the
   *  savestate's own, now that the engine places the system: the ProjectStore calls it once the load's systems are
   *  placed. A system the engine still does not place keeps nothing: its battery is the savestate's. */
  writeHeldBatteries(): void {
    for (const [id, bytes] of this.heldSram) {
      if (!this.backend.writeSram(id, 0, bytes))
        console.warn(`[systems] system ${id}: the battery its project carried could not be written over its savestate`);
    }
    this.heldSram.clear();
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
      this.expectSong(id, { ...c.song, slot: c.song.slot ?? e.workingSlot }, ram);
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
    // Content only: the OPTIONS differing (the dirty predicate counts those) does not make the SONG new,
    // and counting it here carried a just-loaded song as edited with the slot it replaced.
    if (song && sram && catalog?.slotOfCleanSong?.(sram, { ...song, edited: false })) {
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
   *  the ROM read again from its source, the live battery carried - and the new value is in the document that replaces
   *  the old one.
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
    // which is what the running document carried - after a param-only edit it is that edit's parse unpublished, whose
    // construct fields are the values the live core was built with, which is what made the edit param-only - and after
    // over the edit's schema parse. A construct-time field is written in full with the node's default where the stored
    // config holds no value it takes, so a sparse config names the same values a fresh one does; a stored value the
    // schema repairs becomes one the node takes, which is a different value from the one the live core was built with,
    // and takes the rebuild it needs. A setting field that moved is a document of its own, a param that moved is set
    // live unless that document carries it anyway, and a construct-time field that moved is a new system.
    const before = systemKnobs({ type, coreConfig: was });
    const after = systemKnobs({ type, coreConfig: config });
    const linkGroupMoved = roleKind === e.core && linkGroupOf(was) !== linkGroupOf(config);
    // Whether the system drives a physical N8 is placement, not a knob: the document either places its rp.n8.nes or
    // does not, so a change publishes rather than riding setParam or a live settings commit.
    const cartMoved = roleKind === e.core && drivesRealCartOf(was) !== drivesRealCartOf(config);
    const params: [string, number][] = [];
    if (roleKind === e.core) {
      for (const [param, value] of Object.entries(after.params))
        if (before.params[param] !== value) params.push([param, value]);
    }
    if (roleKind === e.core && canonicalJson(before.construct) !== canonicalJson(after.construct)) {
      // A construct-time edit IS a reset, and a reset carries the battery and a work-RAM working song (see reset):
      // the same rows the System menu's Reset runs. (SMS FM Audio is one: read only at construct.)
      this.flushBeforeDrop(e);
      const song = this.takeWorkingSong(e);
      const rebuilt = this.rebuildInPlace(id, { sramBytes: this.backend.readSram(id) ?? undefined }, { roles });
      return this.carryWorkingSong(rebuilt, song) !== null;
    }
    const entries = replaceById(this.entries, id, { ...e, roles });
    const settingMoved = roleKind === e.core && canonicalJson(before.settings) !== canonicalJson(after.settings);
    if (linkGroupMoved || cartMoved || settingMoved || protocolNodeOf(roleKind) !== undefined) {
      if (!this.commitEdit("setRoleConfig", { entries }, false)) return false;
    } else {
      this.entries = entries;
      if (params.length > 0) {
        // The system the document places the cart for after this edit (buildGraph's own rule): its node carries the same
        // knob as its own param, and a live edit publishes no document, so without this the real console keeps the level
        // the last published document gave it. A system that drives no cart - or drives one that another system is
        // placed for - places no cart node, so the second call is skipped rather than addressed at a node that is not
        // there.
        const systems = this.graphSystems();
        const drivesCart = drivenCartSystem(systems)?.id === id;
        for (const [param, value] of params) {
          this.backend.setParam(systemNodeId(id), param, value);
          if (param === "expansionVolume" && drivesCart) this.backend.setParam(cartNodeId(id), param, value);
        }
        this.onLiveParam(systems);
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
    // Every node a capability job named goes with this project's graph: what is still pending learns so now.
    this.backend.rejectPendingJobs("the project closed");
    const had = this.entries.length > 0;
    for (const e of this.entries) this.flushBeforeDrop(e);
    const dropped = this.entries.flatMap((e) => systemHandles(e.resources));
    this.entries = [];
    this.focusedId = 0;
    // Every chain ends in a system that no longer exists, so no old id can name anything a later document places.
    this.replacedBy.clear();
    this.carries.clear();
    this.successors.clear();
    this.observed.clear();
    this.observedEdited.clear();
    this.landing.clear();
    this.heldSram.clear();
    if (had) {
      // A refused empty document leaves the old plan running and the entries already gone, so nothing else will ever
      // release these handles: warn, then give them back anyway. The old plan keeps its own pins until it is reclaimed.
      if (!this.onGraphChange(true))
        console.warn(
          "[systems] clear: the engine refused the empty document; its systems' resources are released anyway",
        );
      for (const id of dropped) {
        try {
          this.pool.drop(id);
        } catch (e) {
          // The entries are already gone, so nothing else will ever offer these handles: a release the transport died
          // on is said and the next one attempted, rather than stranding every handle behind it.
          console.warn(`[systems] clear: releasing a system's resource threw: ${String(e)}`);
        }
      }
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
    // Called from a project load's retry, where the list is already shortened: a release the transport died on is said
    // rather than thrown, or the load fails with the handles behind this one held by an engine that named no system.
    for (const handle of systemHandles(e.resources)) {
      try {
        this.pool.drop(handle);
      } catch (err) {
        console.warn(`[systems] discard: releasing a dropped system's resource threw: ${String(err)}`);
      }
    }
    this.forgetReplacementsEndingIn(id);
    this.forgetWorkingSongState(id);
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
      workingSlot?: TrackedSlot;
    },
    // `song`: a working song the exported project was still carrying across a reboot (see
    // ProjectStore.buildArchive) - parked again on the adopted system, to land once its cart is up.
    // `freshSram`: a battery newer than the savestate's own, written over it once the core is built.
    blobs?: { sramBytes?: Uint8Array; stateBytes?: Uint8Array; song?: WorkingSongImage; freshSram?: Uint8Array },
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
      workingSlot: config.workingSlot ? { ...config.workingSlot } : undefined,
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
    // A working song the archive carried lands once the cart is up (settleCarries), and a battery newer than the
    // savestate's own is written over it, live, once the engine places the system: at once when this adopt's own
    // publish placed it, else (a project load holds the graph until every system is built) by writeHeldBatteries.
    if (blobs?.freshSram && !this.backend.writeSram(id, 0, blobs.freshSram)) this.heldSram.set(id, blobs.freshSram);
    if (blobs?.song) this.carryWorkingSong(id, { ...blobs.song, slot: blobs.song.slot ?? prepared.entry.workingSlot });
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

  /** Remember that an in-place change put `newId` in `oldId`'s slot (see currentId), capped like rememberReplacement. */
  private rememberSuccessor(oldId: number, newId: number): void {
    this.successors.set(oldId, newId);
    for (const key of this.successors.keys()) {
      if (this.successors.size <= SystemsStore.REPLACED_LIMIT) break;
      this.successors.delete(key);
    }
  }

  /** Drop the working-song bookkeeping of `id`, which has stopped being live (removed, discarded or replaced). */
  private forgetWorkingSongState(id: number): void {
    this.carries.delete(id);
    this.observed.delete(id);
    this.observedEdited.delete(id);
    this.landing.delete(id);
    this.heldSram.delete(id);
  }

  // Boot `spec` and make the entry it becomes: the one place a store boot goes through, so every verb puts its
  // resources the same way, takes the same handles and reports a put's refusal the same way. `rest` is the entry's
  // identity and settings, everything but the ROM's name and the resources, which the boot answers.
  private boot(spec: ConstructSpec, rest: Omit<SystemEntry, "romName" | "resources">): Prepared | null {
    const booted = bootSystem(this.pool, spec);
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
  // runs (EngineGraph keeps a refused document's systems as its model). A publish that THROWS is the engine's failure
  // rather than a refusal it answered, and EngineGraph counts it as refused (the engine keeps the plan it ran), so it
  // is put back the same way before the error reaches the caller: the edit's puts are given back and the restored
  // model is sent, so no later shape change rebuilds what the store rolled back. The resources an edit REPLACES are
  // released only once its document has planned: a dropped resource cannot be bound by a new instance, and a refused
  // document's nodes are built afresh. A release that throws is said and the next one attempted, on both paths: the
  // document's own answer is the caller's, not whatever the transport did to a handle afterwards.
  private commitEdit(
    what: string,
    edit: { entries: SystemEntry[]; focusedId?: number; put?: number[]; drop?: number[] },
    rearmTap = true,
  ): boolean {
    const entries = this.entries;
    const focusedId = this.focusedId;
    this.entries = edit.entries;
    if (edit.focusedId !== undefined) this.focusedId = edit.focusedId;
    const restore = (): void => {
      this.entries = entries;
      this.focusedId = focusedId;
      for (const id of edit.put ?? []) {
        try {
          this.pool.drop(id);
        } catch (e) {
          console.warn(`[systems] ${what}: giving back the edit's resource threw too: ${String(e)}`);
        }
      }
    };
    // The restored model's own send, after a refusal or a throw: the graph's model is the edit's either way, so the
    // plan the engine runs is only what a later publish builds once this lands. Its failure is said, never thrown:
    // the caller's own failure is the one it must see.
    const resend = (): void => {
      try {
        if (!this.onGraphChange(rearmTap))
          console.warn(`[systems] ${what}: the restored model was refused too; the engine keeps the previous plan`);
      } catch (e) {
        console.warn(`[systems] ${what}: the restored model's send threw too: ${String(e)}`);
      }
    };
    let planned: boolean;
    try {
      planned = this.onGraphChange(rearmTap);
    } catch (e) {
      restore();
      resend();
      throw e;
    }
    if (planned) {
      for (const id of edit.drop ?? []) {
        try {
          this.pool.drop(id);
        } catch (e) {
          // The document planned, so the edit is done and the verb's answer is its own: a release the transport died
          // on cannot take that away, and must not strand the handles behind it either. What the engine was never
          // told to drop stays live and unnamed - a leaked resource, not a wrong system.
          console.warn(`[systems] ${what}: releasing a replaced resource threw: ${String(e)}`);
        }
      }
      return true;
    }
    console.warn(`[systems] ${what}: the engine refused the document; the edit is rolled back`);
    restore();
    resend();
    return false;
  }
}

/** A system TS has booted, its resources put and its handles held, but not yet placed: the entry it becomes and the
 *  handles its boot took, which an edit gives back when its document is refused. */
interface Prepared {
  entry: SystemEntry;
  handles: number[];
}


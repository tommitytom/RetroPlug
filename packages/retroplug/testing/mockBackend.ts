// In-memory Backend double for tests. No disk, no native: a Map of
// canonical-path -> bytes, a fixed config dir, and a lexical canonicalizer.
// Deterministic and inspectable — seed files, read them back, list what's on
// "disk". This is what lets the whole application layer be tested with `tjs run`
// and nothing else.

import type { ApuState, ApuSquareState, Backend, BreakHitBatch, SetGraphRequest, SetGraphResult, LoadScriptPackResult, BreakInfo, Breakpoint, CallFrame, CoreTransportStats, CpuRegister, DebugEvent, DisasmLine, ExpansionAudioState, FileBrowserOpts, FrameData, PngImageData, PpuState, ProfiledFunction, PutResourceResult, ResourceSpec, ResourceType, TraceLine, ZipEntry } from "../src/backend";
import { savFromJson } from "@retroplug/lsdj";
import {
  jobHandle,
  type EngineJobKind,
  type EngineRunHandle,
  type EngineRunOptions,
  type OfflineRunHandle,
  type OfflineRunRequest,
} from "../src/offline";
import type { GraphDocument, NodeDoc } from "@retroplug/core/graphDocument";
import { rebuildKeyOf } from "../src/graph/buildGraph";
import { canonicalJson } from "@retroplug/core/canonicalJson";
import {
  CapabilityJobs,
  type CapabilityArgs,
  type CapabilityOp,
  type CapabilityProgress,
  type CapabilityResults,
  type JobCompletion,
  type JobRequest,
  type JobSubmitResult,
} from "../src/capability";

const enc = new TextEncoder();
const dec = new TextDecoder();

// Local zip magic + a trivial, faithful-inverse entry encoding for the mock's
// zip/unzip (real native uses miniz DEFLATE; the mock just needs round-trip fidelity).
const PK_MAGIC = [0x50, 0x4b, 0x03, 0x04];
function pushU32(out: number[], n: number): void {
  out.push(n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff);
}
function readU32(b: Uint8Array, off: number): number {
  return (b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)) >>> 0;
}

// Deterministic per-id pump bytes, so a test can assert the exact blob that landed in
// the export zip. Tagged ("ST"/"SR") + id so state and sram never collide.
export function stateBytesFor(id: number): Uint8Array {
  return Uint8Array.of(0x53, 0x54, id & 0xff, (id >>> 8) & 0xff);
}
export function sramBytesFor(id: number): Uint8Array {
  return Uint8Array.of(0x53, 0x52, id & 0xff, (id >>> 8) & 0xff);
}

// A constructed emulator, as the mock tracks it - enough to answer duplicate /
// reload / remove and to let tests inspect what the engine was asked to place. The mock runs no
// cores, so it reads one from the node a document places: the resources' sources say what a boot
// would be seeded with.
interface MockSystem {
  romPath: string;
  embeddedRom: string;
  savPath: string | null;
  statePath: string | null;
  /** True when the system was reconstructed from in-memory seed blobs (import, a carried battery). */
  restoredFromBytes: boolean;
}

// The node types a document places a system with: the ones whose settings name a rom resource
// (graph/systemResources.ts), and the only ones the mock checks for one.
const SYSTEM_NODE_TYPES = new Set(["rp.sameboy.gb", "rp.mesen.nes", "rp.mesen.gba", "rp.mesen.sms"]);

// A node's resource field as native's parser spells it (graph/GraphDocument.cpp parseResourceRef): `res:<id>` with
// one spelling per id, so no `res:007` and no `res:0`.
const RES_REF = /^res:([1-9]\d*)$/;

const refusedGraph = (nodeId: string, code: string, message: string): SetGraphResult => ({
  ok: false,
  errors: [{ nodeId, port: "", code, message }],
});

/** What a document would boot a system from, read out of the mock's table: a helper for tests, not a model - the mock
 *  runs no cores, so this is what the node's settings and the resources they name ADD UP to. */
export interface MockBoot {
  /** The system's id. */
  id: number;
  type: string;
  settings: Record<string, unknown>;
  /** The ROM's name (its node settings' `romName`), which a Master System stages its ROM under. */
  romName: string;
  /** Where the ROM came from: a file path, or an embedded ROM's marker ("" for one put from bytes). */
  romPath: string;
  embeddedRom: string;
  /** The path the battery seed was read from (`null` when it came from bytes or there is none), and the savestate's. */
  savPath: string | null;
  statePath: string | null;
  /** Each resource the node names, with the id, the spec it was put from, the bytes the spec carried when TS held
   *  them (`bytes`, undefined for a file the engine reads), and the bytes the engine holds (`content`). Absent when the
   *  node names none. */
  rom?: MockResource;
  sram?: MockResource;
  state?: MockResource;
  /** The bytes the ROM was put from, when TS held them (an asset role's patched image; a base ROM is a path), and
   *  likewise the seeds TS held. Undefined when the resource came from a file or an embedded marker. */
  romBytes?: Uint8Array;
  sramBytes?: Uint8Array;
  stateBytes?: Uint8Array;
}

/** A resource as a boot named it: the id, what it was put from, and the bytes the engine holds. */
export interface MockResource {
  id: number;
  spec: ResourceSpec;
  bytes: Uint8Array;
  /** Where the bytes came from: TS held them, the engine read a file, or a ROM baked into the binary. */
  source: "bytes" | "path" | "embedded";
}

export class MockBackend implements Backend {
  private files = new Map<string, Uint8Array>();
  private dir: string;

  /** Names of Backend methods called, in order — lets tests assert side-effects
   *  (e.g. that a write went through writeFileAtomic, not writeFile). */
  readonly log: string[] = [];

  // --- emulator-lifecycle bookkeeping (mock-only) -------------------------
  // (no id counter - TS owns ids and names them in its documents.)
  private systems = new Map<number, MockSystem>();
  /** Every putResource call, in order, with the id it got (0 when refused) - so a test can assert what crossed and in
   *  what order. Ids count up from 1 and are never reused, as native's are. */
  readonly putResourceCalls: { spec: ResourceSpec; id: number }[] = [];
  /** Every releaseResource call, in order (dropped or not), so a test can assert each put is balanced. */
  readonly releaseResourceCalls: number[] = [];
  /** Every setGraph request, in order: the documents TS published, their slots and their tap re-arms. */
  readonly graphRequests: SetGraphRequest[] = [];
  /** The resources a putResource created and their owner still holds: the type, the bytes and where they came from. */
  private resources = new Map<number, { type: ResourceType; bytes: Uint8Array; spec: ResourceSpec }>();
  private nextResourceId = 1;

  /** Every system id a document has placed, in the order each first appeared. A store's rebuild takes a new id and a
   *  publish that keeps a system's id boots nothing, so this counts the boots: the length to compare before and after
   *  an edit. */
  readonly bootedIds: number[] = [];

  // --- file-dialog bookkeeping (mock-only) --------------------------------
  /** Opts passed to each openFileBrowser call, in order — lets tests assert which
   *  dialog (ROM-or-sav vs ROM-only) was opened. */
  readonly fileBrowserCalls: FileBrowserOpts[] = [];
  /** One response per dialog the flow will open, consumed FIFO. `null` = cancel. */
  private browseQueue: (string | null)[] = [];

  /** Every setParam call, in order: the live param edits the stores made (a gain, a NES's expansion volume). */
  readonly paramCalls: { nodeId: string; paramId: string; value: number }[] = [];
  /** Joypad edges (backend.pressButton), in call order. */
  readonly pressButtonCalls: { id: number; button: number; down: boolean }[] = [];
  /** Control MIDI aimed at one node (backend.stageNodeMidi), in call order: the node id and its bytes. */
  readonly stageNodeMidiCalls: { nodeId: string; bytes: number[] }[] = [];

  /** Ids passed to the pump reads, in order. */
  readonly readStateCalls: number[] = [];
  readonly readSramCalls: number[] = [];
  /** Test-driven SRAM content per system (setSram), overriding the deterministic
   *  default — lets a test model SRAM changing over time (dedup vs write). */
  private sramOverrides = new Map<number, Uint8Array>();
  /** Test-driven WRAM content per system (setRam) — lets a mock-tier test feed the LSDj runtime reader
   *  a synthetic WRAM snapshot. Absent → readRam returns null (the mock has no real core). */
  private ramOverrides = new Map<number, Uint8Array>();
  private noBattery = new Set<number>();

  /** Paths queued by emitFileChange, drained by drainChangedPaths — simulates the
   *  native watcher (efsw) firing. */
  private changedPaths: string[] = [];
  /** The last ROM set passed to setWatchedRoms — for tests asserting the policy wiring. */
  watchedRoms: string[] = [];
  /** Entry lists passed to zip / archives passed to unzip, in order. */
  readonly zipCalls: ZipEntry[][] = [];
  readonly unzipCalls: Uint8Array[] = [];

  constructor(configDir = "/config") {
    this.dir = configDir;
  }

  /** Seed the responses openFileBrowser will resolve to, in dialog order. */
  queueBrowse(...responses: (string | null)[]): void {
    this.browseQueue.push(...responses);
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** Put a file on the fake disk (string is UTF-8 encoded). */
  seed(path: string, contents: string | Uint8Array): void {
    const bytes = typeof contents === "string" ? enc.encode(contents) : new Uint8Array(contents);
    this.files.set(this.canonicalize(path), bytes);
  }

  /** Drive a system's live SRAM content (what readSram returns), overriding the
   *  deterministic default — lets a test model SRAM changing between flushes. */
  setRam(id: number, bytes: Uint8Array): void {
    this.ramOverrides.set(id, new Uint8Array(bytes));
  }

  setSram(id: number, bytes: Uint8Array): void {
    this.sramOverrides.set(id, new Uint8Array(bytes));
  }

  /** Model a cart with no battery: readSram answers null for it (and sram.dump refuses). */
  setNoBattery(id: number): void {
    this.noBattery.add(id);
  }

  /** Simulate the native watcher firing for `path` (config.json / a bindings profile /
   *  a ROM) — the next drainChangedPaths returns it. */
  emitFileChange(path: string): void {
    this.changedPaths.push(path);
  }

  /** Read a file back as text, or null if absent. */
  readText(path: string): string | null {
    const b = this.readFile(path);
    return b ? dec.decode(b) : null;
  }

  /** Every path currently on the fake disk, sorted. */
  paths(): string[] {
    return [...this.files.keys()].sort();
  }

  // --- Backend ------------------------------------------------------------

  readFile(path: string): Uint8Array | null {
    this.log.push("readFile");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b) : null;
  }

  writeFile(path: string, bytes: Uint8Array): boolean {
    this.log.push("writeFile");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }

  writeFileAtomic(path: string, bytes: Uint8Array): boolean {
    // Atomicity is invisible in-memory; behave like writeFile but log distinctly.
    this.log.push("writeFileAtomic");
    this.files.set(this.canonicalize(path), new Uint8Array(bytes));
    return true;
  }

  appendFile(path: string, bytes: Uint8Array): boolean {
    this.log.push("appendFile");
    const key = this.canonicalize(path);
    const prev = this.files.get(key) ?? new Uint8Array(0);
    const out = new Uint8Array(prev.length + bytes.length);
    out.set(prev);
    out.set(bytes, prev.length);
    this.files.set(key, out);
    return true;
  }

  writeFileAt(path: string, offset: number, bytes: Uint8Array): boolean {
    this.log.push("writeFileAt");
    const key = this.canonicalize(path);
    const prev = this.files.get(key);
    if (!prev) return false; // mirrors native: the file must exist (fstream in|out doesn't create)
    const end = offset + bytes.length;
    const out = end > prev.length ? new Uint8Array(end) : new Uint8Array(prev.length);
    out.set(prev);
    out.set(bytes, offset);
    this.files.set(key, out);
    return true;
  }

  fileExists(path: string): boolean {
    this.log.push("fileExists");
    return this.files.has(this.canonicalize(path));
  }

  rename(from: string, to: string): boolean {
    this.log.push("rename");
    const cf = this.canonicalize(from);
    const bytes = this.files.get(cf);
    if (!bytes) return false;
    this.files.delete(cf);
    this.files.set(this.canonicalize(to), bytes);
    return true;
  }

  syncFile(path: string): boolean {
    this.log.push("syncFile");
    return this.files.has(this.canonicalize(path)); // a file on the fake disk is as durable as it gets
  }

  listDir(dir: string): string[] {
    this.log.push("listDir");
    const parent = this.canonicalize(dir);
    const prefix = parent === "/" ? "/" : parent + "/";
    // Immediate children: a direct file, or a subdirectory (marked with a trailing '/', matching the native
    // listDir contract) inferred from any deeper file path under `parent`.
    const names = new Set<string>();
    for (const key of this.files.keys()) {
      if (!key.startsWith(prefix)) continue;
      const rest = key.slice(prefix.length);
      const slash = rest.indexOf("/");
      names.add(slash < 0 ? rest : rest.slice(0, slash) + "/");
    }
    return [...names].sort();
  }

  deleteFile(path: string): boolean {
    this.log.push("deleteFile");
    return this.files.delete(this.canonicalize(path));
  }

  drainChangedPaths(): string[] {
    this.log.push("drainChangedPaths");
    const out = this.changedPaths;
    this.changedPaths = [];
    return out;
  }

  setWatchedRoms(paths: string[]): void {
    this.log.push("setWatchedRoms");
    this.watchedRoms = [...paths];
  }

  readFilePrefix(path: string, length: number): Uint8Array | null {
    this.log.push("readFilePrefix");
    const b = this.files.get(this.canonicalize(path));
    return b ? new Uint8Array(b.slice(0, length)) : null;
  }

  canonicalize(path: string): string {
    // Lexical normalize: absolutize against the config dir, collapse `.`/`..`
    // and duplicate separators. No symlink resolution (there's no real FS here),
    // which is faithful enough for tests — real symlink collapsing is the native
    // canonicalize's job.
    const abs = path.startsWith("/") ? path : `${this.dir}/${path}`;
    const out: string[] = [];
    for (const seg of abs.split(/[\\/]+/)) {
      if (seg === "" || seg === ".") continue;
      if (seg === "..") {
        if (out.length) out.pop();
      } else {
        out.push(seg);
      }
    }
    return "/" + out.join("/");
  }

  configDir(): string {
    return this.dir;
  }

  version(): string {
    return "0.0.0"; // placeholder; UI tests that assert a title set ctx.version explicitly
  }

  // --- Emulator lifecycle -------------------------------------------------

  // The resource table, as native's: an immutable byte block of one of three types, its id never reused. The mock has
  // no baked ROMs, so an embedded marker becomes a deterministic blob of its own, and only "mgb" exists (the one the
  // app loads), as native refuses a marker this build does not carry.
  putResource(spec: ResourceSpec): PutResourceResult {
    this.log.push("putResource");
    // A recorded call keeps its own copy, so a caller that mutates its Uint8Array afterwards cannot rewrite history.
    const record = (id: number): void => {
      this.putResourceCalls.push({
        spec: "bytes" in spec ? { ...spec, bytes: new Uint8Array(spec.bytes) } : { ...spec },
        id,
      });
    };
    const refused = (error: string): PutResourceResult => {
      record(0);
      return { error };
    };
    if (spec.type !== "rom" && spec.type !== "sram" && spec.type !== "state")
      return refused(`a resource type is "rom", "sram" or "state", not "${spec.type}"`);
    // Native's exactly-one-source rule (EngineRpcService.cpp): the union makes a malformed spec unrepresentable in
    // typed code, but a test that casts past it must see the engine's answer, not the first source found.
    const sources = spec as { bytes?: Uint8Array; path?: string; embedded?: string };
    const given =
      (sources.bytes != null ? 1 : 0) + (sources.path != null ? 1 : 0) + (sources.embedded != null ? 1 : 0);
    if (given !== 1) return refused(`a resource carries exactly one of bytes, path or embedded (${given} given)`);
    let bytes: Uint8Array | undefined;
    if (sources.bytes != null) bytes = sources.bytes;
    else if (sources.path != null) {
      // A file that is not there is refused; one that is there and empty is not, since an empty payload is a resource
      // (a zero-byte `.sav` is a blank battery). An empty rom is refused below, as a byte block is.
      bytes = this.files.get(this.canonicalize(sources.path));
      if (!bytes) return refused(`could not read ${JSON.stringify(sources.path)}: the file is absent or unreadable`);
    } else if (sources.embedded !== "mgb") return refused(`${JSON.stringify(sources.embedded)} is not an embedded ROM`);
    else bytes = enc.encode("embedded:mgb");
    // An empty payload is a legitimate seed (a blank battery), but a ROM has to have something to boot.
    if (bytes.length === 0 && spec.type === "rom")
      return refused("a rom resource cannot be empty: a zero-length ROM cannot boot");
    const id = this.nextResourceId++;
    this.resources.set(id, { type: spec.type, bytes: new Uint8Array(bytes), spec: "bytes" in spec ? { ...spec, bytes: new Uint8Array(spec.bytes) } : { ...spec } });
    record(id);
    return { id };
  }

  // The owner's drop: idempotent, and a dropped id stays dropped (it never comes back, and ids are never reused). Native
  // keeps a dropped resource alive while a plan still pins it, which the mock has no plans to model.
  releaseResource(id: number): boolean {
    this.log.push("releaseResource");
    this.releaseResourceCalls.push(id);
    return this.resources.delete(id);
  }

  /** The resource `putResource` created under `id`, or undefined when it was never created or has been dropped. */
  resource(id: number): { type: ResourceType; bytes: Uint8Array } | undefined {
    const r = this.resources.get(id);
    return r && { type: r.type, bytes: new Uint8Array(r.bytes) };
  }

  /** What the last document sent would boot `system` from, read out of the mock's resource table: its node's type,
   *  settings and ROM name, and each resource the node names with the spec it was put from. Undefined when the last
   *  document places no such node. */
  bootOf(system: number): MockBoot | undefined {
    const node = this.graphNode(`sys-${system}`);
    if (!node) return undefined;
    const settings = node.settings ?? {};
    const named = (field: string): MockBoot["rom"] => {
      const value = settings[field];
      const match = typeof value === "string" ? RES_REF.exec(value) : null;
      const held = match ? this.resources.get(Number(match[1])) : undefined;
      if (!held) return undefined;
      return {
        id: Number(match![1]),
        spec: held.spec,
        bytes: new Uint8Array(held.bytes),
        source: "bytes" in held.spec ? "bytes" : "path" in held.spec ? "path" : "embedded",
      };
    };
    const rom = named("rom");
    const sram = named("sram");
    const state = named("state");
    const path = (r: MockBoot["rom"]): string | null => (r && "path" in r.spec ? r.spec.path : null);
    return {
      id: system,
      type: node.type,
      settings,
      romName: typeof settings.romName === "string" ? settings.romName : "",
      romPath: path(rom) ?? "",
      embeddedRom: rom && "embedded" in rom.spec ? rom.spec.embedded : "",
      savPath: path(sram),
      statePath: path(state),
      rom,
      sram,
      state,
      romBytes: rom?.source === "bytes" ? rom.bytes : undefined,
      sramBytes: sram?.source === "bytes" ? sram.bytes : undefined,
      stateBytes: state?.source === "bytes" ? state.bytes : undefined,
    };
  }

  /** The system node each live system was last placed with, by id: a document that changes one's rebuild key under
   *  the id it already holds asks for a new core while the old one is live, which native refuses. */
  private systemNodes = new Map<number, NodeDoc>();

  /** The boot of the most recently booted system, in the last document that placed it: the convenience for the many
   *  cases that own one system. Undefined when no document placed one. */
  lastBoot(): MockBoot | undefined {
    const id = this.bootedIds[this.bootedIds.length - 1];
    return id === undefined ? undefined : this.bootOf(id);
  }

  // The slots become the live systems: every system the document does not place goes. The engine runs each system's
  // node from the resources its settings name (docs/modular-graph-phase-c3-design.md section 6), so this is where a
  // document naming a resource the engine does not hold is refused: an unknown or dropped id, a resource of another
  // type, a field that is not a reference at all, or no ROM. A system it already places is reused only while its
  // rebuild key holds: a document that moves one of those fields under the same id asks for a new core while the old
  // one is live, which native refuses with the same words.
  setGraph(request: SetGraphRequest): SetGraphResult {
    this.log.push("setGraph");
    this.graphRequests.push(request);
    const nodes = new Map(((JSON.parse(request.document) as GraphDocument).nodes ?? []).map((n) => [n.id, n]));
    const live = new Map<number, MockSystem>();
    const placed = new Map<number, NodeDoc>();
    for (const id of request.slots) {
      const node = nodes.get(`sys-${id}`);
      // Native names no node for this: the slots and the document's system nodes disagree, which is the document's
      // fault rather than one node's (Engine.cpp), so a load cannot read it as one system's refusal.
      if (!node) return refusedGraph("", "slots", "the slots and the document's system nodes name different systems");
      const problem = this.resourceProblem(node);
      if (problem) return refusedGraph(node.id, problem.code, problem.message);
      const was = this.systemNodes.get(id);
      if (was && canonicalJson(rebuildKeyOf(was)) !== canonicalJson(rebuildKeyOf(node)))
        return refusedGraph(node.id, "activate-failed", `system ${id} is live`);
      live.set(id, this.systemOf(node));
      placed.set(id, node);
      if (!this.bootedIds.includes(id)) this.bootedIds.push(id);
    }
    this.systems = live;
    this.systemNodes = placed;
    // The document's cart nodes, which the capability jobs address by id (one rp.n8.nes at most, its type unique).
    this.cartNodes = new Set([...nodes.values()].filter((n) => n.type === "rp.n8.nes").map((n) => n.id));
    return { ok: true, errors: [] };
  }

  /** Why the resources a system node names would fail its create, or null when they are all live and of its type. */
  private resourceProblem(node: NodeDoc): { code: string; message: string } | null {
    if (!SYSTEM_NODE_TYPES.has(node.type)) return null;
    const settings = node.settings ?? {};
    // Native reads every settings field before it validates or creates anything, so a ref that is not "res:N" is
    // refused first, as the settings read refuses it (rfl names the field it was reading).
    for (const field of ["rom", "sram", "state"] as const) {
      const value = settings[field];
      if (value === undefined || value === "") continue;
      if (typeof value !== "string" || !RES_REF.test(value))
        return {
          code: "settings-invalid",
          message: `Failed to parse field '${field}': ${JSON.stringify(value)} is not a resource reference (res:N)`,
        };
    }
    for (const [field, type] of [["rom", "rom"], ["sram", "sram"], ["state", "state"]] as const) {
      const value = settings[field];
      if (value === undefined) {
        if (field === "rom") return { code: "settings-invalid", message: `a system node names its ROM as {"rom": "res:N"}` };
        continue;
      }
      // Native reads an empty sram or state as none (id 0); an empty rom names nothing a node could boot.
      if (value === "" && field !== "rom") continue;
      const match = typeof value === "string" ? RES_REF.exec(value) : null;
      if (!match) return { code: "create-failed", message: `${field}: ${JSON.stringify(value)} names no resource` };
      const held = this.resources.get(Number(match[1]));
      if (!held) return { code: "create-failed", message: `${field}: "${value}" is not a live resource` };
      if (held.type !== type) return { code: "create-failed", message: `${field}: "${value}" is a ${held.type} resource, not a ${type}` };
    }
    return null;
  }

  /** The system a node places, as the mock's reads and tests see it: its ROM's source and seeds, read off the
   *  resources its settings name. */
  private systemOf(node: NodeDoc): MockSystem {
    const settings = node.settings ?? {};
    const spec = (field: string): ResourceSpec | undefined => {
      const value = settings[field];
      const id = typeof value === "string" ? RES_REF.exec(value) : null;
      return id ? this.resources.get(Number(id[1]))?.spec : undefined;
    };
    const rom = spec("rom");
    const sram = spec("sram");
    const state = spec("state");
    const path = (s: ResourceSpec | undefined): string | null => (s && "path" in s ? s.path : null);
    return {
      romPath: path(rom) ?? "",
      embeddedRom: rom && "embedded" in rom ? rom.embedded : "",
      savPath: path(sram),
      statePath: path(state),
      restoredFromBytes: (sram !== undefined && "bytes" in sram) || (state !== undefined && "bytes" in state),
    };
  }

  // Script packs need a JS engine to run their nodes in, which the mock does not have: it refuses every one, before
  // any replan, as native refuses a pack it cannot load.
  loadScriptPack(): LoadScriptPackResult {
    this.log.push("loadScriptPack");
    return { ok: false, error: "the mock backend runs no script packs", errors: [], types: [] };
  }

  /** The system the build `call` displaced from its slot: the id that held that slot in the document before
   *  the first one placing `call.id`. Undefined for a build that was appended, or never placed. */
  replacedBy(call: { id: number }): number | undefined {
    const at = this.graphRequests.findIndex((r) => r.slots.includes(call.id));
    if (at <= 0) return undefined;
    const slot = this.graphRequests[at].slots.indexOf(call.id);
    const before = this.graphRequests[at - 1].slots[slot];
    return before !== undefined && !this.graphRequests[at].slots.includes(before) ? before : undefined;
  }

  // duplicate + reload are TS orchestration (SystemsStore) over readState/readSram and the document it publishes; the
  // mock has no bespoke method, and its systems' seeds are read off the document's resources (bootOf/restoredIds).

  readState(id: number): Uint8Array | null {
    this.log.push("readState");
    this.readStateCalls.push(id);
    return this.systems.has(id) ? stateBytesFor(id) : null;
  }

  readSram(id: number): Uint8Array | null {
    this.log.push("readSram");
    this.readSramCalls.push(id);
    if (this.noBattery.has(id)) return null;
    const override = this.sramOverrides.get(id);
    if (override) return new Uint8Array(override);
    return this.systems.has(id) ? sramBytesFor(id) : null;
  }

  readRam(id: number): Uint8Array | null {
    this.log.push("readRam");
    const override = this.ramOverrides.get(id);
    if (override) return new Uint8Array(override);
    return null; // the mock has no real core → no WRAM unless a test set one
  }

  /** Mirrors the native contract: write into the SAME buffer readRam returns, refuse out-of-bounds, and
   *  refuse when there is no RAM at all (the mock has none until a test calls setRam). */
  writeRam(id: number, offset: number, bytes: Uint8Array): boolean {
    this.log.push("writeRam");
    const ram = this.ramOverrides.get(id);
    if (!ram || !bytes.length) return false;
    if (offset < 0 || offset + bytes.length > ram.length) return false;
    ram.set(bytes, offset);
    return true;
  }

  // --- capability jobs (src/capability.ts) over the mock's model: a system node is sys-<id>, its memory region "ram"
  // is setRam's buffer, its battery what setSram / readSram answer and its ROM the seeded file its ROM path names. As
  // natively: a read completes at once, a write at once while no audio runs and at the next runBlock() while it does
  // (audioRunning), and at most 64 jobs are queued (a held cart transfer is the cart's own, one at a time, outside
  // that count). A node the document places that is not a system presents no capability; a full command ring is not
  // modelled (that is the ring's occupancy, which the mock does not track, and commands other than writes fill it too).

  /** Model a running audio thread: a write then completes at the next runBlock(), not at its submission. */
  audioRunning = false;

  // The cart a document's rp.n8.nes node stands for, as the card jobs see it (the N8's own battery file and the ROM
  // this host booted on it). The bytes model the cart's SD card save file; the jobs' own refusals (busy, not
  // connected, no battery file, a running game) come from the same places native's do.
  /** The cart's battery file, or null when it has none (`sram.dump` then refuses). */
  cartBattery: Uint8Array | null = new Uint8Array(66_560);
  /** Whether a cart is attached; a job while not fails "not connected". */
  cartConnected = true;
  /** Hold a card job open until runCartJob(), reporting `cartProgress` first: a test's long USB transfer, whose busy
   *  state and progress row it can watch. */
  holdCartJobs = false;
  /** The progress a held job reports, in order, before its completion. */
  cartProgress: { progress: number; total: number }[] = [];
  /** The ROM image the last rom.upload booted, and the name it was stored under. */
  cartRom: { name: string; bytes: Uint8Array } | null = null;
  /** The card's game folders this host made (or a booted ROM's folder): a restore into one answers `created: false`,
   *  as native's does when the folder it lists is already there. */
  private cardFolders = new Set<string>();
  private heldCartJob: { id: number; bytes: Uint8Array; apply: () => void } | null = null;
  /** The ids of the last document's rp.n8.nes nodes. */
  private cartNodes = new Set<string>();

  /** Finish the card job holdCartJobs held open: its recorded progress, then the completion (applying what it wrote).
   *  Nothing when none is held. The promise settles with the next pump. */
  runCartJob(): void {
    const held = this.heldCartJob;
    if (!held) return;
    this.heldCartJob = null;
    held.apply();
    this.completeJob(held.id, true, held.bytes, "");
  }
  readonly jobRequests: JobRequest[] = [];
  private nextJobId = 1;
  private jobCompletions: JobCompletion[] = [];
  private queuedWrites: { id: number; system: number; offset: number; bytes: Uint8Array }[] = [];
  private readonly jobs = new CapabilityJobs({
    submitJob: (request) => this.submitJob(request),
    drainJobs: () => this.drainJobs(),
  });

  /** The block boundary the queued writes land at (audioRunning). */
  runBlock(): void {
    for (const w of this.queuedWrites.splice(0)) {
      const ram = this.ramOverrides.get(w.system);
      const ok = !!ram && this.systems.has(w.system);
      if (ok) ram.set(w.bytes, w.offset);
      this.completeJob(w.id, ok, ok ? new Uint8Array(0) : undefined, ok ? "" : `node sys-${w.system} left the plan before the job completed`);
    }
  }

  submitJob(request: JobRequest): JobSubmitResult {
    this.jobRequests.push(request);
    if (this.queuedWrites.length + this.jobCompletions.length >= 64)
      return { error: "busy: 64 jobs are outstanding; drain their completions first" };
    if (this.cartNodes.has(request.nodeId)) return this.submitCartJob(request);
    const m = /^sys-([1-9][0-9]*)$/.exec(request.nodeId);
    const system = m ? Number(m[1]) : 0;
    if (!this.systems.has(system)) {
      // A node the document places but which is not a system node presents no capability, as native refuses it; an id
      // it does not place has no node at all.
      if (this.graphNode(request.nodeId)) return { error: `node ${request.nodeId} presents no ${request.capability}` };
      return { error: `the running graph has no node ${request.nodeId}` };
    }
    // An id is taken only by a job accepted, as natively: a refusal takes none.
    const now = (bytes: Uint8Array): JobSubmitResult => {
      const id = this.nextJobId++;
      this.completeJob(id, true, bytes, "");
      return { id };
    };
    const refuse = (error: string): JobSubmitResult => ({ error });
    const ram = this.ramOverrides.get(system);
    const key = `${request.capability} ${request.op}`;
    if (key === "rp.system.memory/1 regions")
      return now(enc.encode(JSON.stringify([{ name: "ram", size: ram?.length ?? 0 }])));
    if (key === "rp.system.memory/1 read" || key === "rp.system.memory/1 write") {
      if (request.region !== "ram") return refuse(`node ${request.nodeId} has no memory region '${request.region ?? ""}'`);
      if (!ram) return refuse(`region ram of node ${request.nodeId} is not published`);
      const offset = request.offset ?? 0;
      if (key.endsWith("read")) {
        const length = request.length || ram.length - offset;
        if (length <= 0 || offset + length > ram.length) return refuse(`the read runs past region ram (${ram.length} bytes)`);
        return now(ram.slice(offset, offset + length));
      }
      const bytes = request.bytes ?? new Uint8Array(0);
      if (!bytes.length) return refuse("an empty write");
      if (offset + bytes.length > ram.length) return refuse(`the write runs past region ram (${ram.length} bytes)`);
      const id = this.nextJobId++;
      this.queuedWrites.push({ id, system, offset, bytes: new Uint8Array(bytes) });
      if (!this.audioRunning) this.runBlock();
      return { id };
    }
    if (key === "rp.system.sram/1 dump") {
      // The battery as the model holds it, not through readSram: a capability dump reads what native publishes, not the
      // system-id RPC, so it is not a battery read any test counting readSramCalls should see.
      if (this.noBattery.has(system)) return refuse(`node ${request.nodeId} publishes no battery`);
      return now(new Uint8Array(this.sramOverrides.get(system) ?? sramBytesFor(system)));
    }
    if (key === "rp.system.sram/1 restore")
      return refuse(`node ${request.nodeId} restores no battery in place: an emulated system's battery is restored by rebuilding it with the bytes as its seed`);
    const rom = this.files.get(this.canonicalize(this.systems.get(system)!.romPath)) ?? new Uint8Array(0);
    if (key === "rp.system.rom/1 header") return now(rom.slice(0, request.length || rom.length));
    if (key === "rp.system.rom/1 title") {
      // The title field of the node's console, as each core's CoreRom reads it: a Game Boy's and a GBA's, printable
      // ASCII up to the first byte that is not (a title filling its 15 bytes stops before the CGB flag at $0143).
      const type = this.systemNodes.get(system)?.type;
      const [start, length] = type === "rp.sameboy.gb" ? [0x134, 16] : type === "rp.mesen.gba" ? [0xa0, 12] : [0, 0];
      let end = start;
      while (end < start + length && end < rom.length && rom[end] >= 0x20 && rom[end] <= 0x7e) end++;
      return now(rom.slice(start, end));
    }
    return refuse(`unknown capability ${request.capability}`);
  }

  /** A card job on the cart node `request.nodeId`: the battery file's bytes, the ROM upload, the booted ROM's header
   *  and title. `name` names the ROM whose battery file the job reaches; without one the last boot counts, as
   *  natively. A restore answers native's JSON result, `{"path", "created"}`, so the caller can say where the bytes
   *  went and whether the ROM's folder had to be made for them. */
  private submitCartJob(request: JobRequest): JobSubmitResult {
    const refuse = (error: string): JobSubmitResult => ({ error });
    if (!this.cartConnected) return refuse("not connected");
    if (this.heldCartJob) return refuse("busy: the cart is running another job");
    const key = `${request.capability} ${request.op}`;
    const name = request.name || this.cartRom?.name || "";
    const battery = key === "rp.system.sram/1 dump" || key === "rp.system.sram/1 restore";
    // As native: a battery job reaches the ROM whose file it names, else the one this host last booted through an
    // upload, and with neither there is no file to reach.
    if (battery && !name)
      return refuse("a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
    if (key === "rp.system.sram/1 dump") {
      if (!this.cartBattery) return refuse(`no battery file for ${name}`);
      return this.cartJob(new Uint8Array(this.cartBattery), () => {});
    }
    if (key === "rp.system.sram/1 restore") {
      const bytes = request.bytes ?? new Uint8Array(0);
      if (!bytes.length) return refuse("a battery restore takes at least one byte");
      // The card's file for that name, and whether its folder had to be made: one exists for a ROM this host booted on
      // the card (a game that has run there makes it at its first write-back) or one a restore already made, and a
      // restore makes one for any other name, which its result says as native's does.
      const created = !this.cardFolders.has(name);
      const result = enc.encode(JSON.stringify({ path: `EDN8/gamedata/${name}/bram.srm`, created }));
      return this.cartJob(result, () => {
        this.cardFolders.add(name);
        this.cartBattery = new Uint8Array(bytes);
      });
    }
    if (key === "rp.system.rom/1 upload") {
      const bytes = request.bytes ?? new Uint8Array(0);
      const uploadName = request.name ?? "";
      if (!bytes.length) return refuse("the upload is not an iNES image");
      if (!uploadName || /[\\/]/.test(uploadName) || uploadName === "." || uploadName === "..")
        return refuse("an upload names the file it is stored under, a plain file name");
      return this.cartJob(
        enc.encode(JSON.stringify({ path: `usb-games/${uploadName}`, mapIndex: 0, streaming: "stopped" })),
        () => {
          this.cartRom = { name: uploadName, bytes: new Uint8Array(bytes) };
          this.cardFolders.add(uploadName);
        },
      );
    }
    if (key === "rp.system.rom/1 header") {
      if (!this.cartRom) return refuse("the cart is running nothing this host booted on it");
      return this.nowJob(this.cartRom.bytes.slice(0, request.length || this.cartRom.bytes.length));
    }
    if (key === "rp.system.rom/1 title") return this.nowJob(enc.encode(this.cartRom?.name ?? ""));
    return refuse(`unknown capability ${request.capability}`);
  }

  /** A cart job: completed at once (and its write applied), or held for runCartJob(). An id is taken only when the
   *  job is accepted, as natively. */
  private cartJob(bytes: Uint8Array, apply: () => void): JobSubmitResult {
    const id = this.nextJobId++;
    if (this.holdCartJobs) {
      this.heldCartJob = { id, bytes, apply };
      // The progress a transfer reports as it runs, before it is finished: what the menu row shows while it is busy.
      for (const p of this.cartProgress)
        this.jobCompletions.push({
          id, done: false, ok: true, error: "", bytes: new Uint8Array(0), progress: p.progress, total: p.total,
        });
    } else {
      apply();
      this.completeJob(id, true, bytes, "");
    }
    return { id };
  }

  private nowJob(bytes: Uint8Array): JobSubmitResult {
    const id = this.nextJobId++;
    this.completeJob(id, true, bytes, "");
    return { id };
  }

  drainJobs(): JobCompletion[] {
    return this.jobCompletions.splice(0);
  }

  private completeJob(id: number, ok: boolean, bytes: Uint8Array | undefined, error: string): void {
    this.jobCompletions.push({ id, done: true, ok, error, bytes: bytes ?? new Uint8Array(0), progress: 0, total: 0 });
  }

  capability<O extends CapabilityOp>(
    nodeId: string,
    op: O,
    args?: CapabilityArgs,
    onProgress?: (p: CapabilityProgress) => void,
  ): Promise<CapabilityResults[O]> {
    return this.jobs.run(nodeId, op, args, onProgress);
  }

  pumpJobs(): number {
    return this.jobs.pump();
  }

  rejectPendingJobs(reason: string): void {
    this.jobs.rejectAll(reason);
  }

  // The mock tier has no graph executor, so it runs no offline job: a caller learns so rather than getting buffers no
  // graph produced. Its handle has nothing to cancel.
  offlineRun(_request: OfflineRunRequest): OfflineRunHandle {
    this.log.push("offlineRun");
    const error = new Error("offline jobs need a native host: the mock backend cannot run graphs");
    return jobHandle(Promise.reject(error), () => false);
  }

  // Nor an engine to run an engine job's runtime on (a render boots a real core), so every engine job rejects.
  engineRun(_kind: EngineJobKind, _spec: object | string, _options?: EngineRunOptions): EngineRunHandle {
    this.log.push("engineRun");
    const error = new Error("engine jobs need a native host: the mock backend has no engine to run them on");
    return jobHandle(Promise.reject(error), () => false);
  }

  /** writeRam's battery twin. Writes through to the override `readSram` returns (seeding it from the
   *  system's synthetic battery on first write), with the same refusals the native side makes. */
  writeSram(id: number, offset: number, bytes: Uint8Array, expect?: Uint8Array): boolean {
    this.log.push("writeSram");
    if (this.noBattery.has(id)) return false;
    let sram = this.sramOverrides.get(id);
    if (!sram && this.systems.has(id)) {
      sram = sramBytesFor(id);
      this.sramOverrides.set(id, sram);
    }
    if (!sram || !bytes.length) return false;
    if (offset < 0 || offset + bytes.length > sram.length) return false;
    if (expect && expect.length !== bytes.length) return false;
    // A conditional write the battery has moved under is accepted by the call (as native's is - it can only
    // be judged when applied) and then not applied.
    if (expect && !expect.every((b, i) => sram![offset + i] === b)) return true;
    sram.set(bytes, offset);
    return true;
  }

  getFrame(id: number): FrameData | null {
    this.log.push("getFrame");
    // The mock never advances a real core, so a live system reports GB dimensions but no published
    // frame; an absent id is null. (Video rendering is proven against the native backend, not here.)
    if (!this.systems.has(id)) return null;
    return { width: 160, height: 144, published: false, pixels: new Uint8Array(0) };
  }

  // Live-core debug reads: the mock has no real core, so these are deterministic stand-ins that keep
  // `implements Backend` satisfied (the real behaviour is proven against the native backend).
  getApuState(_id: number): ApuState {
    this.log.push("getApuState");
    const square = (): ApuSquareState => ({
      enabled: false, period: 0, timer: 0, duty: 0, outputVolume: 0, frequency: 0, lengthCounter: 0,
      constantVolume: false, envelopeVolume: 0, envelopeLevel: 0, envelopeOutput: 0, envelopeLoop: false,
      sweepEnabled: false, sweepNegate: false, sweepPeriod: 0, sweepShift: 0,
    });
    return {
      pulse1: square(),
      pulse2: square(),
      triangle: { enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, linearCounter: 0 },
      noise: {
        enabled: false, period: 0, timer: 0, outputVolume: 0, frequency: 0, lengthCounter: 0, modeFlag: false,
        constantVolume: false, envelopeVolume: 0, envelopeLevel: 0, envelopeOutput: 0, envelopeLoop: false,
      },
      dmc: { enabled: false, sampleAddr: 0, sampleLength: 0, bytesRemaining: 0, period: 0, outputVolume: 0, loop: false, irqEnabled: false, sampleRate: 0 },
    };
  }

  getExpansionAudioState(_id: number): ExpansionAudioState {
    this.log.push("getExpansionAudioState");
    return { chip: "none", channels: [] };
  }

  getPpuState(_id: number): PpuState {
    this.log.push("getPpuState");
    return {
      scanline: 0, cycle: 0, frameCount: 0, control: 0, mask: 0, status: 0,
      scrollX: 0, videoRamAddr: 0, tmpVideoRamAddr: 0, writeToggle: false, spriteRamAddr: 0,
      paletteRam: new Uint8Array(32),
    };
  }

  readCpu(id: number, _addr: number): number | null {
    this.log.push("readCpu");
    return this.systems.has(id) ? 0 : null;
  }

  writeCpu(id: number, _addr: number, _value: number): boolean {
    this.log.push("writeCpu");
    return this.systems.has(id);
  }

  readMemory(id: number, _region: number): Uint8Array | null {
    this.log.push("readMemory");
    return this.systems.has(id) ? new Uint8Array(0) : null;
  }

  getCpuRegisters(_id: number): CpuRegister[] {
    this.log.push("getCpuRegisters");
    return [];
  }

  stepInstruction(id: number): number {
    this.log.push("stepInstruction");
    return this.systems.has(id) ? 1 : 0;
  }

  drainEvents(id: number): DebugEvent[] {
    this.log.push("drainEvents");
    // No real core: hand back one deterministic Register-write event when the id is live, otherwise none.
    return this.systems.has(id)
      ? [{ type: 0, operationType: 1, address: 0x4000, value: 0x80, programCounter: 0x8000, scanline: 0, cycle: 0, frame: 1 }]
      : [];
  }

  drainCoreBytes(_id: number): Uint8Array {
    this.log.push("drainCoreBytes");
    // No real core, so no cartridge to talk back — always an empty drain, which is also what a live
    // system whose ROM never sends returns.
    return new Uint8Array(0);
  }

  getCoreTransportStats(_id: number): CoreTransportStats {
    this.log.push("getCoreTransportStats");
    // No real core, so no transport: all zero, which is also what a live core with an unbounded
    // queue and nothing in flight reports.
    return { rxDepth: 0, rxCapacity: 0, droppedBytes: 0, wirePending: 0, txDepth: 0, linkFrames: 0, linkResent: 0 };
  }

  loadLabels(_id: number, _path: string): boolean {
    this.log.push("loadLabels");
    return false; // the mock has no NES debug target, so no symbol file is ever loaded
  }

  symbolAddress(_id: number, _name: string): number | null {
    this.log.push("symbolAddress");
    return null; // nothing is ever loaded (see loadLabels)
  }

  setCpuRegister(id: number, _name: string, _value: number): boolean {
    this.log.push("setCpuRegister");
    return this.systems.has(id);
  }

  runUntilPc(_id: number, _target: number, _maxCycles: number): boolean {
    this.log.push("runUntilPc");
    return false; // the mock has no real core to step, so a target PC is never reached
  }

  setBreakpoints(id: number, _breakpoints: Breakpoint[]): boolean {
    this.log.push("setBreakpoints");
    return this.systems.has(id); // installs nothing real; only a live NES core breaks
  }

  drainBreakHits(_id: number): BreakHitBatch {
    this.log.push("drainBreakHits");
    // No real core, so nothing can ever trip: "the invariant held" is the honest mock answer.
    return { hits: [], overflow: 0 };
  }

  runUntilBreak(_id: number, _maxCycles: number): BreakInfo {
    this.log.push("runUntilBreak");
    return { broke: false, pc: 0, breakpointId: -1 }; // no real core → nothing ever fires
  }

  setTrace(id: number, _on: boolean): boolean {
    this.log.push("setTrace");
    return this.systems.has(id); // toggles nothing real; only a live NES core traces
  }

  readTrace(_id: number, _count: number): TraceLine[] {
    this.log.push("readTrace");
    return []; // the mock never captures a trace (no real core)
  }

  stepInto(id: number): BreakInfo {
    this.log.push("stepInto");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  stepOver(id: number): BreakInfo {
    this.log.push("stepOver");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  stepOut(id: number): BreakInfo {
    this.log.push("stepOut");
    return { broke: this.systems.has(id), pc: 0, breakpointId: -1 };
  }

  beginProfile(id: number): boolean {
    this.log.push("beginProfile");
    return this.systems.has(id); // only a live NES core has a profiler
  }

  readProfile(_id: number): ProfiledFunction[] {
    this.log.push("readProfile");
    return []; // the mock never profiles (no real core)
  }

  disassemble(_id: number, _addr: number, _count: number): DisasmLine[] {
    this.log.push("disassemble");
    return []; // the mock has no disassembler (no real core)
  }

  getCallStack(_id: number): CallFrame[] {
    this.log.push("getCallStack");
    return []; // the mock has no call stack (no real core)
  }

  zip(entries: ZipEntry[]): Uint8Array | null {
    this.log.push("zip");
    this.zipCalls.push(entries.map((e) => ({ name: e.name, bytes: new Uint8Array(e.bytes) })));
    const parts: number[] = [...PK_MAGIC];
    for (const e of entries) {
      const name = enc.encode(e.name);
      pushU32(parts, name.length);
      for (const b of name) parts.push(b);
      pushU32(parts, e.bytes.length);
      for (const b of e.bytes) parts.push(b);
    }
    return new Uint8Array(parts);
  }

  unzip(bytes: Uint8Array): ZipEntry[] | null {
    this.log.push("unzip");
    this.unzipCalls.push(new Uint8Array(bytes));
    if (bytes.length < 4 || !PK_MAGIC.every((b, i) => bytes[i] === b)) return null;
    const out: ZipEntry[] = [];
    let off = 4;
    while (off + 4 <= bytes.length) {
      const nameLen = readU32(bytes, off);
      off += 4;
      const name = dec.decode(bytes.slice(off, off + nameLen));
      off += nameLen;
      const byteLen = readU32(bytes, off);
      off += 4;
      out.push({ name, bytes: bytes.slice(off, off + byteLen) });
      off += byteLen;
    }
    return out;
  }

  savFromJson(json: string): Uint8Array {
    this.log.push("savFromJson");
    // The LSDj codec is now pure TS, so the mock runs the REAL encoder — a valid
    // 128 KiB `jk`/`rb`-stamped image, no native host needed.
    return savFromJson(json);
  }

  // A trivial lossless RGBA container (magic "RPNG" + w + h + raw rgba) — NOT real PNG. Like the mock
  // zip/unzip, it just needs round-trip fidelity so pure tests can exercise the font tile↔rgba mapping;
  // decoding a real .png file is covered by the test-native suite (real lodepng host).
  pngEncode(width: number, height: number, rgba: Uint8Array): Uint8Array | null {
    this.log.push("pngEncode");
    if (rgba.length < width * height * 4) return null;
    const parts: number[] = [0x52, 0x50, 0x4e, 0x47]; // "RPNG"
    pushU32(parts, width);
    pushU32(parts, height);
    return new Uint8Array([...parts, ...rgba.subarray(0, width * height * 4)]);
  }

  pngDecode(bytes: Uint8Array): PngImageData | null {
    this.log.push("pngDecode");
    if (bytes.length < 12 || bytes[0] !== 0x52 || bytes[1] !== 0x50 || bytes[2] !== 0x4e || bytes[3] !== 0x47) return null;
    const width = readU32(bytes, 4);
    const height = readU32(bytes, 8);
    const rgba = bytes.slice(12, 12 + width * height * 4);
    if (rgba.length < width * height * 4) return null;
    return { width, height, rgba };
  }

  openFileBrowser(opts: FileBrowserOpts): Promise<string | null> {
    this.log.push("openFileBrowser");
    this.fileBrowserCalls.push(opts);
    return Promise.resolve(this.browseQueue.length ? (this.browseQueue.shift() as string | null) : null);
  }

  // Recorded; accepted for a node the last document sent places, as native accepts one the last published plan places
  // (the mock knows no node's params, so any param id passes).
  setParam(nodeId: string, paramId: string, value: number): boolean {
    this.log.push("setParam");
    this.paramCalls.push({ nodeId, paramId, value });
    return !Number.isNaN(value) && this.graphNode(nodeId) !== undefined;
  }

  pressButton(id: number, button: number, down: boolean): boolean {
    this.log.push("pressButton");
    this.pressButtonCalls.push({ id, button, down });
    return true;
  }

  stageNodeMidi(nodeId: string, bytes: number[]): boolean {
    this.log.push("stageNodeMidi");
    if (bytes.length === 0) return false;
    // As natively: a node the last document (the running plan, in the mock) does not place takes nothing, silently.
    if (!this.graphNode(nodeId)) return true;
    this.stageNodeMidiCalls.push({ nodeId, bytes: [...bytes] });
    return true;
  }

  // --- test helpers (not part of Backend) ---------------------------------

  /** The last graph document setGraph was sent, as it was spelt (canonical: keys sorted), or undefined when none was
   *  sent. */
  lastDocument(): GraphDocument | undefined {
    const request = this.graphRequests[this.graphRequests.length - 1];
    return request ? (JSON.parse(request.document) as GraphDocument) : undefined;
  }

  /** Node `id` of the last graph document sent (the routing, the controller, a system's protocol node
   *  `sys-<id>/<role-kind>`, ...), or undefined when that document has none or none was sent. */
  graphNode(id: string): NodeDoc | undefined {
    return this.lastDocument()?.nodes.find((n) => n.id === id);
  }

  /** The ids of the protocol nodes the last document places for system `system`, in document order. */
  protocolNodes(system: number): string[] {
    const prefix = `sys-${system}/`;
    return (this.lastDocument()?.nodes ?? []).filter((n) => n.id.startsWith(prefix)).map((n) => n.id);
  }

  /** The ids the mock currently considers live, sorted. */
  liveSystemIds(): number[] {
    return [...this.systems.keys()].sort((a, b) => a - b);
  }

  /** The live ids that were reconstructed from in-memory zip blobs (import), sorted. */
  restoredIds(): number[] {
    return [...this.systems.entries()].filter(([, s]) => s.restoredFromBytes).map(([id]) => id).sort((a, b) => a - b);
  }
}

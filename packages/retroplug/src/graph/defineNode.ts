// TypeScript nodes: the same contract as a C++ node (docs/modular-graph-design.md section 4.8). A
// definition yields the same descriptor JSON its C++ twin would, so the planner cannot tell them apart.
//
//   export const gain = defineNode({
//     id: "rp.test.gain",
//     ports: { in: audio.in(), out: audio.out(channels.match("in")) },
//     params: { gain: param.float({ name: "Gain", min: -60, max: 12, default: 0, unit: "dB" }) },
//     create: () => ({ process(block) { ... } }),
//   });
//
// Settings are validated on the CONTROL plane (parseSettings), so the code that runs on the audio thread
// receives a plain, already-checked object. A definition's settings are any SettingsSchema, which brings its own
// safeParse and toJSONSchema: an `s` schema (settingsSchema.ts), which holds no zod, or a zod one, which has the same
// two methods. defineNode imports only their types, so a bundle holds zod only when a node it carries does.
import { plainId, wellFormed } from "./descriptor";
import type { ChannelSpec, Layout, ParamDesc, PortDesc, QueueCapacity, TypeDescriptor } from "./descriptor";
import type { SettingsIssue, SettingsSchema } from "./settingsSchema";

// ---- ports ------------------------------------------------------------------------------------------------

type AnyChannels = Extract<ChannelSpec, { any: true }>;
type MatchChannels = Extract<ChannelSpec, { match: string }>;
type FixedChannels = Extract<ChannelSpec, { count: number }>;
type OutputChannels = MatchChannels | FixedChannels;

// Each kind's legal shape, so a hand-written spec that C++'s parser would refuse does not type-check. (The
// same rules are also checked at run time, for callers that are not type-checked.)
export type PortSpec =
  | { dir: "in"; kind: "audio"; channels: ChannelSpec }
  | { dir: "out"; kind: "audio"; channels: OutputChannels }
  | { dir: "in"; kind: "events" | "bytes" }
  | { dir: "out"; kind: "events" | "bytes"; capacity?: QueueCapacity }
  | { dir: "in" | "out"; kind: "control" }
  | { dir: "in"; kind: "resource"; resourceType: string; access?: "write" }
  | { dir: "out"; kind: "resource"; resourceType: string }
  | { dir: "link"; kind: "link" };

export const channels = {
  any: (): AnyChannels => ({ any: true }),
  match: (port: string): MatchChannels => ({ match: port }),
  fixed: (count: number, layout: string): FixedChannels => ({ count, layout }),
  mono: (): FixedChannels => ({ count: 1, layout: "mono" }),
  stereo: (): FixedChannels => ({ count: 2, layout: "stereo" }),
};

export const audio = {
  in: (spec: ChannelSpec = channels.any()): PortSpec => ({ dir: "in", kind: "audio", channels: spec }),
  out: (spec: OutputChannels): PortSpec => ({ dir: "out", kind: "audio", channels: spec }),
};
// An events or bytes output, with the queue capacity it declares if any (QueueCapacity). An input takes none: it
// reads its source's queue, so a capacity there would bound nothing.
const queueOut = (kind: "events" | "bytes", capacity?: QueueCapacity): PortSpec =>
  capacity === undefined ? { dir: "out", kind } : { dir: "out", kind, capacity };

export const events = {
  in: (): PortSpec => ({ dir: "in", kind: "events" }),
  out: (capacity?: QueueCapacity): PortSpec => queueOut("events", capacity),
};
export const bytes = {
  in: (): PortSpec => ({ dir: "in", kind: "bytes" }),
  out: (capacity?: QueueCapacity): PortSpec => queueOut("bytes", capacity),
};
export const control = {
  in: (): PortSpec => ({ dir: "in", kind: "control" }),
  out: (): PortSpec => ({ dir: "out", kind: "control" }),
};
export const resource = {
  in: (type: string): PortSpec => ({ dir: "in", kind: "resource", resourceType: type }),
  /** A resource input the node writes: one writer per mutable resource, readers wired from its output. */
  write: (type: string): PortSpec => ({ dir: "in", kind: "resource", resourceType: type, access: "write" }),
  out: (type: string): PortSpec => ({ dir: "out", kind: "resource", resourceType: type }),
};
export const link = (): PortSpec => ({ dir: "link", kind: "link" });

// ---- params -----------------------------------------------------------------------------------------------

type ParamSpec = DistributiveOmit<ParamDesc, "id">;
type DistributiveOmit<T, K extends keyof T> = T extends unknown ? Omit<T, K> : never;

// An empty unit is omitted, as the C++ side omits it.
const numeric = (type: "float" | "int", o: { name: string; min: number; max: number; default: number; unit?: string }): ParamSpec => {
  const { unit, ...rest } = o;
  return unit ? { type, ...rest, unit } : { type, ...rest };
};

export const param = {
  float: (o: { name: string; min: number; max: number; default: number; unit?: string }): ParamSpec => numeric("float", o),
  int: (o: { name: string; min: number; max: number; default: number; unit?: string }): ParamSpec => numeric("int", o),
  bool: (o: { name: string; default?: boolean }): ParamSpec => ({ type: "bool", name: o.name, default: o.default ?? false }),
  enum: (o: { name: string; values: readonly string[]; default: string }): ParamSpec => ({
    type: "enum",
    name: o.name,
    values: [...o.values],
    default: o.default,
  }),
};

// ---- the block a node processes (filled by the script-node runtime) ---------------------------------------

export interface Transport {
  sampleRate: number;
  tempo: number;
  /** At block start. */
  ppq: number;
  playing: boolean;
}

export interface Message {
  frame: number;
  /** A view into a buffer the next block overwrites: copy the bytes to keep them past this process call. */
  data: Uint8Array;
  /** On a bytes port, bit 0 = flush barrier. */
  flags: number;
}

export interface ControlStream {
  /** The value at block start. */
  start: number;
  /** This block's changes, frame-ordered. */
  changes: readonly { frame: number; value: number }[];
}

/** The value a control stream holds from the block's first frame: its start, or the last change at frame 0. A node
 *  cannot set an output's start (it is the last block's end), so a value that takes effect from frame 0 arrives as a
 *  change at frame 0. */
export function startValue(stream: ControlStream): number {
  let value = stream.start;
  const changes = stream.changes;
  for (let i = 0; i < changes.length && changes[i].frame === 0; i++) value = changes[i].value;
  return value;
}

// ---- the host contract ------------------------------------------------------------------------------------
// What the script host (packages/native/src/host/script/ScriptHost.cpp) does around a node that the types below
// cannot say (docs/modular-graph-phase-b3a-design.md section 2):
//
// - One realm for every node. Every instance of every script type lives in one QuickJS context, so a global one
//   node's code sets, or an accessor it hangs on a shared prototype, is there for every node. The host keeps its
//   own writes off them: each field it fills is the object's own, from when it made the object. The Block prototype is
//   frozen with its methods, so assigning over one (`block.send = f`) throws in strict code, where
//   Object.defineProperty on the node's own Block still works. The one gap: a field deleted from an object the
//   block hands out is set again through the prototype chain.
// - process and the hooks are read once, from what create returned (and the prototypes it inherits them through,
//   short of Object.prototype): assigning another one later does nothing.
// - portsChanged runs once on every fresh instance, when the plan that made it is adopted, before its first
//   process; then again whenever a later plan reshapes it or changes which of its outputs are wired.
// - send and emit: a frame earlier than the port's last one this block is raised to it, and none is range-checked.
//   A frame past the block goes on as given, and a negative one wraps through ToUint32 (-1 is 4294967295), raising
//   every later one on that port this block to it. A send that does not fit returns false and is dropped, and
//   counted; an emit onto a full stream overwrites its last change and returns false. A port the layout does not
//   have, or not of the method's kind, throws a TypeError.
// - A feedback input carries last block's messages at their frames in that block, so a frame can lie at or past
//   this block's end. The master-sync decoder B3b ports reads one: a Game Boy's serialOut, each byte at its frame.
// - Promise reactions and queueMicrotask callbacks run at the end of the block, after every node's process: at
//   most 256 a block (the rest at the next block's end), under one call deadline between them. Every Block method
//   throws inside one, as in a hook: a block exists only during its node's process call. A reaction's own throw
//   only rejects its promise and is not counted; a queueMicrotask callback's is counted as a throw, charged to no
//   node, so it never faults one.
//
// What a block hands out (the arrays, the views, the message objects) is valid only during the process call it is
// handed to: the same objects are refilled for the next block, or made again, so read them through the block every
// block and copy what you keep.
export interface Block {
  frames: number;
  transport: Transport;
  /** Audio ports: one Float32Array per channel, `frames` long. They view the node's own copy of each channel,
   *  never the buffers other nodes read: an input's samples are copied in before process and an output's copied
   *  out after it, so writing an input changes only this node's copy. Valid only during this call: a later block
   *  may hand out new views (when its length moves, say), and only the ones it hands out are read. */
  audio(port: string): Float32Array[];
  /** Events / bytes inputs. Each message's `data` is overwritten next block. */
  messages(port: string): readonly Message[];
  /** Events / bytes outputs. False when the fixed-capacity queue is full (the message is dropped). */
  send(port: string, frame: number, data: ArrayLike<number>, flags?: number): boolean;
  /** A control input, or a param by id. */
  control(id: string): ControlStream;
  /** Control outputs. */
  emit(port: string, frame: number, value: number): boolean;
}

// Every hook runs on the thread that runs blocks, never on the control thread: the script host holds every
// instance in one runtime that only that thread enters (docs/modular-graph-phase-b3a-design.md section 2.1). Each
// call is held to a deadline of the thread's own CPU time (2 s unless the host sets another; section 2.7): one that
// runs past it is stopped, which no try can catch, and counted as a throw. Under faultOnThrow the node then faults;
// without, it runs again next block and faults if that call is stopped too. A fault outlives reshapes.
export interface NodeInstance<S = unknown> {
  /** Return "fault" to be bypassed from now on (the executor silences this node and counts it). A throw is
   *  counted and the next block runs again, unless the definition says faultOnThrow. */
  process(block: Block): void | "fault";
  /** A new sample rate or block size (the host re-activates without resetting): resize anything sized from
   *  CreateContext's maxFrames. Return false if the node cannot run at these. Called only on a live instance, while
   *  no audio thread runs blocks (a fresh one reads both from CreateContext). A throw is no refusal: it is counted,
   *  and under faultOnThrow faults the node at its next block. */
  activate?(sampleRate: number, maxFrames: number): boolean | void;
  /** Called when the connected-output set changes at a plan swap; connected[i] follows output order. A throw is
   *  counted, and under faultOnThrow faults the node at its next block. */
  portsChanged?(connected: readonly boolean[]): void;
  /** New settings, already checked, at the plan swap that kept this instance through a settings change (a swap,
   *  or a reshape when the definition reshapes), before the first block that runs with them. In their JSON form,
   *  as CreateContext's settings are. A throw is counted, and under faultOnThrow faults the node at its next block. */
  onSettings?(settings: S): void;
}

export interface CreateContext<S> {
  /** What parseSettings returned, in its JSON form: the settings are checked on the control side and cross to
   *  the runtime that runs the node as JSON (the two share no value), so a transform whose result JSON cannot
   *  hold (a Map, a Set, a Date, undefined) arrives as JSON spells it. The node's side never runs the schema,
   *  so it does no checking at a plan swap. */
  settings: S;
  sampleRate: number;
  maxFrames: number;
}

// ---- the definition ---------------------------------------------------------------------------------------

type PortMap = Record<string, PortSpec>;
type ParamMap = Record<string, ParamSpec>;

export interface NodeDefinition<S> {
  id: string;
  version?: string;
  name?: string;
  category?: string;
  requires?: { heap?: boolean; threads?: boolean };
  settings?: SettingsSchema<S>;
  ports: PortMap | ((settings: S) => PortMap);
  params?: ParamMap | ((settings: S) => ParamMap);
  exclusive?: string[][];
  /** Keep the instance through a settings change that alters its layout (ports or params that follow its
   *  settings): the planner rebinds its ports by id, and onSettings receives the new settings at the swap.
   *  Without it, such a change creates a new instance. */
  reshapes?: true;
  /** A throw in process faults the node (bypassed from then on, as returning "fault" does) rather than letting it
   *  run again next block, and is counted all the same; so does one in onSettings, portsChanged or activate, at its
   *  next block. */
  faultOnThrow?: true;
  /** Runs on the thread that runs blocks, when the plan holding the node is adopted, not when the document is
   *  planned: a document naming the node plans whatever create will do, and a create that throws (or returns no
   *  process) surfaces as the node faulting at its first block, the throw counted and kept. */
  create(ctx: CreateContext<S>): NodeInstance<S>;
}

export interface DefinedNode<S> extends NodeDefinition<S> {
  /** Settings checked on the control plane: defaults filled, and a refusal thrown as the schema's first issue on one
   *  line (`field: why`), not every issue as zod's own message spells them. */
  parseSettings(raw: unknown): S;
  /** The resolved layout for these (already-parsed) settings. */
  layout(settings: S): Layout;
  /** The type descriptor: the layout for default settings, plus metadata and the settings schema. */
  descriptor(): TypeDescriptor;
}

const MAX_CHANNELS = 4294967295;
// The most a queue port may declare: kMaxQueueMessages / kMaxQueueBytes in Descriptor.hpp.
const MAX_QUEUE_MESSAGES = 1048576;
const MAX_QUEUE_BYTES = 16777216;
const q = (s: string): string => JSON.stringify(s);

/** How many channels a layout name means (mono 1, stereo 2, 5.1 6, 7.1 8, discrete(n) n); null for "any"
 *  or an unknown name. The twin of layoutChannels in packages/native/src/graph/Descriptor.cpp. */
export function layoutChannels(layout: string): number | null {
  switch (layout) {
    case "mono": return 1;
    case "stereo": return 2;
    case "5.1": return 6;
    case "7.1": return 8;
  }
  const m = /^discrete\(([1-9][0-9]*)\)$/.exec(layout);
  const n = m ? Number(m[1]) : 0;
  return n >= 1 && n <= MAX_CHANNELS ? n : null;
}

const isString = (v: unknown): v is string => typeof v === "string";
const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

// One port's own shape, as parsePort in Descriptor.cpp checks it.
function checkPort(node: string, p: PortDesc): void {
  const where = `${node}: port ${q(p.id)}`;
  if (!["in", "out", "link"].includes(p.dir)) throw new Error(`${where}: unknown dir ${q(String(p.dir))}`);
  if (!["audio", "events", "bytes", "control", "link", "resource"].includes(p.kind))
    throw new Error(`${where}: unknown kind ${q(String(p.kind))}`);
  if ((p.kind === "link") !== (p.dir === "link")) throw new Error(`${where}: link ports, and only link ports, have dir "link"`);
  if (p.kind === "audio") {
    const c = p.channels as Record<string, unknown> | undefined;
    if (!c || typeof c !== "object") throw new Error(`${where}: an audio port needs channels`);
    if (["any", "match", "count"].filter((k) => has(c, k) && c[k] !== undefined).length !== 1)
      throw new Error(`${where}: channels needs exactly one of any / match / count`);
    if (has(c, "any")) {
      if (c.any !== true) throw new Error(`${where}: channels.any must be true`);
      if (p.dir === "out") throw new Error(`${where}: an output cannot be "any"; use match or a fixed count`);
    } else if (has(c, "match")) {
      if (!plainId(c.match)) throw new Error(`${where}: channels.match must name a port`);
    } else {
      const count = c.count as number;
      if (typeof count !== "number" || !Number.isInteger(count) || count < 1 || count > MAX_CHANNELS)
        throw new Error(`${where}: needs a whole channel count from 1 to ${MAX_CHANNELS}`);
      if (!isString(c.layout) || !c.layout || !wellFormed(c.layout)) throw new Error(`${where}: needs a layout name`);
      const known = layoutChannels(c.layout);
      if (known === null && c.layout.startsWith("discrete("))
        throw new Error(`${where}: layout ${q(c.layout)} is not a discrete(n) layout`);
      if (known !== null && known !== count) throw new Error(`${where}: layout ${q(c.layout)} is ${known} channels, not ${count}`);
    }
  } else if (p.channels !== undefined) {
    throw new Error(`${where}: only audio ports have channels`);
  }
  if (p.kind === "resource") {
    if (!plainId(p.resourceType))
      throw new Error(`${where}: a resource port needs a resourceType with no whitespace, control characters or lone surrogates`);
    if (p.access !== undefined && p.access !== "write") throw new Error(`${where}: access can only be "write"`);
    if (p.access === "write" && p.dir !== "in") throw new Error(`${where}: only a resource input may have access "write"`);
  } else if (p.resourceType !== undefined || p.access !== undefined) {
    throw new Error(`${where}: only resource ports have resourceType or access`);
  }
  if (p.capacity !== undefined) {
    if (p.kind !== "events" && p.kind !== "bytes") throw new Error(`${where}: only events and bytes ports have capacity`);
    // Worded as Descriptor.cpp words it after the port.
    if (p.dir !== "out") throw new Error(`${where}: only an output declares a capacity: a wire carries its source's queue`);
    checkCapacity(where, p.capacity);
  }
}

// A queue capacity, as parseCapacity in Descriptor.cpp checks it: messages, bytes or both, nothing else, each a
// whole number from 1 to its cap. An undefined part is no part, as canonicalJson spells it.
function checkCapacity(where: string, c: unknown): void {
  if (typeof c !== "object" || c === null || Array.isArray(c)) throw new Error(`${where}: capacity must be an object`);
  const parts = c as Record<string, unknown>;
  const given = Object.keys(parts).filter((k) => parts[k] !== undefined);
  const stray = given.find((k) => k !== "messages" && k !== "bytes");
  if (stray !== undefined) throw new Error(`${where}: capacity: unknown field ${q(stray)}`);
  if (given.length === 0) throw new Error(`${where}: capacity needs messages, bytes or both`);
  for (const [key, max] of [["messages", MAX_QUEUE_MESSAGES], ["bytes", MAX_QUEUE_BYTES]] as const) {
    const v = parts[key];
    if (v !== undefined && (typeof v !== "number" || !Number.isInteger(v) || v < 1 || v > max))
      throw new Error(`${where}: capacity.${key} must be a whole number from 1 to ${max}`);
  }
}

// One param's own shape, as parseParam in Descriptor.cpp checks it.
function checkParam(node: string, p: ParamDesc): void {
  const where = `${node}: param ${q(p.id)}`;
  if (!isString(p.name) || !wellFormed(p.name)) throw new Error(`${where}: needs a name`);
  if (p.type === "enum") {
    if (!Array.isArray(p.values) || p.values.length === 0 || p.values.some((v) => !isString(v) || !v || !wellFormed(v)))
      throw new Error(`${where}: an enum needs non-empty values`);
    if (new Set(p.values).size !== p.values.length) throw new Error(`${where}: an enum lists a value twice`);
    if (!p.values.includes(p.default)) throw new Error(`${where}: default ${q(String(p.default))} is not one of its values`);
  } else if (p.type === "bool") {
    if (typeof p.default !== "boolean") throw new Error(`${where}: a bool's default must be true or false`);
  } else if (p.type === "float" || p.type === "int") {
    if (![p.min, p.max, p.default].every((v) => typeof v === "number" && Number.isFinite(v)))
      throw new Error(`${where}: needs finite min, max and default`);
    if (!(p.min <= p.max)) throw new Error(`${where}: min must not exceed max`);
    if (!(p.min <= p.default && p.default <= p.max)) throw new Error(`${where}: default must lie within [min, max]`);
    if (p.type === "int" && ![p.min, p.max, p.default].every(Number.isInteger))
      throw new Error(`${where}: an int param needs whole-number min, max and default`);
  } else {
    throw new Error(`${where}: unknown type ${q(String((p as { type: unknown }).type))}`);
  }
  const unit = (p as { unit?: unknown }).unit;
  if (unit !== undefined && (!isString(unit) || !wellFormed(unit))) throw new Error(`${where}: unit must be a string`);
}

// A fresh copy of a port, so a caller that edits a returned layout cannot reach the definition.
function copyPort(id: string, spec: PortSpec): PortDesc {
  const p: PortDesc = { id, dir: spec.dir, kind: spec.kind };
  if ("channels" in spec && spec.channels !== undefined) p.channels = { ...spec.channels } as ChannelSpec;
  if ("resourceType" in spec && spec.resourceType !== undefined) p.resourceType = spec.resourceType;
  if ("access" in spec && spec.access !== undefined) p.access = spec.access;
  if ("capacity" in spec && spec.capacity !== undefined) p.capacity = copyCapacity(spec.capacity);
  return p;
}

// Only the parts given: an undefined one is no part, which is how canonicalJson (and so C++) reads it.
function copyCapacity(c: QueueCapacity): QueueCapacity {
  if (typeof c !== "object" || c === null || Array.isArray(c)) return c;  // checkPort refuses it
  const copy: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(c)) if (value !== undefined) copy[key] = value;
  return copy as QueueCapacity;
}

// Field by field, as C++ writes a param back out: only the fields its type has (a stray one would be
// dropped by C++'s re-spelling, so the two would disagree), a missing name read as the id, and a unit on
// any type when it is non-empty.
function copyParam(id: string, spec: ParamSpec): ParamDesc {
  const s = spec as Record<string, unknown>;
  const name = s.name === undefined || s.name === "" ? id : s.name;
  let p: Record<string, unknown>;
  if (s.type === "float" || s.type === "int") p = { id, name, type: s.type, min: s.min, max: s.max, default: s.default };
  else if (s.type === "bool") p = { id, name, type: s.type, default: s.default };
  else if (s.type === "enum") p = { id, name, type: s.type, values: Array.isArray(s.values) ? [...s.values] : s.values, default: s.default };
  else p = { id, name, type: s.type };
  if (s.unit !== undefined && s.unit !== "") p.unit = s.unit;
  return p as unknown as ParamDesc;
}

// The metadata a descriptor carries, as C++'s parser types it.
function checkMetadata<S>(def: NodeDefinition<S>): void {
  for (const key of ["version", "name", "category"] as const) {
    const v = def[key];
    if (v !== undefined && (!isString(v) || !wellFormed(v))) throw new Error(`node type ${q(def.id)}: ${key} must be a string`);
  }
  for (const key of ["heap", "threads"] as const) {
    const v = def.requires?.[key];
    if (v !== undefined && typeof v !== "boolean") throw new Error(`node type ${q(def.id)}: requires.${key} must be true or false`);
  }
  // Read by the script host as given, so a false would be a promise in a form it does not read.
  for (const key of ["reshapes", "faultOnThrow"] as const)
    if (def[key] !== undefined && def[key] !== true) throw new Error(`node type ${q(def.id)}: ${key} is true or absent`);
}

// Every string (and key) in a JSON value is one C++ can read.
function allWellFormed(v: unknown): boolean {
  if (isString(v)) return wellFormed(v);
  if (Array.isArray(v)) return v.every(allWellFormed);
  if (v && typeof v === "object") return Object.entries(v).every(([k, x]) => wellFormed(k) && allWellFormed(x));
  return true;
}

// The INPUT schema: what a document may write (defaulted fields optional, extra keys allowed), which is
// what C++'s DefaultIfMissing schema says too. zod's default, the output schema, would mark every
// defaulted field required; an `s` schema has the input form alone.
function settingsSchema<S>(def: NodeDefinition<S>): Record<string, unknown> {
  const schema = def.settings!.toJSONSchema({ io: "input" }) as Record<string, unknown>;
  if (!allWellFormed(schema)) throw new Error(`node type ${q(def.id)}: the settings schema holds a lone surrogate, which C++ cannot read`);
  return schema;
}

// An integer-like key ("1") would be listed before every other key by Object.entries, so it could not
// keep its declaration order; C++ keeps it, so the two sides would disagree.
const integerLike = (id: string): boolean => /^(0|[1-9][0-9]*)$/.test(id);

function buildLayout<S>(def: NodeDefinition<S>, settings: S): Layout {
  const portMap = typeof def.ports === "function" ? def.ports(settings) : def.ports;
  const paramMap = typeof def.params === "function" ? def.params(settings) : (def.params ?? {});
  const ports: PortDesc[] = Object.entries(portMap).map(([id, spec]) => copyPort(id, spec));
  // Inputs (and link ports) first, then outputs, each in declaration order - the C++ layer's order too.
  const ordered = [...ports.filter((p) => p.dir !== "out"), ...ports.filter((p) => p.dir === "out")];
  const params = Object.entries(paramMap).map(([id, spec]) => copyParam(id, spec));
  const seen = new Set<string>();
  for (const p of [...ordered, ...params]) {
    if (!plainId(p.id) || p.id.includes("*"))
      throw new Error(`${def.id}: id ${q(p.id)} must be non-empty, with no whitespace, control characters, lone surrogates or '*'`);
    if (integerLike(p.id)) throw new Error(`${def.id}: id ${q(p.id)} is a number, which a JS object cannot keep in order`);
    if (seen.has(p.id)) throw new Error(`${def.id}: ${q(p.id)} is used by more than one port or param`);
    seen.add(p.id);
  }
  // The same checks the C++ parser applies to every layout (Descriptor.cpp), so a TS node cannot declare
  // what the planner would refuse.
  for (const p of ordered) checkPort(def.id, p);
  for (const p of params) checkParam(def.id, p);
  for (const p of ordered) {
    const c = p.channels;
    if (p.kind !== "audio" || !c || !("match" in c) || c.match === undefined) continue;
    const target = ordered.find((t) => t.id === c.match);
    if (!target || target === p || target.kind !== "audio")
      throw new Error(`${def.id}: port ${q(p.id)} matches ${q(c.match)}, which is not another audio port`);
  }
  const isWildcard = (m: string) => m.length > 2 && m.endsWith(".*");
  const covers = (pattern: string, id: string) => isWildcard(pattern) && id.startsWith(pattern.slice(0, -1));
  for (const group of def.exclusive ?? []) {
    if (!Array.isArray(group) || group.length === 0) throw new Error(`${def.id}: an exclusive group needs members`);
    group.forEach((m, i) => {
      if (!isString(m) || !m || !wellFormed(m) || (!isWildcard(m) && !ordered.some((p) => p.id === m && p.dir === "out")))
        throw new Error(`${def.id}: exclusive member ${q(String(m))} must be an output's id or a "prefix.*" pattern`);
      for (const other of group.slice(0, i)) {
        const otherId = isWildcard(other) ? other.slice(0, -1) : other;
        const mineId = isWildcard(m) ? m.slice(0, -1) : m;
        if (other === m || covers(m, otherId) || covers(other, mineId))
          throw new Error(`${def.id}: exclusive members ${q(other)} and ${q(m)} overlap`);
      }
    });
  }
  const layout: Layout = { ports: ordered, params };
  if (def.exclusive?.length) layout.exclusive = def.exclusive.map((g) => [...g]);
  return layout;
}

export function defineNode<S = Record<string, never>>(def: NodeDefinition<S>): DefinedNode<S> {
  if (!plainId(def.id))
    throw new Error(`node type id ${q(String(def.id))} must be non-empty, with no whitespace, control characters or lone surrogates`);
  checkMetadata(def);
  // The first issue, on one line, as `field: why`: what the planner reports (settings-invalid) and a node's
  // rp_error can carry, where zod's own message is every issue as indented JSON.
  const firstIssue = (error: { issues: readonly SettingsIssue[] }): string => {
    const issue = error.issues[0];
    return `${issue?.path.length ? `${issue.path.join(".")}: ` : ""}${issue?.message ?? "invalid"}`;
  };
  // Settings arrive as JSON: an object, or nothing at all (the defaults), never another value, as in C++.
  const parseSettings = (raw: unknown): S => {
    const given = raw === undefined ? {} : raw;
    if (typeof given !== "object" || given === null || Array.isArray(given))
      throw new Error(`node type ${q(def.id)}: settings must be a JSON object`);
    if (!def.settings) return {} as S;
    const parsed = def.settings.safeParse(given);
    if (!parsed.success) throw new Error(`node type ${q(def.id)}: settings: ${firstIssue(parsed.error)}`);
    return parsed.data;
  };
  // The descriptor describes the layout for default settings, so every field needs one (C++ has S{}).
  if (def.settings) {
    const defaults = def.settings.safeParse({});
    if (!defaults.success) throw new Error(`node type ${q(def.id)}: every settings field needs a default (${firstIssue(defaults.error)})`);
  }
  return {
    ...def,
    parseSettings,
    layout: (settings: S) => buildLayout(def, settings),
    descriptor: (): TypeDescriptor => ({
      id: def.id,
      version: def.version ?? "1.0.0",
      name: def.name ?? def.id,
      category: def.category ?? "",
      requires: { heap: def.requires?.heap ?? false, threads: def.requires?.threads ?? false },
      ...buildLayout(def, parseSettings({})),
      settings: def.settings ? settingsSchema(def) : { type: "object" },
    }),
  };
}

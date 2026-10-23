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
// receives a plain, already-checked object and the audio-thread bundle never carries zod.
import { z } from "zod";
import type { ChannelSpec, Layout, ParamDesc, PortDesc, TypeDescriptor } from "./descriptor";

// ---- ports ------------------------------------------------------------------------------------------------

type PortSpec = Omit<PortDesc, "id">;

export const channels = {
  any: (): ChannelSpec => ({ any: true }),
  match: (port: string): ChannelSpec => ({ match: port }),
  fixed: (count: number, layout: string): ChannelSpec => ({ count, layout }),
  mono: (): ChannelSpec => ({ count: 1, layout: "mono" }),
  stereo: (): ChannelSpec => ({ count: 2, layout: "stereo" }),
};

export const audio = {
  in: (spec: ChannelSpec = channels.any()): PortSpec => ({ dir: "in", kind: "audio", channels: spec }),
  out: (spec: ChannelSpec): PortSpec => ({ dir: "out", kind: "audio", channels: spec }),
};
export const events = {
  in: (): PortSpec => ({ dir: "in", kind: "events" }),
  out: (): PortSpec => ({ dir: "out", kind: "events" }),
};
export const bytes = {
  in: (): PortSpec => ({ dir: "in", kind: "bytes" }),
  out: (): PortSpec => ({ dir: "out", kind: "bytes" }),
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

export interface Block {
  frames: number;
  transport: Transport;
  /** Audio ports: one Float32Array per channel. */
  audio(port: string): Float32Array[];
  /** Events / bytes inputs. */
  messages(port: string): readonly Message[];
  /** Events / bytes outputs. False when the fixed-capacity queue is full (the message is dropped). */
  send(port: string, frame: number, data: ArrayLike<number>, flags?: number): boolean;
  /** A control input, or a param by id. */
  control(id: string): ControlStream;
  /** Control outputs. */
  emit(port: string, frame: number, value: number): boolean;
}

export interface NodeInstance {
  /** Return "fault" to be bypassed from now on (the executor silences this node and counts it). */
  process(block: Block): void | "fault";
  /** Called when the connected-output set changes at a plan swap; connected[i] follows output order. */
  portsChanged?(connected: readonly boolean[]): void;
}

export interface CreateContext<S> {
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
  settings?: z.ZodType<S>;
  ports: PortMap | ((settings: S) => PortMap);
  params?: ParamMap | ((settings: S) => ParamMap);
  exclusive?: string[][];
  create(ctx: CreateContext<S>): NodeInstance;
}

export interface DefinedNode<S> extends NodeDefinition<S> {
  /** Settings checked on the control plane: defaults filled, errors thrown with zod's message. */
  parseSettings(raw: unknown): S;
  /** The resolved layout for these (already-parsed) settings. */
  layout(settings: S): Layout;
  /** The type descriptor: the layout for default settings, plus metadata and the settings schema. */
  descriptor(): TypeDescriptor;
}

const NO_SETTINGS_SCHEMA = { type: "object" };

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
  return m ? Number(m[1]) : null;
}

const MAX_CHANNELS = 4294967295;

function buildLayout<S>(def: NodeDefinition<S>, settings: S): Layout {
  const portMap = typeof def.ports === "function" ? def.ports(settings) : def.ports;
  const paramMap = typeof def.params === "function" ? def.params(settings) : (def.params ?? {});
  const ports: PortDesc[] = Object.entries(portMap).map(([id, spec]) => ({ id, ...spec }));
  // Inputs (and link ports) first, then outputs, each in declaration order - the C++ layer's order too.
  const ordered = [...ports.filter((p) => p.dir !== "out"), ...ports.filter((p) => p.dir === "out")];
  const params = Object.entries(paramMap).map(([id, spec]) => ({ id, ...spec }) as ParamDesc);
  const seen = new Set<string>();
  for (const p of [...ordered, ...params]) {
    if (!p.id) throw new Error(`${def.id}: a port or param needs a non-empty id`);
    if (seen.has(p.id)) throw new Error(`${def.id}: "${p.id}" is used by more than one port or param`);
    seen.add(p.id);
  }
  // The same checks the C++ parser applies to every layout (Descriptor.cpp), so a TS node cannot declare
  // what the planner would refuse.
  for (const p of ordered) {
    if (p.kind !== "audio" || !p.channels) continue;
    const c = p.channels;
    if ("any" in c && p.dir === "out") throw new Error(`${def.id}: output "${p.id}" cannot be "any"`);
    if ("match" in c) {
      const target = ordered.find((t) => t.id === c.match);
      if (!target || target === p || target.kind !== "audio")
        throw new Error(`${def.id}: "${p.id}" matches "${c.match}", which is not another audio port`);
    }
    if ("count" in c) {
      const known = layoutChannels(c.layout);
      if (!Number.isInteger(c.count) || c.count < 1 || c.count > MAX_CHANNELS)
        throw new Error(`${def.id}: "${p.id}" needs a whole channel count from 1 to ${MAX_CHANNELS}`);
      if (!c.layout) throw new Error(`${def.id}: "${p.id}" needs a layout name`);
      if (known === null && c.layout.startsWith("discrete("))
        throw new Error(`${def.id}: "${p.id}" layout "${c.layout}" is not a discrete(n) layout`);
      if (known !== null && known !== c.count)
        throw new Error(`${def.id}: "${p.id}" layout "${c.layout}" is ${known} channels, not ${c.count}`);
    }
  }
  for (const p of ordered)
    if (p.kind === "resource" && !p.resourceType) throw new Error(`${def.id}: resource port "${p.id}" needs a resourceType`);
  for (const p of params) {
    if (p.type === "enum") {
      if (p.values.length === 0 || p.values.some((v) => !v)) throw new Error(`${def.id}: enum param "${p.id}" needs non-empty values`);
      if (new Set(p.values).size !== p.values.length) throw new Error(`${def.id}: enum param "${p.id}" lists a value twice`);
      if (!p.values.includes(p.default))
        throw new Error(`${def.id}: enum param "${p.id}" default "${p.default}" is not one of its values`);
    } else if (p.type === "float" || p.type === "int") {
      if (![p.min, p.max, p.default].every(Number.isFinite)) throw new Error(`${def.id}: param "${p.id}" needs finite min, max and default`);
      if (!(p.min <= p.max)) throw new Error(`${def.id}: param "${p.id}" min must not exceed max`);
      if (!(p.min <= p.default && p.default <= p.max)) throw new Error(`${def.id}: param "${p.id}" default must lie within [min, max]`);
      if (p.type === "int" && ![p.min, p.max, p.default].every(Number.isInteger))
        throw new Error(`${def.id}: int param "${p.id}" needs whole-number min, max and default`);
    }
  }
  for (const group of def.exclusive ?? []) {
    if (group.length === 0) throw new Error(`${def.id}: an exclusive group needs members`);
    group.forEach((m, i) => {
      const wildcard = m.length > 2 && m.endsWith(".*");
      if (!m || (!wildcard && !ordered.some((p) => p.id === m && p.dir === "out")))
        throw new Error(`${def.id}: exclusive member "${m}" must be an output's id or a "prefix.*" pattern`);
      const covers = (pattern: string, id: string) => pattern.endsWith(".*") && id.startsWith(pattern.slice(0, -1));
      for (const other of group.slice(0, i)) {
        const otherId = other.endsWith(".*") ? other.slice(0, -1) : other;
        const mineId = wildcard ? m.slice(0, -1) : m;
        if (other === m || covers(m, otherId) || covers(other, mineId))
          throw new Error(`${def.id}: exclusive members "${other}" and "${m}" overlap`);
      }
    });
  }
  const layout: Layout = { ports: ordered, params };
  if (def.exclusive?.length) layout.exclusive = def.exclusive;
  return layout;
}

export function defineNode<S = Record<string, never>>(def: NodeDefinition<S>): DefinedNode<S> {
  // The C++ registry's isPlainId, code unit for byte: nothing at or below U+0020, nor U+007F.
  if (!def.id || /[\u0000-\u0020\u007f]/.test(def.id))
    throw new Error(`node type id ${JSON.stringify(def.id)} must be non-empty, with no whitespace or control characters`);
  const parseSettings = (raw: unknown): S => (def.settings ? def.settings.parse(raw ?? {}) : ({} as S));
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
      settings: def.settings ? (z.toJSONSchema(def.settings) as Record<string, unknown>) : NO_SETTINGS_SCHEMA,
    }),
  };
}

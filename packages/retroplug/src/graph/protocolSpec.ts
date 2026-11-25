// The protocol node contract and its checks (docs/downloadable-packs-m2e-plan.md): how buildGraph wires a protocol node
// (ProtocolNodeSpec), and the rules the live protocol tables (src/contributions/protocolRegistries.ts) hold every commit
// to, so a pack's protocol half meets at its load what the product's own packages meet at the app's. The generator
// keeps its build-time checks of the manifests (scripts/lib/packages.mjs); these judge the tables as they stand. Imports
// nothing but types, so the planner's bundle carries it with no node code and no schema.
import type { PortDesc } from "@retroplug/core/descriptor";
import type { SerialOutCaptureMode } from "./serialCapture";

/** The system-node inputs a protocol node feeds, each from its output of the same name. */
export type SystemInput = "serialIn" | "midi" | "bytes" | "buttons";

/** How buildGraph wires a protocol node type. */
export interface ProtocolNodeSpec {
  /** The input its system's routed MIDI arrives on, or null for a node that reads only the transport. */
  midiIn: string | null;
  /** The system inputs it writes, each by one node of its system at most: protocolPipeline keeps that so. */
  writes: readonly SystemInput[];
  /** It takes the controller's launches on `launches` when its system is the controller's target. */
  launches?: true;
  /** It takes its Game Boy's serial out on `serialOut`, in the block the core captured it, over a forward wire from
   *  the system, so it is placed after its system: what MI.OUT and master sync decode. */
  serialOut?: true;
  /** It sends host MIDI on `midiOut`, under its system's id. */
  midiOut?: true;
  /** The serial-out capture a system's core runs for each of this node's modes: `drive` (clock a transfer waiting on
   *  an external clock, capturing the bytes too), `listen` (capture the bytes the core clocks out itself) or `off`. The
   *  node's own package supplies it as data, because the mapping is the cart's, while the planner that applies it may
   *  carry no package. A mode this does not name captures nothing. */
  capture?: Readonly<Record<string, SerialOutCaptureMode>>;
  /** The type of the second node buildGraph places for the role, after the systems, as sys-<id>/<kind>/out with the
   *  same settings: a protocol node type that writes no system input, so protocolPipeline's one-writer rule
   *  (protocolNodes.ts) is the first node's alone. */
  out?: string;
}

/** Every system input, in the order a refusal lists them. */
export const SYSTEM_INPUTS: readonly SystemInput[] = ["serialIn", "midi", "bytes", "buttons"];

/** One protocol node as a manifest declares it: its type and, when it is wired, how. What the live table holds. */
export type ProtocolNodeEntry = { readonly type: string } & Partial<ProtocolNodeSpec>;

/** A role kind and the node type it is placed as. */
export interface RoleNodeEntry {
  readonly kind: string;
  readonly node: string;
}

/** An entry with the package that contributed it, as a registry's check is handed it. */
interface Owned<T> {
  readonly pkg: string;
  readonly item: T;
}

const NODE_KEYS = ["type", "midiIn", "writes", "launches", "serialOut", "midiOut", "capture", "out"];
const FLAGS = ["launches", "serialOut", "midiOut"] as const;
const CAPTURES = ["off", "listen", "drive"];

const has = (o: object, key: string): boolean => Object.prototype.hasOwnProperty.call(o, key);

/** Whether buildGraph wires the node: a role may be placed as it. */
export function isWired(entry: ProtocolNodeEntry): boolean {
  return entry.midiIn !== undefined || entry.writes !== undefined;
}

/** One entry's shape, which throws naming the package and the node. */
export function checkProtocolEntry(pkg: string, raw: unknown): void {
  if (raw === null || typeof raw !== "object" || Array.isArray(raw) || typeof (raw as { type?: unknown }).type !== "string" ||
      (raw as { type: string }).type === "") {
    throw new Error(`${pkg}: every protocol node needs a "type", the node's id`);
  }
  const e = raw as Record<string, unknown> & { type: string };
  const at = `${pkg}: the node ${e.type}`;
  for (const key of Object.keys(e)) {
    if (key === "label") throw new Error(`${at} has a "label": a span label is the host's to give, at load`);
    if (!NODE_KEYS.includes(key)) {
      throw new Error(`${at} has ${JSON.stringify(key)}, which no protocol node reads (a node's keys are ` +
        `${NODE_KEYS.join(", ")})`);
    }
  }
  const midiIn = has(e, "midiIn");
  const writes = has(e, "writes");
  if (midiIn !== writes) {
    throw new Error(`${at} declares ${midiIn ? "midiIn" : "writes"} without ${midiIn ? "writes" : "midiIn"}: a wired ` +
      "node declares both (an unwired one neither)");
  }
  if (!midiIn) {
    for (const key of [...FLAGS, "out"]) {
      if (has(e, key)) throw new Error(`${at} has ${key} and is not wired (no midiIn and writes), so nothing reads it`);
    }
  } else {
    if (e.midiIn !== null && (typeof e.midiIn !== "string" || e.midiIn === "")) {
      throw new Error(`${at}'s midiIn must be an input's name or null, not ${JSON.stringify(e.midiIn)}`);
    }
    if (!Array.isArray(e.writes)) throw new Error(`${at}'s writes is not a list`);
    for (const input of e.writes) {
      if (!SYSTEM_INPUTS.includes(input as SystemInput)) {
        throw new Error(`${at} writes ${JSON.stringify(input)}, which is no system input (${SYSTEM_INPUTS.join(", ")})`);
      }
    }
    for (const flag of FLAGS) {
      if (has(e, flag) && e[flag] !== true) {
        throw new Error(`${at}'s ${flag} must be true when it is set, not ${JSON.stringify(e[flag])}`);
      }
    }
    if (has(e, "out") && (typeof e.out !== "string" || e.out === "")) {
      throw new Error(`${at}'s out must name a node type, not ${JSON.stringify(e.out)}`);
    }
  }
  if (has(e, "capture")) {
    // A capture on an unwired node keeps the generator's message: nothing would decode the bytes it clocked.
    if (!midiIn) {
      throw new Error(`${at} declares a capture and is not wired (no midiIn and writes), so nothing would decode ` +
        "the bytes it clocked");
    }
    const capture = e.capture;
    if (capture === null || typeof capture !== "object" || Array.isArray(capture)) {
      throw new Error(`${at}'s capture is not an object of mode: capture`);
    }
    for (const [mode, value] of Object.entries(capture)) {
      if (!CAPTURES.includes(value as string)) {
        throw new Error(`${at}'s capture for ${JSON.stringify(mode)} is ${JSON.stringify(value)}, not "off", "listen" ` +
          'or "drive"');
      }
    }
  }
}

/** The whole node table, as a registry's check is handed it: each entry's shape, one owner per type, each capture
 *  decoded by a serial-out node its entry names as `out`, and each `out` a node placed after its system that only
 *  decodes (it writes no system input and reads neither routed MIDI nor launches), as buildGraph wires one. */
export function checkProtocolTable(entries: readonly Owned<ProtocolNodeEntry>[]): void {
  const byType = new Map<string, Owned<ProtocolNodeEntry>>();
  for (const entry of entries) {
    checkProtocolEntry(entry.pkg, entry.item);
    const at = byType.get(entry.item.type);
    if (at) {
      throw new Error(at.pkg === entry.pkg
        ? `the package ${entry.pkg} declares the script node ${entry.item.type} twice`
        : `two packages declare the script node ${entry.item.type}: ${at.pkg} and ${entry.pkg}`);
    }
    byType.set(entry.item.type, entry);
  }
  const outs = new Set<string>();
  for (const { pkg, item } of entries) {
    if (item.out === undefined) continue;
    outs.add(item.out);
    const decoder = byType.get(item.out)?.item;
    if (item.capture !== undefined) {
      if (!decoder) {
        throw new Error(`${pkg}: the node ${item.type} declares a capture decoded at ${item.out}, which no package ` +
          "declares");
      }
      if (decoder.serialOut !== true) {
        throw new Error(`${pkg}: the node ${item.type} declares a capture decoded at ${item.out}, which declares no ` +
          "serialOut");
      }
    }
    if (!decoder || !isWired(decoder) || (decoder.writes ?? []).length > 0 || decoder.midiIn !== null ||
        decoder.launches !== undefined) {
      throw new Error(`${pkg}: the node ${item.type}'s out ${item.out} must be a wired node that writes no system ` +
        "input and reads neither routed MIDI nor launches");
    }
  }
  for (const { pkg, item } of entries) {
    if (item.capture !== undefined && item.out === undefined) {
      throw new Error(`${pkg}: the node ${item.type} declares a capture and no out: a capture is for the serial-out ` +
        "node that decodes the bytes");
    }
    if (item.serialOut === true && !outs.has(item.type)) {
      throw new Error(`${pkg}: the node ${item.type} reads its system's serialOut and is no node's out, so nothing ` +
        "would wire it");
    }
  }
}

/** The role table against the node table: one owner per kind, each placed as a declared node buildGraph wires. */
export function checkRoleNodes(roles: readonly Owned<RoleNodeEntry>[], nodes: readonly Owned<ProtocolNodeEntry>[]): void {
  const owners = new Map<string, string>();
  for (const { pkg, item } of roles) {
    if (typeof item.kind !== "string" || item.kind === "" || typeof item.node !== "string" || item.node === "") {
      throw new Error(`${pkg}: a role's kind and node must be non-empty strings`);
    }
    const at = owners.get(item.kind);
    if (at !== undefined) {
      throw new Error(at === pkg
        ? `the package ${pkg} declares the role kind ${item.kind} twice`
        : `two packages declare the role kind ${item.kind}: ${at} and ${pkg}`);
    }
    owners.set(item.kind, pkg);
    const declared = nodes.find((n) => n.item.type === item.node)?.item;
    if (!declared) throw new Error(`${pkg}: the role ${item.kind} names ${item.node}, which no package declares`);
    if (!isWired(declared)) {
      throw new Error(`${pkg}: the role ${item.kind} names ${item.node}, which declares no midiIn and writes to wire it`);
    }
  }
}

/** A port as buildGraph wires it: "<id> <dir> <kind>". */
export const portKey = (p: Pick<PortDesc, "id" | "dir" | "kind">): string => `${p.id} ${p.dir} ${p.kind}`;

/** The ports a wired node's spec says it has, as buildGraph wires each, sorted. A system input a node writes is an
 *  output of the kind the system node takes it as: serial bytes into a Game Boy, core bytes into a NES or an SMS, the
 *  rest events. */
export function portsOf(spec: ProtocolNodeSpec): string[] {
  const out: string[] = [];
  if (spec.midiIn !== null) out.push(portKey({ id: spec.midiIn, dir: "in", kind: "events" }));
  if (spec.launches) out.push(portKey({ id: "launches", dir: "in", kind: "events" }));
  if (spec.serialOut) out.push(portKey({ id: "serialOut", dir: "in", kind: "bytes" }));
  for (const input of spec.writes)
    out.push(portKey({ id: input, dir: "out", kind: input === "serialIn" || input === "bytes" ? "bytes" : "events" }));
  if (spec.midiOut) out.push(portKey({ id: "midiOut", dir: "out", kind: "events" }));
  return out.sort();
}

/** A pack's wired node against the ports its descriptor declares (default settings: no protocol node's ports follow
 *  its settings). A port the spec names that the node lacks is a wire the planner refuses, and with it the whole
 *  document; one the node has that the spec does not name is never wired. */
export function checkProtocolPorts(type: string, spec: ProtocolNodeSpec,
                                   ports: readonly Pick<PortDesc, "id" | "dir" | "kind">[]): void {
  const actual = ports.map(portKey).sort();
  const wired = portsOf(spec);
  if (actual.join("\n") !== wired.join("\n")) {
    throw new Error(`its protocol node ${type} has the ports ${actual.join(", ")}, and its pack.json's protocol.nodes ` +
      `wires ${wired.join(", ")}`);
  }
}

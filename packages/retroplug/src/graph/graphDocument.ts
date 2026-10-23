// The graph document TS owns and sends to the C++ planner with setGraph - the JSON shape of
// packages/native/src/graph/GraphDocument.hpp. Node ids may not contain ".", so an endpoint always splits
// at its first dot: "gb1.pins.pulse" is node "gb1", port "pins.pulse".
import { canonicalJson } from "./canonicalJson";
import { plainId, wellFormed } from "./descriptor";

export interface NodeDoc {
  id: string;
  type: string;
  settings?: Record<string, unknown>;
  /** Param values: a number, an enum label, or a bool. */
  params?: Record<string, number | string | boolean>;
  /** Resource input port -> "res:<id>". */
  resources?: Record<string, string>;
}

export interface WireDoc {
  from: string;
  to: string;
  feedback?: boolean;
}

export interface GraphDocument {
  nodes: NodeDoc[];
  wires: WireDoc[];
}

const q = (s: string): string => JSON.stringify(s);

/** The planner's node-id rule: a plain id (see plainId) with no ".", which would make a split ambiguous. */
export const isNodeId = (id: unknown): id is string => plainId(id) && !id.includes(".");

/** "node.port" for a wire endpoint. Throws for a node id the planner would refuse, or a port id that is
 *  not a plain id (an empty one would not even split back). */
export function endpoint(node: string, port: string): string {
  if (!isNodeId(node))
    throw new Error(`node id ${q(node)} must be non-empty and contain no ".", whitespace, control characters or lone surrogates`);
  if (!plainId(port)) throw new Error(`port id ${q(port)} must be non-empty, with no whitespace, control characters or lone surrogates`);
  return `${node}.${port}`;
}

/** Split an endpoint at its first dot; null when it has no node or no port. */
export function parseEndpoint(text: string): { node: string; port: string } | null {
  const dot = text.indexOf(".");
  if (dot <= 0 || dot === text.length - 1) return null;
  return { node: text.slice(0, dot), port: text.slice(dot + 1) };
}

const isPlainObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === "object" && v !== null && !Array.isArray(v) && [Object.prototype, null].includes(Object.getPrototypeOf(v));

// Why a value is not plain JSON that C++ reads as written, or null. Every string (and key) must have a
// UTF-8 spelling; NaN and infinities would spell null, and so would functions; typed arrays, dates and
// maps would be spelt as something else entirely.
function unreadable(v: unknown, where: string): string | null {
  switch (typeof v) {
    case "string":
      return wellFormed(v) ? null : `${where} holds a lone surrogate, which C++ cannot read`;
    case "number":
      return Number.isFinite(v) ? null : `${where} holds ${v}, which JSON cannot carry`;
    case "boolean":
      return null;
    case "object":
      if (v === null) return null;
      if (Array.isArray(v)) {
        for (let i = 0; i < v.length; ++i) {
          if (v[i] === undefined) return `${where}[${i}] is undefined`;
          const why = unreadable(v[i], `${where}[${i}]`);
          if (why) return why;
        }
        return null;
      }
      if (!isPlainObject(v)) return `${where} is not a plain JSON object`;
      for (const [k, x] of Object.entries(v)) {
        if (!wellFormed(k)) return `${where} has a key with a lone surrogate`;
        const why = x === undefined ? null : unreadable(x, `${where}.${k}`);
        if (why) return why;
      }
      return null;
    default:
      return `${where} holds a ${typeof v}, which JSON cannot carry`;
  }
}

/** Why the C++ parser or the planner's structure check would refuse this node, or null. */
export function checkNode(n: NodeDoc): string | null {
  if (!isNodeId(n.id)) return `id ${q(String(n.id))} is not a node id (a plain id with no ".")`;
  if (!plainId(n.type)) return `type ${q(String(n.type))} is not a node type id`;
  if (n.settings !== undefined) {
    if (!isPlainObject(n.settings)) return "settings must be an object";
    const why = unreadable(n.settings, "settings");
    if (why) return why;
  }
  if (n.params !== undefined) {
    if (!isPlainObject(n.params)) return "params must be an object";
    for (const [id, v] of Object.entries(n.params)) {
      if (!plainId(id)) return `param id ${q(id)} is not a plain id`;
      if (!["number", "string", "boolean"].includes(typeof v)) return `param ${q(id)} must be a number, label or bool`;
      const why = unreadable(v, `param ${q(id)}`);
      if (why) return why;
    }
  }
  if (n.resources !== undefined) {
    if (!isPlainObject(n.resources)) return "resources must be an object";
    for (const [port, ref] of Object.entries(n.resources)) {
      if (!plainId(port)) return `resource port ${q(port)} is not a plain id`;
      if (typeof ref !== "string" || !/^res:[1-9][0-9]*$/.test(ref) || Number(ref.slice(4)) > 4294967295)
        return `resource ${q(port)} must be "res:<id>", not ${q(String(ref))}`;
    }
  }
  return null;
}

/** Why the C++ parser would refuse this wire, or null. */
export function checkWire(w: WireDoc): string | null {
  if (typeof w.from !== "string" || typeof w.to !== "string" || !parseEndpoint(w.from) || !parseEndpoint(w.to))
    return `${q(String(w.from))} -> ${q(String(w.to))} needs node.port endpoints`;
  const why = unreadable(w.from, "from") ?? unreadable(w.to, "to");
  if (why) return why;
  if (w.feedback !== undefined && typeof w.feedback !== "boolean") return "feedback must be true or false";
  return null;
}

// Only the fields GraphDocument.hpp reads: C++ refuses a whole document over one unknown field, which TS's
// structural types would let through.
function projectNode(n: NodeDoc): Record<string, unknown> {
  const out: Record<string, unknown> = { id: n.id, type: n.type };
  if (n.settings !== undefined) out.settings = n.settings;
  if (n.params !== undefined) out.params = n.params;
  if (n.resources !== undefined) out.resources = n.resources;
  return out;
}

function projectWire(w: WireDoc): Record<string, unknown> {
  const out: Record<string, unknown> = { from: w.from, to: w.to };
  if (w.feedback !== undefined) out.feedback = w.feedback;
  return out;
}

/** The text to send with setGraph: only the fields GraphDocument.hpp reads, in canonical JSON. Throws,
 *  naming the node or wire, for anything the C++ parser or the planner's structure check would refuse
 *  (checkNode, checkWire, and a node id used twice). */
export function toGraphJson(doc: GraphDocument): string {
  const ids = new Set<string>();
  const nodes = doc.nodes.map((n, i) => {
    const why = checkNode(n);
    if (why) throw new Error(`node #${i} ${q(String(n.id))}: ${why}`);
    if (ids.has(n.id)) throw new Error(`node id ${q(n.id)} is used twice`);
    ids.add(n.id);
    return projectNode(n);
  });
  const wires = doc.wires.map((w, i) => {
    const why = checkWire(w);
    if (why) throw new Error(`wire #${i}: ${why}`);
    return projectWire(w);
  });
  return canonicalJson({ nodes, wires });
}

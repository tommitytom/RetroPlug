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

// Every string (and key) in a JSON value is one C++ can read; NaN and infinities spell null, which no
// param or setting means.
function readable(v: unknown, where: string): void {
  if (typeof v === "string") {
    if (!wellFormed(v)) throw new Error(`${where} holds a lone surrogate, which C++ cannot read`);
  } else if (typeof v === "number") {
    if (!Number.isFinite(v)) throw new Error(`${where} holds ${v}, which JSON cannot carry`);
  } else if (Array.isArray(v)) {
    v.forEach((x, i) => readable(x, `${where}[${i}]`));
  } else if (v && typeof v === "object") {
    for (const [k, x] of Object.entries(v)) {
      if (!wellFormed(k)) throw new Error(`${where} has a key with a lone surrogate`);
      readable(x, `${where}.${k}`);
    }
  }
}

/** The text to send with setGraph: only the fields GraphDocument.hpp reads (C++ refuses the whole document
 *  over one unknown field, which TS's structural types would let through), in canonical JSON. Throws, with
 *  the node it is about, for anything the C++ parser or the planner's structure check would refuse. */
export function toGraphJson(doc: GraphDocument): string {
  const nodes = doc.nodes.map((n, i) => {
    const where = `node #${i}`;
    if (!isNodeId(n.id)) throw new Error(`${where}: id ${q(String(n.id))} is not a node id (a plain id with no ".")`);
    if (!plainId(n.type)) throw new Error(`node ${q(n.id)}: type ${q(String(n.type))} is not a node type id`);
    const out: Record<string, unknown> = { id: n.id, type: n.type };
    if (n.settings !== undefined) {
      if (typeof n.settings !== "object" || n.settings === null || Array.isArray(n.settings))
        throw new Error(`node ${q(n.id)}: settings must be an object`);
      readable(n.settings, `node ${q(n.id)} settings`);
      out.settings = n.settings;
    }
    if (n.params !== undefined) {
      for (const [id, v] of Object.entries(n.params)) {
        if (!["number", "string", "boolean"].includes(typeof v)) throw new Error(`node ${q(n.id)}: param ${q(id)} must be a number, label or bool`);
        readable(v, `node ${q(n.id)} param ${q(id)}`);
        readable(id, `node ${q(n.id)} params`);
      }
      out.params = n.params;
    }
    if (n.resources !== undefined) {
      for (const [port, ref] of Object.entries(n.resources))
        if (!/^res:[1-9][0-9]*$/.test(ref) || Number(ref.slice(4)) > 4294967295)
          throw new Error(`node ${q(n.id)}: resource ${q(port)} must be "res:<id>", not ${q(String(ref))}`);
      out.resources = n.resources;
    }
    return out;
  });
  const wires = doc.wires.map((w, i) => {
    if (!parseEndpoint(w.from) || !parseEndpoint(w.to)) throw new Error(`wire #${i}: ${q(w.from)} -> ${q(w.to)} needs node.port endpoints`);
    readable(w.from, `wire #${i}`);
    readable(w.to, `wire #${i}`);
    const out: Record<string, unknown> = { from: w.from, to: w.to };
    if (w.feedback !== undefined) {
      if (typeof w.feedback !== "boolean") throw new Error(`wire #${i}: feedback must be true or false`);
      out.feedback = w.feedback;
    }
    return out;
  });
  return canonicalJson({ nodes, wires });
}

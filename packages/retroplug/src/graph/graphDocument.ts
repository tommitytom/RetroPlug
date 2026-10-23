// The graph document TS owns and sends to the C++ planner with setGraph - the JSON shape of
// packages/native/src/graph/GraphDocument.hpp. Node ids may not contain ".", so an endpoint always splits
// at its first dot: "gb1.pins.pulse" is node "gb1", port "pins.pulse".
import { plainId } from "./descriptor";

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

/** "node.port" for a wire endpoint. Throws for a node id the planner would refuse: one that is not a plain
 *  id (see plainId), or that contains "." and so would make the split ambiguous. */
export function endpoint(node: string, port: string): string {
  if (!plainId(node) || node.includes("."))
    throw new Error(`node id ${JSON.stringify(node)} must be non-empty and contain no ".", whitespace or control characters`);
  return `${node}.${port}`;
}

/** Split an endpoint at its first dot; null when it has no node or no port. */
export function parseEndpoint(text: string): { node: string; port: string } | null {
  const dot = text.indexOf(".");
  if (dot <= 0 || dot === text.length - 1) return null;
  return { node: text.slice(0, dot), port: text.slice(dot + 1) };
}

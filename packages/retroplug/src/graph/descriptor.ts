// The descriptor model shared with C++ (packages/native/src/graph/Descriptor.hpp): what a node type
// declares (TypeDescriptor) and what one instance resolves to for its settings (Layout). These are the
// JSON shapes, field for field; canonicalJson spells them identically on both sides. The id rules both
// sides share are here too.

/** Can C++ read this string? A lone surrogate has no UTF-8 spelling, so its parser refuses one. */
export function wellFormed(s: string): boolean {
  for (let i = 0; i < s.length; ++i) {
    const c = s.charCodeAt(i);
    if (c >= 0xd800 && c <= 0xdbff) {
      const next = s.charCodeAt(i + 1);
      if (!(next >= 0xdc00 && next <= 0xdfff)) return false;
      ++i;
    } else if (c >= 0xdc00 && c <= 0xdfff) return false;
  }
  return true;
}

/** The C++ isPlainId (Descriptor.hpp), code unit for byte: non-empty, nothing at or below U+0020, nor
 *  U+007F, and (since C++ reads UTF-8) no lone surrogate. Node type ids, package ids, node ids, port and
 *  param ids and resource types all follow it. */
export const plainId = (s: unknown): s is string =>
  typeof s === "string" && s.length > 0 && !/[\u0000-\u0020\u007f]/.test(s) && wellFormed(s);

export type PortKind = "audio" | "events" | "bytes" | "control" | "link" | "resource";
export type PortDir = "in" | "out" | "link";

/** An audio port's channels: a fixed count + layout, any (inputs only), or a copy of another port. A fixed
 *  layout of "any" fixes the count only. Exactly one form: the `never` members stop an object literal from
 *  mixing two (`{ any: true, count: 2 }`), which C++'s parser refuses. */
export type ChannelSpec =
  | { any: true; match?: never; count?: never; layout?: never }
  | { match: string; any?: never; count?: never; layout?: never }
  | { count: number; layout: string; any?: never; match?: never };

/** An events or bytes output's queue: the most messages, and the most payload bytes, one block carries (a push
 *  past either is dropped and counted). An output declares either part or both; one it leaves out takes the
 *  default, 256 messages and 4096 bytes, and neither may pass 1 Mi messages or 16 MiB. A wire carries its
 *  source's queue (an output queue, or a feedback wire's front, sized from the source too), so an input has no
 *  queue of its own to size, and one that declares a capacity is refused. The twin of QueueCapacity in
 *  Descriptor.hpp. */
export interface QueueCapacity {
  messages?: number;
  bytes?: number;
}

export interface PortDesc {
  id: string;
  dir: PortDir;
  kind: PortKind;
  channels?: ChannelSpec;   // audio only
  resourceType?: string;    // resource only
  access?: "write";         // a resource input that writes the resource
  capacity?: QueueCapacity; // events / bytes outputs only
}

export type ParamDesc =
  | { id: string; name: string; type: "float" | "int"; min: number; max: number; default: number; unit?: string }
  | { id: string; name: string; type: "bool"; default: boolean }
  | { id: string; name: string; type: "enum"; values: string[]; default: string };

export interface Layout {
  ports: PortDesc[];
  params: ParamDesc[];
  exclusive?: string[][];
}

export interface TypeDescriptor extends Layout {
  id: string;
  version: string;
  name: string;
  category: string;
  requires: { heap: boolean; threads: boolean };
  /** JSON Schema of the settings object. Generator-specific (zod here, reflect-cpp in C++), so it is the
   *  one part of a descriptor two languages are not expected to spell identically - except the no-settings
   *  case, which both write as {"type":"object"}. */
  settings: Record<string, unknown>;
}

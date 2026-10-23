// The descriptor model shared with C++ (packages/native/src/graph/Descriptor.hpp): what a node type
// declares (TypeDescriptor) and what one instance resolves to for its settings (Layout). These are the
// JSON shapes, field for field; canonicalJson spells them identically on both sides.

export type PortKind = "audio" | "events" | "bytes" | "control" | "link" | "resource";
export type PortDir = "in" | "out" | "link";

/** An audio port's channels: a fixed count + layout, any (inputs only), or a copy of another port. A fixed
 *  layout of "any" fixes the count only. */
export type ChannelSpec = { any: true } | { match: string } | { count: number; layout: string };

export interface PortDesc {
  id: string;
  dir: PortDir;
  kind: PortKind;
  channels?: ChannelSpec;   // audio only
  resourceType?: string;    // resource only
  access?: "write";         // a resource input that writes the resource
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

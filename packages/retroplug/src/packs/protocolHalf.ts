// A pack's protocol half as the live tables' additions (docs/downloadable-packs-m2e-plan.md): its nodes and roles, each
// wired node's ports first held to the descriptor the host's trial load read, so a wiring the planner would refuse is
// the pack's refusal and not every document's. The tables' own checks (graph/protocolSpec.ts) run at the commit.
import type { Addition } from "../contributions/registry";
import { protocolNodesRegistry, roleNodesRegistry } from "../contributions/protocolRegistries";
import { checkProtocolEntry, checkProtocolPorts, isWired, type ProtocolNodeEntry, type ProtocolNodeSpec,
  type RoleNodeEntry } from "../graph/protocolSpec";
import type { PackProtocol } from "./packHost";

export function protocolAdditions(pkg: string, protocol: PackProtocol | undefined): Addition<any>[] {
  if (!protocol) throw new Error("its pack.json names a protocol half, and the host listed no protocol member");
  if (!Array.isArray(protocol.nodes)) throw new Error("its protocol member's nodes is not a list");
  const isObject = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
  if (!isObject(protocol.roles)) throw new Error("its protocol member's roles is not an object");
  if (!isObject(protocol.descriptors)) throw new Error("its protocol member's descriptors is not an object");
  for (const raw of protocol.nodes) {
    checkProtocolEntry(pkg, raw);
    const entry = raw as ProtocolNodeEntry;
    if (!isWired(entry)) continue;
    const descriptor = Object.prototype.hasOwnProperty.call(protocol.descriptors, entry.type)
      ? protocol.descriptors[entry.type] : undefined;
    if (!descriptor) throw new Error(`its protocol node ${entry.type} has no descriptor (the host did not load it)`);
    if (!isObject(descriptor) || !Array.isArray(descriptor.ports)) {
      throw new Error(`its protocol node ${entry.type}'s descriptor has no ports list`);
    }
    checkProtocolPorts(entry.type, entry as ProtocolNodeSpec, descriptor.ports);
  }
  const roles: RoleNodeEntry[] = Object.entries(protocol.roles).map(([kind, node]) => ({ kind, node }));
  return [
    { registry: protocolNodesRegistry, pkg, items: protocol.nodes as ProtocolNodeEntry[] },
    { registry: roleNodesRegistry, pkg, items: roles },
  ];
}

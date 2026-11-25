// The protocol tables, as live registries (docs/downloadable-packs-m2e-plan.md): every protocol node the product's
// packages and the loaded packs declare, wired or not, and the role kinds placed as one. The generated
// src/generated/protocol.ts seeds them in span-label order; a pack's protocol half adds to them at its commit
// (src/packs/protocolHalf.ts). The checks are graph/protocolSpec.ts's, run over the whole staged table at every
// commit. Only registry.ts and that module are imported, so the planner's bundle carries no node code and no schema.
import { Registry } from "./registry";
import { checkProtocolTable, checkRoleNodes, type ProtocolNodeEntry, type RoleNodeEntry } from "../graph/protocolSpec";

export const protocolNodesRegistry = new Registry<ProtocolNodeEntry>("protocolNodes", (entries) =>
  checkProtocolTable(entries));

export const roleNodesRegistry = new Registry<RoleNodeEntry>("roleNodes", (entries, view) =>
  checkRoleNodes(entries, view(protocolNodesRegistry)));

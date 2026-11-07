// Core's protocol contribution (docs/modular-graph-phase-f1-design.md section 3): the MIDI routing node, span label 16,
// which the generated script bundle registers first. The manifest names this module and the label; the node's own code
// is midiRoute.ts, so a consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES) imports the generated
// table and never this module.
import type { DefinedNode } from "./defineNode";
import { midiRouteNode } from "./midiRoute";

const protocolNodes: readonly DefinedNode<any>[] = [midiRouteNode];

export default protocolNodes;

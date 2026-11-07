// n8's protocol contribution: the NES host-MIDI passthrough node, span label 18. The manifest names this module and
// the label; the node's own code is n8MidiToFifoNode.ts, so a consumer that only needs the protocol data
// (buildGraph's PROTOCOL_NODES) imports the generated table and never this module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { n8MidiToFifoNode } from "./n8MidiToFifoNode";

const protocolNodes: readonly DefinedNode<any>[] = [n8MidiToFifoNode];

export default protocolNodes;

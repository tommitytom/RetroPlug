// mGB's protocol contribution: the MIDI-to-serial node, span label 17. The manifest names this module and the label; the
// node's own code is mgbMidiNode.ts, so a consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES)
// imports the generated table and never this module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { mgbMidiNode } from "./mgbMidiNode";

const protocolNodes: readonly DefinedNode<any>[] = [mgbMidiNode];

export default protocolNodes;

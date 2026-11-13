// genmddj's protocol contribution: the host-sync node, span label 24. The manifest names this module and the label; the
// node's own code is genmddjSyncNode.ts, so a consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES)
// imports the generated table and never this module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { genmddjSyncNode } from "./genmddjSyncNode";

const protocolNodes: readonly DefinedNode<any>[] = [genmddjSyncNode];

export default protocolNodes;

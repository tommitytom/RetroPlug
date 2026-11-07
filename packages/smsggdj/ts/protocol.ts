// smsggdj's protocol contribution: the host-sync node, span label 20. The manifest names this module and the label; the
// node's own code is smsSyncNode.ts, so a consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES)
// imports the generated table and never this module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { smsSyncNode } from "./smsSyncNode";

const protocolNodes: readonly DefinedNode<any>[] = [smsSyncNode];

export default protocolNodes;

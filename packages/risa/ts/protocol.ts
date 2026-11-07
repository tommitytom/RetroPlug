// risa's protocol contribution: the host-sync node, span label 19. The manifest names this module and the label; the
// node's own code is risaSyncNode.ts, so a consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES)
// imports the generated table and never this module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { risaSyncNode } from "./risaSyncNode";

const protocolNodes: readonly DefinedNode<any>[] = [risaSyncNode];

export default protocolNodes;

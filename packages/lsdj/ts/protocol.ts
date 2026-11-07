// LSDj's protocol contribution: the host-to-cart sync node (span label 21) and the cart-to-host serial-out decoder (23),
// in that order within the package (the bundle sorts every package's nodes by their manifest labels anyway). The
// manifest names this module and the labels; the nodes' own code is lsdjSyncNode.ts and lsdjSerialOutNode.ts, so a
// consumer that only needs the protocol data (buildGraph's PROTOCOL_NODES) imports the generated table and never this
// module.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { lsdjSyncNode } from "./lsdjSyncNode";
import { lsdjSerialOutNode } from "./lsdjSerialOutNode";

const protocolNodes: readonly DefinedNode<any>[] = [lsdjSyncNode, lsdjSerialOutNode];

export default protocolNodes;

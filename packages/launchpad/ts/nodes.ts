// The package's script nodes, its manifest's retroplug.protocol.module: the array the generated bundle entry
// (packages/retroplug/src/generated/scriptNodes.ts) registers. This module only imports the node module; which type
// it contributes, and where its label sits, is the manifest's statement.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { controllerSessionNode } from "./controllerSession";

export const launchpadNodes: readonly DefinedNode<any>[] = [controllerSessionNode];

export default launchpadNodes;

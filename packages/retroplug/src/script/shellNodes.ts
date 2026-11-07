// The shell's script-node definitions, its package.json's retroplug.protocol.module
// (docs/modular-graph-phase-f1-design.md section 3): the array the generated bundle entry
// (src/generated/scriptNodes.ts) registers, in the span-label order the manifest gives each type. This module only
// imports the node modules; which type a package contributes, and where its label sits, is the manifest's statement, so
// the bundle's order does not depend on this file's order.
import type { DefinedNode } from "@retroplug/core/defineNode";
import { controllerSessionNode } from "./nodes/controllerSession";

// The one production script node type still the shell's (spec/08-profiling.md: one span label each, 16 up, a type
// added goes last): the control-surface session (docs/modular-graph-phase-b3b-design.md section 2), which moves with
// the controller in F3. Every cart's node - and the NES's, and the MIDI routing core's - is its package's protocol
// module. Nothing here registers roles or imports zod: a node's settings are @retroplug/core/settingsSchema's, and
// test/script/bundle-no-zod.test.ts holds the bundle to that.
const shellNodes: readonly DefinedNode<any>[] = [controllerSessionNode];

export default shellNodes;

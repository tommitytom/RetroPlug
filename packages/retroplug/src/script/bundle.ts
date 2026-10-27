// The script-node bundle (docs/modular-graph-phase-b3a-design.md section 2.5): every TS node type the engine
// runs. esbuilt to a global-code IIFE, compiled to QuickJS bytecode at build time and embedded in the backend
// library (packages/native/cmake/bundles.cmake, retroplug-script-bundle), so every Engine loads it into both of
// its script contexts at construction and every host has the same node types, with no boot call. An entry point
// only: importing it registers.
import type { DefinedNode } from "../graph/defineNode";
import { kernelNode } from "./kernelNode";
import { midiRouteNode } from "./nodes/midiRoute";
import { mgbMidiNode } from "./nodes/mgbMidi";
import { n8MidiToFifoNode } from "./nodes/n8MidiToFifo";
import { risaSyncNode } from "./nodes/risaSync";
import { smsSyncNode } from "./nodes/smsSync";
import { lsdjSyncNode } from "./nodes/lsdjSync";
import { controllerSessionNode } from "./nodes/controllerSession";
import { registerScriptNodes } from "./register";

// The production script node types, in registration order: the DSP kernel, which no document places any more, then
// the per-role nodes that replaced it (docs/modular-graph-phase-b3b-design.md section 2), which buildGraph places.
const scriptNodes: readonly DefinedNode<any>[] = [
  kernelNode,
  midiRouteNode,
  mgbMidiNode,
  n8MidiToFifoNode,
  risaSyncNode,
  smsSyncNode,
  lsdjSyncNode,
  controllerSessionNode,
];

registerScriptNodes(scriptNodes);

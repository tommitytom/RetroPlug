// The script-node bundle (docs/modular-graph-phase-b3a-design.md section 2.5): every TS node type the engine
// runs. esbuilt to a global-code IIFE, compiled to QuickJS bytecode at build time and embedded in the backend
// library (packages/native/cmake/bundles.cmake, retroplug-script-bundle), so every Engine loads it into both of
// its script contexts at construction and every host has the same node types, with no boot call. An entry point
// only: importing it registers.
import type { DefinedNode } from "../graph/defineNode";
import { kernelNode } from "./kernelNode";
import { registerScriptNodes } from "./register";

// The production script node types: the DSP kernel, beside the native one the engine still runs until it switches.
const scriptNodes: readonly DefinedNode<any>[] = [kernelNode];

registerScriptNodes(scriptNodes);

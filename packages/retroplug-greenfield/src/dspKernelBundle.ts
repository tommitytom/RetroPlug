// The DSP role kernel, bundled for the BARE QuickJS DSP context (packages/native-greenfield). esbuild
// bundles this entry + its imports into one IIFE; native compiles it to bytecode and loads it into
// the DSP runtime, which has ALREADY bound the pushSerialIn / emitMidiOut / pressButton sink thunks
// on the global. This entry wires those thunks into a SinkTarget, builds the kernel with the built-in
// roles, and exposes the two globals native calls: setSystems(json) once per structure change, and
// processBlock(input) once per audio block. A JSValue never crosses — only bytes and scalars.

import { DspKernel, type SinkTarget, type BlockInput, type KernelStructure } from "./dspKernel";
import { RoleRegistry } from "./systemRoles";
import { registerDspRoles } from "./dspRoles";

// The system-addressed sink thunks DspRuntime bound on the global (see DspRuntime.cpp). Declared for
// the type-checker; provided by the host at runtime as free globals.
declare function pushSerialIn(system: number, frame: number, byte: number): void;
declare function emitMidiOut(system: number, frame: number, data: number[]): void;
declare function pressButton(system: number, frame: number, button: number, down: boolean): void;

const registry = new RoleRegistry();
registerDspRoles(registry);

// Forward each kernel sink straight to the bound C thunk — no intermediate JS Sinks arrays.
const sink: SinkTarget = {
  pushSerialIn: (system, frame, byte) => pushSerialIn(system, frame, byte),
  emitMidiOut: (system, frame, data) => emitMidiOut(system, frame, data),
  pressButton: (system, frame, button, down) => pressButton(system, frame, button, down),
};

const kernel = new DspKernel(registry, sink);

const g = globalThis as Record<string, unknown>;
g.setSystems = (json: string): void => kernel.setSystems(JSON.parse(json) as KernelStructure);
g.processBlock = (input: BlockInput): void => {
  kernel.processBlock(input);
};

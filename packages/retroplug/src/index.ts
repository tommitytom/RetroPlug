// @retroplug/retroplug — the TypeScript layer over the native core: the
// synchronous in-process RPC client plus the `emu` domain ergonomics over the
// generated HarnessService surface, shared by the CLI test harness and the
// end-user CLI.

export { createSyncClient, type Unpromisify, type RpcSend } from "./createSyncClient";
export { harnessRpcSend } from "./syncTransport";
export {
  createEmu, printProfile, Button, Mem, Routing,
  type Emu,
  type ButtonId, type MemType, type RoutingId,
  type CpuRegisters, type ProfiledFunction, type DisasmLine, type TraceLine,
  type CallFrame, type MidiOutEvent, type SerialOutByte, type KitSample,
  type BreakpointSpec, type BreakInfo, type Frame, type ChordOpts,
} from "./emu";

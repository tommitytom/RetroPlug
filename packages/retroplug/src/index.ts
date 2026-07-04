// @retroplug/retroplug — the TypeScript layer over the native core: the
// synchronous in-process RPC client plus the `emu` domain ergonomics over the
// generated HarnessService surface, shared by the CLI test harness and the
// end-user CLI.

export { createSyncClient, type Unpromisify, type RpcSend } from "./createSyncClient";
export { harnessRpcSend, hostArgv, hostExit } from "./syncTransport";
export {
  createEmu, printProfile, Button, Mem, Routing,
  type Emu,
  type ButtonId, type MemType, type RoutingId,
  type CpuRegisters, type ProfiledFunction, type DisasmLine, type TraceLine,
  type CallFrame, type MidiOutEvent, type SerialOutByte, type KitSample,
  type BreakpointSpec, type BreakInfo, type Frame, type ChordOpts,
  type ApuState, type ApuSquareState, type ApuTriangleState,
  type ApuNoiseState, type ApuDmcState,
} from "./emu";

// Shared project-serialization orchestration (moved out of C++).
export { K_PROJECT, VersionCheck, checkVersion, parseProjectVersion } from "./schemaVersions";
export { PROJECT_JSON, blobKey, parseBlobKey, isBlobEntry, type BlobKey, type BlobKind } from "./projectBinaries";
export { saveRplg, saveProjectFile, loadRplg, type ProjectHost, type Blob } from "./projectSerialization";
export type {
  ProjectConfig, ProjectSettings, SystemConfig, SystemKind,
  SameBoyConfig, MesenNesConfig, MesenGbaConfig, RoleConfig, LsdjKitConfig, KitSampleConfig,
} from "./projectConfig";

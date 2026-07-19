// The public authoring surface of the retroplug-cli test SDK: one barrel that re-exports everything a
// ROM developer needs to drive a system and assert on its state. tools/build-cli-sdk.mjs esbuilds this
// into a single self-contained ESM (retroplug-cli.js, DSP kernel baked in) plus a rolled-up
// retroplug-cli.d.ts, shipped to consumer repos (e.g. evermidi) so they author tests with only esbuild
// (bundle) + tsc (typecheck) — no copy of this package's src/ tree.
//
// Keep this the SSOT for what's "public": adding a symbol here widens the SDK; nothing else does.
// `runSession` is intentionally NOT exported — a test file uses `test()`/`bootSession()` (runSession
// would call tjs.exit(0) synchronously and preempt the harness, reporting a false pass).

export { bootSession, hostArgs } from "./session";
export type { Session } from "./session";

export { Timeline, renderTimeline, Button } from "./timeline";
export type { TimelineEvent } from "./timeline";

export { encodeWav } from "./wav";

export { test, expect } from "../testing/harness";

// Live-core read types (getApuState / getPpuState / getCpuRegisters / drainEvents). NES-only today;
// the shapes mirror the native reflect-cpp structs field-for-field.
export type {
  Backend,
  ApuState,
  ApuSquareState,
  ApuTriangleState,
  ApuNoiseState,
  ApuDmcState,
  ExpansionAudioState,
  ExpansionAudioChannel,
  PpuState,
  CpuRegister,
  DebugEvent,
  // Debugger + profiler (breakpoints / stepping / trace / disassembly / call stack / profiling).
  Breakpoint,
  BreakInfo,
  TraceLine,
  ProfiledFunction,
  DisasmLine,
  CallFrame,
} from "../src/backend";

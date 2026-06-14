// retroplug-cli TypeScript test harness — the front door test files import:
//
//   import { test, expect, emu, Button, Mem } from "harness";
//
// Runs inside the embedded txiki/QuickJS runtime (see packages/native/cli/TestHarness.cpp).
// `test()` registers cases; on the runtime's synthetic window 'load' event we
// run each one, turning any thrown error (from expect() or a native emu call)
// into a TAP `not ok`. All emulator control is synchronous — press, advance
// time, read memory, assert, in order.
//
// `emu` and the Button/Mem/Routing enums now live in @retroplug/retroplug
// (createEmu), shared with the end-user CLI; this module just wires it to the
// harness transport and keeps the TAP test runner (beginCase/report/done) — that
// plumbing stays native, resetting the Project and writing TAP, not emu state.

import { createEmu, harnessRpcSend, type RpcSend } from "@retroplug/retroplug";

// Re-export the shared facade surface so existing tests' `from "harness"`
// imports keep working unchanged.
export {
  printProfile, Button, Mem, Routing,
  type Emu,
  type ButtonId, type MemType, type RoutingId,
  type CpuRegisters, type ProfiledFunction, type DisasmLine, type TraceLine,
  type CallFrame, type MidiOutEvent, type SerialOutByte, type KitSample,
  type BreakpointSpec, type BreakInfo, type Frame, type ChordOpts,
} from "@retroplug/retroplug";

// Native runner plumbing on Symbol.for("retroplug") (bound by TestHarness.cpp).
interface Runner {
  beginCase(name: string): void;
  report(name: string, ok: boolean, message: string): void;
  done(): void;
}
const runner = (globalThis as any)[Symbol.for("retroplug")] as Runner;

// Resolve the __rpcSend hook lazily: merely importing this module (the
// `ui-harness` re-exports test/expect from here) must not require the harness
// bridge — only an actual emu.* call does. The UI test runner has no
// retroplug.__rpcSend, but it never touches emu.
let resolvedSend: RpcSend | undefined;
export const emu = createEmu((bytes) => (resolvedSend ??= harnessRpcSend())(bytes));

// -- test() / expect() -------------------------------------------------------

type TestFn = () => void;
const cases: { name: string; fn: TestFn }[] = [];

export function test(name: string, fn: TestFn): void {
  cases.push({ name, fn });
}

function fmt(v: unknown): string {
  if (v instanceof Uint8Array) return `Uint8Array(${v.length})`;
  try { return typeof v === "string" ? `"${v}"` : JSON.stringify(v); }
  catch { return String(v); }
}

function deepEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (a instanceof Uint8Array && b instanceof Uint8Array) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    return a.every((x, i) => deepEqual(x, b[i]));
  }
  return false;
}

export function expect(actual: any) {
  return {
    toBe(expected: any) {
      if (actual !== expected)
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
    },
    toEqual(expected: any) {
      if (!deepEqual(actual, expected))
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
    },
    toBeGreaterThan(n: number) {
      if (!(actual > n)) throw new Error(`expected > ${n}, got ${fmt(actual)}`);
    },
    toBeLessThan(n: number) {
      if (!(actual < n)) throw new Error(`expected < ${n}, got ${fmt(actual)}`);
    },
    toBeTruthy() {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy() {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
  };
}

// -- runner ------------------------------------------------------------------

function runAll(): void {
  for (const c of cases) {
    runner.beginCase(c.name);
    try {
      c.fn();
      runner.report(c.name, true, "");
    } catch (e: any) {
      runner.report(c.name, false, String((e && e.stack) || e));
    }
  }
  runner.done();
}

(globalThis as any).window.addEventListener("load", runAll);

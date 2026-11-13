// The test host's session pump (docs/known-bugs-batch-3-design.md section 7): a throw that nothing caught - in the
// module body, in a microtask job, in a timer made before the first test(), or in a timer while the session is kept
// alive - ends the run at once with the error on stderr and exit 1. Before the pump, the host drove txiki's loop
// without looking for the pending exception or the stop request txiki raises with it, so a module body's throw ended as
// "tests did not complete (tjs.exit never called)" with no reason, and a kept-alive file's throw ran to the 30-minute
// deadline.
//
// Each case is a CHILD process: the throw has to happen before the harness installs its own handlers, and a parent
// cannot read its own stderr. The child is this same bundle (__SELF_BUNDLE__), selected by an environment variable, as
// midi-facet.test.ts runs its renamed-session child.
//
// WHY THE SPAWN RULES ARE HERE (a run once fork-bombed this machine): a child that reaches this file's test()
// registrations spawns children of its own, and each of those spawns more, so one run can leave thousands of hosts
// behind. Three rules, each enforced below:
//   1. a child NEVER registers a case: every test() in this file is inside `if (CHILD === undefined)`, and the child
//      branches end the module body (a throw) or the process (tjs.exit, then an unreachable throw);
//   2. a spawned child REFUSES to spawn: the parent passes RP_SPAWN_DEPTH=1, and spawnChild() throws on sight if it
//      finds it already set;
//   3. every child has a watchdog (SIGTERM after a few seconds), so a host that keeps pumping cannot hold the parent.
//
// The last case is the exit's own: txiki's tjs.exit used to call C exit() from inside the JS call, so main's stack never
// unwound and the launchpad provider's disconnect never wrote the close bytes - a pad a CLI session claimed stayed in
// Programmer mode with its front panel locked (docs/known-bugs-batch-3-design.md section 8).
import { test, expect } from "@retroplug/testing";
import { createLaunchpadClient } from "@retroplug/launchpad/client";
import { exitToLiveMode, PRO_MK3 } from "@retroplug/launchpad";
import { makeCall } from "../src/rpcClient";

// Just enough of txiki's spawn and a child's pipe, declared locally as midi-facet.test.ts declares them.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  exePath: string;
  exit(code: number): void;
  spawn(
    args: string[],
    opts: { env?: Record<string, string | undefined>; stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
    kill(signal: string): void;
  };
};
declare const __SELF_BUNDLE__: string;
declare const __rp_keepAlive: () => void;
// The runtime's globals, declared locally as parity/group.ts declares them: this file bundles for txiki, which has no DOM
// lib in the package's tsconfig.
declare function setTimeout(fn: () => void, ms: number): number;
declare function clearTimeout(id: number): void;
declare function queueMicrotask(fn: () => void): void;

async function drain(stream: ByteStream | null): Promise<string> {
  if (!stream) return "";
  const chunks: Uint8Array[] = [];
  const reader = stream.getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    if (value) chunks.push(value);
  }
  let total = 0;
  for (const c of chunks) total += c.length;
  const all = new Uint8Array(total);
  let at = 0;
  for (const c of chunks) {
    all.set(c, at);
    at += c.length;
  }
  return new TextDecoder().decode(all);
}

/** One child's run: its exit status, its two pipes, and how long it took. A child that does NOT end on its own is
 *  killed at `timeoutMs` and reported as it was found: that is the shape of the bug (a kept-alive session pumped to a
 *  deadline), and a validation run must not wait it out. */
async function child(what: string, timeoutMs = 20000): Promise<{ status: number; out: string; err: string; ms: number }> {
  if (tjs.env.RP_SPAWN_DEPTH) throw new Error("a spawned child may not spawn children (RP_SPAWN_DEPTH is set)");
  const started = Date.now();
  const proc = tjs.spawn([tjs.exePath, __SELF_BUNDLE__], {
    // RETROPLUG_TEARDOWN_TRACE: the host says what its teardown ran (packages/native/src/main.cpp's ExitTrace), which is
    // the only way a parent can see the close bytes a claimed Launchpad got on the way out.
    env: { ...tjs.env, RP_PUMP_CHILD: what, RP_SPAWN_DEPTH: "1", RETROPLUG_TEARDOWN_TRACE: "1" },
    stdout: "pipe",
    stderr: "pipe",
  });
  const watchdog = setTimeout(() => proc.kill("SIGTERM"), timeoutMs);
  const [out, err, status] = await Promise.all([drain(proc.stdout), drain(proc.stderr), proc.wait()]);
  clearTimeout(watchdog);
  return { status: status.exit_status, out, err, ms: Date.now() - started };
}

// ---- the children ---------------------------------------------------------------------------------------------
//
// Selected by RP_PUMP_CHILD and run BEFORE the first test() below, which is the point: a module body's throw and a
// timer created here are what the host used to drop. Each branch either throws out of the module body or ends the
// process, so no case is registered in a child (rule 1 at the top of the file).
const CHILD = tjs.env.RP_PUMP_CHILD;
if (CHILD === "module") {
  // A throw in the module body itself, before any test() exists: what a file's top-level setup does when it fails.
  throw new Error("boom in the module body");
} else if (CHILD === "microtask") {
  // A job of the drain the runtime runs itself: txiki stops the loop and leaves the reason pending.
  queueMicrotask(() => {
    throw new Error("boom in a microtask");
  });
  // The same loop turn's sibling asks for a clean exit, which must NOT hide the error above. A second, not a few
  // milliseconds: an instrumented build's turn is slower than such a timer's deadline, and the timer would then fire
  // inside uv_run BEFORE the job drain - the child exiting 0 with the error never having run, which says nothing about
  // the host. A second is longer than any turn. (While this task is the tree's tip the timer would C-exit(0), which is
  // why it is already a second here: the child must not race the fix to be its own witness.)
  setTimeout(() => tjs.exit(0), 1000);
} else if (CHILD === "timer") {
  // Created before this file's first test(), so the harness's own wrappers are not around it yet.
  setTimeout(() => {
    throw new Error("boom in a timer");
  }, 0);
} else if (CHILD === "kept-alive") {
  __rp_keepAlive();
  setTimeout(() => {
    throw new Error("boom while kept alive");
  }, 0);
  // Only reachable if the throw was dropped: the host would then pump to its 30-minute deadline, and this case holds
  // the child to a few seconds by exiting cleanly (a red run says "exit 0", not "hung").
  setTimeout(() => tjs.exit(0), 8000);
} else if (CHILD === "launchpad") {
  // A session that CLAIMS the scripted Pro MK3 and then exits normally: the provider's teardown is what writes the
  // close bytes (exitToLiveMode), and the host prints them under RETROPLUG_TEARDOWN_TRACE.
  const client = createLaunchpadClient(makeCall());
  const held = client.claim("LPProMK3 MIDI", "LPProMK3 MIDI", Uint8Array.from(exitToLiveMode(PRO_MK3)));
  if (!held.connected) throw new Error(`the scripted Launchpad was not claimed: ${held.error}`);
  tjs.exit(0);
}
if (CHILD !== undefined) {
  // Nothing more to do in a child: its cases below are guarded out, and its end is the throw it scheduled or its own
  // tjs.exit. (This is rule 1's other half: a child never reaches a test() registration.)
}

// ---- the cases -------------------------------------------------------------------------------------------------

if (CHILD === undefined) {
  test("a throw in a module body ends the run with its reason, not with 'tests did not complete'", async () => {
    __rp_keepAlive(); // the child outlasts the host's bounded spin
    const run = await child("module");
    expect(run.status).toBe(1);
    expect(run.err.includes("boom in the module body")).toBe(true);
    expect(run.err.includes("tests did not complete")).toBe(false);
  });

  test("a throw in a microtask job ends the run, even with a sibling timer asking for a clean exit", async () => {
    __rp_keepAlive();
    const run = await child("microtask");
    // The sibling's tjs.exit(0) must not be what the session ends as: the harness never ran, and the job threw.
    expect(run.status).toBe(1);
    expect(run.err.includes("boom in a microtask")).toBe(true);
  });

  test("a throw in a timer made before the first test() ends the run with its reason", async () => {
    __rp_keepAlive();
    const run = await child("timer");
    expect(run.status).toBe(1);
    expect(run.err.includes("boom in a timer")).toBe(true);
    expect(run.err.includes("tests did not complete")).toBe(false);
  });

  test("a kept-alive session that throws ends at once, with its reason", async () => {
    __rp_keepAlive();
    const run = await child("kept-alive", 15000);
    expect(run.status).toBe(1);
    expect(run.err.includes("boom while kept alive")).toBe(true);
    // Not the deadline's 30 minutes, nor the child's own 8-second fallback.
    expect(run.ms < 5000).toBe(true);
  });

  test("a normal exit runs the teardown: a claimed Launchpad gets its close bytes", async () => {
    __rp_keepAlive();
    const run = await child("launchpad", 15000);
    expect(run.status).toBe(0);
    // The close bytes the claim recorded, written by the provider's disconnect on the way out, and the line that says the
    // provider stack stopped: neither ran when tjs.exit ended the process inside the JS call.
    expect(run.err.includes("teardown: launchpad out: F0 00 20 29 02 0E 0E 00 F7")).toBe(true);
    expect(run.err.includes("teardown: providers stopped")).toBe(true);
  });
}

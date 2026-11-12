// Minimal TAP test harness for the package. Runs under txiki.js
// (`tjs run <bundle>`), completely independent of the C++/plugin build — the
// whole point is to develop the TS application logic against a MOCK backend.
//
// A test file imports { test, expect }, registers cases synchronously at module
// load; this harness runs them on a microtask after the module finishes
// evaluating, prints TAP to stdout, and sets the process exit code (nonzero on
// any failure) via `tjs.exit`, so the runner can treat exit code as pass/fail.
//
// A case that cannot run - a missing fixture, an absent host capability - calls
// `skip(reason)` and is reported as `ok N - name # SKIP reason`, NOT as a plain
// pass. That distinction is the whole point: for a long time these suites logged
// "# SKIP" to stdout and then returned, so the case printed `ok` and counted as
// coverage that never ran.

type TestFn = () => void | Promise<void>;

const cases: { name: string; fn: TestFn }[] = [];
let scheduled = false;

// Errors nothing handled - a rejection, or a throw inside a timer callback - recorded instead of left to the runtime.
// Uncancelled, txiki makes one fatal, and a host that ends on it (the CLI) ends the file with no TAP at all, every
// OTHER case lost too; whether a given leak did that or passed unseen depended on whether it was dispatched before or
// after the harness had already exited. Recorded:
//
//   - one that happens while a case runs FAILS THAT CASE AT ONCE: the case is raced against an abort, so a case
//     awaiting work that died first (a background task that threw before signalling) does not wait for ever, which
//     with keepAlive() hung the whole run;
//   - a timer's throw is blamed on the case that CREATED the timer. If that case has already reported, the FILE fails,
//     naming it, rather than whichever innocent case happens to be running;
//   - one outside every case fails the file;
//   - every one is printed to stderr the moment it is recorded, so a file that never reaches its summary (a bounded
//     session that runs out of pumps) still says what went wrong.
//
// A rejection handled LATE - after the event loop turned with it pending - counts: it was unhandled when the runtime
// looked, as it is in Node. All of this is installed by the first test() and not before: a process that registers no
// case (a CLI command, whose dispatcher reaches this module through the SDK) keeps the runtime's own handling, so an
// error there is left fatal and the launcher reports it.
//
// A timer that fires after the file's last loop turn is not seen: the harness has exited. A timer created before the
// first test() is not wrapped. Real timers in a CLI test also need keepAlive() at all: without it the session's bounded
// pumps end before they fire.
interface Stray {
  reason: string;
  during: number; // the case it belongs to, -1 for none
  late: boolean; // it belongs to a case that had already reported
}
const strays: Stray[] = [];
let current = -1;
let abortCase: ((e: unknown) => void) | null = null;
const ABORTED = { rpAbort: true };

/** Text for anything thrown, never itself throwing: a reason that cannot be stringified (an object with no prototype)
 *  must not turn a recorded error back into a fatal one inside the listener. */
function describe(r: unknown): string {
  try {
    return r instanceof Error ? `${r.message}\n${r.stack ?? ""}` : String(r);
  } catch {
    return "(a thrown value that cannot be turned into text)";
  }
}

function record(owner: number, what: string, reason: string): void {
  const late = owner >= 0 && owner !== current;
  strays.push({ reason: `${what}: ${reason}`, during: owner, late });
  const where =
    owner < 0 ? "outside any test" : late ? `from test ${owner + 1}, after it had finished` : `in test ${owner + 1}`;
  try {
    console.error(`# harness: ${what} ${where}: ${reason.split("\n")[0]}`);
  } catch {
    // never let the report itself throw
  }
  if (!late && owner >= 0 && abortCase) abortCase(ABORTED);
}

type TimerFn = (f: unknown, ms?: number, ...a: unknown[]) => unknown;

function installHooks(): void {
  const g = globalThis as unknown as {
    addEventListener?: (t: string, f: (e: { reason: unknown; preventDefault(): void }) => void) => void;
  } & Record<string, TimerFn | undefined>;
  g.addEventListener?.("unhandledrejection", (ev) => {
    ev.preventDefault();
    // A rejection carries no trace of who created it, so it belongs to the case running now, which may be the one
    // AFTER the culprit; its message says so.
    record(current, "unhandled rejection (during, or left by a test before, this one)", describe(ev.reason));
  });
  // A throw inside a timer callback is not a rejection, and txiki gives JS no event for it: it is fatal natively. Wrap
  // the timers: the callback runs in a try, and the case that created the timer is captured when it is created.
  // clearTimeout / clearInterval keep working because the original's handle is returned.
  for (const name of ["setTimeout", "setInterval"] as const) {
    const orig = g[name];
    if (!orig) continue;
    g[name] = (f: unknown, ms?: number, ...a: unknown[]) => {
      const owner = current;
      return orig(
        typeof f !== "function"
          ? f
          : (...args: unknown[]) => {
              try {
                (f as (...x: unknown[]) => void)(...args);
              } catch (e) {
                record(owner, "uncaught error in a timer", describe(e));
              }
            },
        ms,
        ...a,
      );
    };
  }
}

/** One turn of the event loop: long enough for the runtime to dispatch rejections left pending. */
const turn = (): Promise<void> =>
  new Promise((r) => (globalThis as unknown as { setTimeout(f: () => void, ms: number): unknown }).setTimeout(r, 0));

/** How many cases this process has registered. The CLI's `runSession` refuses to run when one has: a session file
 *  run by path registers none, and a test file calling it would exit the process before the harness reported, which
 *  the runner reads as a pass. */
export function registeredCases(): number {
  return cases.length;
}

export function test(name: string, fn: TestFn): void {
  cases.push({ name, fn });
  // Schedule the run once, after the current synchronous module body (all the
  // top-level test() calls) has finished registering.
  if (!scheduled) {
    scheduled = true;
    installHooks();
    Promise.resolve().then(runAll);
  }
}

/** A case abandoned because it could not run, as opposed to one that failed.
 *
 *  Branded with a plain string own-property rather than a subclass, and matched structurally: this
 *  harness is bundled separately into every test file by esbuild AND embedded as QuickJS bytecode in
 *  the retroplug-cli binary, so `instanceof` would be comparing constructors from different copies of
 *  the module. A property check holds across all of them. */
type SkipError = Error & { rpSkip: true; rpSkipReason: string };

function isSkip(e: unknown): e is SkipError {
  return typeof e === "object" && e !== null && (e as { rpSkip?: unknown }).rpSkip === true;
}

/** Abandon the current case as SKIPPED rather than passed, because a precondition it cannot control is
 *  absent (a ROM fixture that is not checked out, a counter only a profile host exposes).
 *
 *  It THROWS, which is what makes it usable from a helper or a loop: several files delegate an entire
 *  case to a shared `bootsAndRenders(...)`, and a sentinel return value there would end the helper
 *  while the case carried on to its assertions. `return skip(...)` also type-checks anywhere, since
 *  `never` is assignable to every return type.
 *
 *  One limit: any `catch` between the call and the harness swallows it - which is exactly why `toThrow`
 *  below re-throws it - so call it only from code the test body runs or awaits. */
export function skip(reason: string): never {
  const e = new Error(`SKIP: ${reason}`) as SkipError;
  e.rpSkip = true;
  e.rpSkipReason = reason;
  throw e;
}

function fmt(v: unknown): string {
  if (typeof v === "number") return String(v); // JSON would print NaN / Infinity as null
  if (v instanceof Uint8Array) {
    const head = Array.from(v.slice(0, 8)).join(",");
    return `Uint8Array(${v.length})[${head}${v.length > 8 ? ",…" : ""}]`;
  }
  try {
    return typeof v === "string" ? JSON.stringify(v) : (JSON.stringify(v) ?? String(v));
  } catch {
    return String(v);
  }
}

/** Fluent assertions. Every failure names BOTH values (an inequality reports the actual number, not just
 *  "expected truthy"), and an optional `message` is prefixed to the failure so a case with several checks
 *  says which one fired: `expect(hz, "pulse1 pitch").toBeCloseTo(440, 1)`. */
export function expect(actual: unknown, message?: string) {
  const fail = (detail: string): never => {
    throw new Error(message ? `${message}: ${detail}` : detail);
  };
  const num = (what: string): number => {
    if (typeof actual !== "number") fail(`${what} expects a number, got ${fmt(actual)}`);
    return actual as number;
  };
  return {
    toBe(expected: unknown): void {
      if (!Object.is(actual, expected)) fail(`expected ${fmt(expected)}, got ${fmt(actual)}`);
    },
    toEqual(expected: unknown): void {
      const a = JSON.stringify(actual);
      const b = JSON.stringify(expected);
      if (a !== b) fail(`expected ${b}, got ${a}`);
    },
    toBeTruthy(): void {
      if (!actual) fail(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy(): void {
      if (actual) fail(`expected falsy, got ${fmt(actual)}`);
    },
    toBeGreaterThan(bound: number): void {
      if (!(num("toBeGreaterThan") > bound)) fail(`expected > ${fmt(bound)}, got ${fmt(actual)}`);
    },
    toBeGreaterThanOrEqual(bound: number): void {
      if (!(num("toBeGreaterThanOrEqual") >= bound)) fail(`expected >= ${fmt(bound)}, got ${fmt(actual)}`);
    },
    toBeLessThan(bound: number): void {
      if (!(num("toBeLessThan") < bound)) fail(`expected < ${fmt(bound)}, got ${fmt(actual)}`);
    },
    toBeLessThanOrEqual(bound: number): void {
      if (!(num("toBeLessThanOrEqual") <= bound)) fail(`expected <= ${fmt(bound)}, got ${fmt(actual)}`);
    },
    /** |actual - expected| <= tolerance (an absolute tolerance, default 1e-9 - NOT jest's digits). */
    toBeCloseTo(expected: number, tolerance = 1e-9): void {
      const a = num("toBeCloseTo");
      const diff = Math.abs(a - expected);
      if (!(diff <= tolerance))
        fail(`expected ${fmt(expected)} +/- ${fmt(tolerance)}, got ${fmt(actual)} (off by ${fmt(diff)})`);
    },
    toThrow(match?: string | RegExp): void {
      if (typeof actual !== "function") fail("toThrow expects a function");
      let threw: unknown;
      let didThrow = false;
      try {
        (actual as () => unknown)();
      } catch (e) {
        // A skip is not "the function threw": swallowing it here would turn an un-runnable case into a
        // green assertion, which is the very vacuous pass this directive exists to remove.
        if (isSkip(e)) throw e;
        didThrow = true;
        threw = e;
      }
      if (!didThrow) fail("expected function to throw, but it did not");
      if (match !== undefined) {
        const msg = threw instanceof Error ? threw.message : String(threw);
        const ok = typeof match === "string" ? msg.includes(match) : match.test(msg);
        if (!ok) fail(`expected throw matching ${fmt(match)}, got ${fmt(msg)}`);
      }
    },
  };
}

function exit(code: number): void {
  const g = globalThis as { tjs?: { exit(n: number): void } };
  g.tjs?.exit(code);
}

async function runAll(): Promise<void> {
  // One turn FIRST, so a rejection left by the module body itself (a throw after the test() calls) is recorded as
  // outside every case rather than blamed on the first one.
  await turn();
  const out: string[] = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  let skipped = 0;
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    current = i;
    let thrown: unknown = undefined;
    let threw = false;
    try {
      const aborted = new Promise<never>((_, reject) => (abortCase = reject));
      await Promise.race([Promise.resolve().then(fn), aborted]);
      await turn(); // let what the case left pending be dispatched while it still counts as this case's
    } catch (e) {
      thrown = e;
      threw = true;
    }
    abortCase = null;
    const mine = strays.filter((s) => s.during === i && !s.late);
    // A skip is a skip only if nothing ELSE went wrong in the case. The skip check comes before any String(e): a
    // non-Error would otherwise be formatted as a failure.
    if (threw && isSkip(thrown) && mine.length === 0) {
      skipped++;
      // TAP13's real directive. Uppercase to match Catch2's own TAP reporter, and because it keeps the output greppable
      // by the same literal these suites have always logged.
      out.push(`ok ${i + 1} - ${name} # SKIP ${thrown.rpSkipReason}`);
      continue;
    }
    if (!threw && mine.length === 0) {
      out.push(`ok ${i + 1} - ${name}`);
      continue;
    }
    failed++;
    const parts: string[] = [];
    // QuickJS's Error.stack doesn't prefix the message, so lead with it. The abort itself is not news.
    if (threw && thrown !== ABORTED && !isSkip(thrown)) parts.push(describe(thrown));
    for (const s of mine) parts.push(s.reason);
    out.push(`not ok ${i + 1} - ${name}`, "  ---");
    for (const line of parts.join("\n").split("\n")) out.push(`  ${line}`);
    out.push("  ...");
  }
  current = -1;
  await turn();
  // What belongs to no case that could still report - outside every case, or a case that had already reported - fails
  // the FILE. Listed once each: one throw can surface as more than one rejection (a module body's, and the import
  // chain's).
  const fileLevel = [...new Set(strays.filter((s) => s.during < 0 || s.late).map((s) => s.reason))];
  if (fileLevel.length) {
    out.push("# FAIL: uncaught errors that no test could report:");
    for (const r of fileLevel) for (const line of r.split("\n")) out.push(`#   ${line}`);
  }
  // For a human reading one file's block. The runner derives its own counts from the result lines above rather than
  // parsing this, so there is one source of truth and this can never disagree.
  out.push(
    `# ${cases.length - failed - skipped} passed, ${skipped} skipped, ${failed} failed` +
      (fileLevel.length ? `, and the file failed (${fileLevel.length} error(s) outside the tests)` : ""),
  );
  console.log(out.join("\n"));
  // A skip is not a failure: the exit code still answers "did anything break?".
  exit(failed || fileLevel.length ? 1 : 0);
}

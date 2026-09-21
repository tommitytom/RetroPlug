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

export function test(name: string, fn: TestFn): void {
  cases.push({ name, fn });
  // Schedule the run once, after the current synchronous module body (all the
  // top-level test() calls) has finished registering.
  if (!scheduled) {
    scheduled = true;
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
 *  Two limits. Call it only from a test body: an exception raised inside a DSP kernel callback is
 *  flattened to a boolean by the native runtime and would vanish. And any `catch` between the call and
 *  the harness swallows it - which is exactly why `toThrow` below re-throws it. */
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
  const out: string[] = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  let skipped = 0;
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    try {
      await fn();
      out.push(`ok ${i + 1} - ${name}`);
    } catch (e) {
      // The skip check comes FIRST: below, a non-Error would fall through to String(e) and be
      // formatted as a failure.
      if (isSkip(e)) {
        skipped++;
        // TAP13's real directive. Uppercase to match Catch2's own TAP reporter, and because it keeps
        // the output greppable by the same literal these suites have always logged.
        out.push(`ok ${i + 1} - ${name} # SKIP ${e.rpSkipReason}`);
        continue;
      }
      failed++;
      // QuickJS's Error.stack doesn't prefix the message, so lead with it.
      const detail = e instanceof Error ? `${e.message}\n${e.stack ?? ""}` : String(e);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  // For a human reading one file's block. The runner derives its own counts from the result lines
  // above rather than parsing this, so there is one source of truth and this can never disagree.
  out.push(`# ${cases.length - failed - skipped} passed, ${skipped} skipped, ${failed} failed`);
  console.log(out.join("\n"));
  // A skip is not a failure: the exit code still answers "did anything break?".
  exit(failed ? 1 : 0);
}

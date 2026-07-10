// Minimal TAP test harness for the greenfield package. Runs under txiki.js
// (`tjs run <bundle>`), completely independent of the C++/plugin build — the
// whole point is to develop the TS application logic against a MOCK backend.
//
// A test file imports { test, expect }, registers cases synchronously at module
// load; this harness runs them on a microtask after the module finishes
// evaluating, prints TAP to stdout, and sets the process exit code (nonzero on
// any failure) via `tjs.exit`, so the runner can treat exit code as pass/fail.

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

function fmt(v: unknown): string {
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

export function expect(actual: unknown) {
  return {
    toBe(expected: unknown): void {
      if (!Object.is(actual, expected)) {
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
      }
    },
    toEqual(expected: unknown): void {
      const a = JSON.stringify(actual);
      const b = JSON.stringify(expected);
      if (a !== b) throw new Error(`expected ${b}, got ${a}`);
    },
    toBeTruthy(): void {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy(): void {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
    toThrow(match?: string | RegExp): void {
      if (typeof actual !== "function") throw new Error("toThrow expects a function");
      let threw: unknown;
      let didThrow = false;
      try {
        (actual as () => unknown)();
      } catch (e) {
        didThrow = true;
        threw = e;
      }
      if (!didThrow) throw new Error("expected function to throw, but it did not");
      if (match !== undefined) {
        const msg = threw instanceof Error ? threw.message : String(threw);
        const ok = typeof match === "string" ? msg.includes(match) : match.test(msg);
        if (!ok) throw new Error(`expected throw matching ${fmt(match)}, got ${fmt(msg)}`);
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
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    try {
      await fn();
      out.push(`ok ${i + 1} - ${name}`);
    } catch (e) {
      failed++;
      // QuickJS's Error.stack doesn't prefix the message, so lead with it.
      const detail = e instanceof Error ? `${e.message}\n${e.stack ?? ""}` : String(e);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  console.log(out.join("\n"));
  exit(failed ? 1 : 0);
}

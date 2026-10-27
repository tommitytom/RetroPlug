// Shared console capture for tests that check what the code under test warns about (a store's load repairs, say).
// Not a *.test.ts, so the runner ignores it.

/** Run `fn`, capturing every console.warn it makes instead of printing it: its result, and the warnings, each as one
 *  line. The console is put back however `fn` ends. */
export function warned<T>(fn: () => T): { result: T; warnings: string[] } {
  const warnings: string[] = [];
  const warn = console.warn;
  console.warn = (...args: unknown[]) => warnings.push(args.map(String).join(" "));
  try {
    return { result: fn(), warnings };
  } finally {
    console.warn = warn;
  }
}

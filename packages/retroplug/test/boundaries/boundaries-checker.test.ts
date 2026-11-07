// The boundary check over its fixture packages (packages/retroplug/scripts/check-boundaries.mjs, docs/modular-graph-
// phase-f1-design.md section 4): one fixture tree per refusal, each breaking exactly one rule, and the check fails it
// with that rule's message, naming the file, the import and the owner:
//
// - an @retroplug import the package does not declare;
// - a relative reach into the shell;
// - a third-party module (zod) the package does not declare;
// - a cycle in the declared dependencies;
// - an #include that climbs out of the package;
// - a dependency on the shell, declared and imported (the two halves of one rule);
// - an import nothing resolves, which is reported by the file and the specifier rather than as a bundler failure, and
//   does not hide the rest of the package's refusals;
// - a type-only reach into the shell (`import type`, `export type ... from`, `import("x").T`);
// - a devDependency a source file imports, which only a test file may;
// - a declared package that does not exist;
//
// and two positive trees: one where a package declares a package it imports by name, a declared third-party subpath, a
// devDependency from a test file and its own interface by a relative path, and the real tree, where the imports the
// check knows about are the EXCEPTIONS list's: the ones stage 3 removes.
//
// The checker is Node's (it reads TypeScript's scanner), so the test spawns test/boundaries/run-checker.mjs, which runs
// it over a fixture root and gives back its output as JSON, as test/packages/packages-generator.test.ts runs the
// generator.
import { test, expect } from "../../testing/harness";

// Just enough of txiki's spawn, a child's pipe and its file reads, declared locally as bundle-no-zod.test.ts does.
interface ByteStream {
  getReader(): { read(): Promise<{ done: boolean; value?: Uint8Array }> };
}
declare const tjs: {
  env: Record<string, string | undefined>;
  spawn(
    args: string[],
    opts: { stdout: "pipe"; stderr: "pipe" },
  ): {
    stdout: ByteStream | null;
    stderr: ByteStream | null;
    wait(): Promise<{ exit_status: number; term_signal: string | null }>;
  };
};

async function text(stream: ByteStream | null): Promise<string> {
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

/** The checker's report for one tree, parsed. */
async function drive(which: string): Promise<Record<string, any>> {
  const child = tjs.spawn([tjs.env.RP_NODE || "node", "test/boundaries/run-checker.mjs", which], {
    stdout: "pipe",
    stderr: "pipe",
  });
  // Both pipes drained beside the wait, so a child that fills one cannot block on it.
  const [out, err, status] = await Promise.all([text(child.stdout), text(child.stderr), child.wait()]);
  if (status.term_signal || status.exit_status !== 0) throw new Error(`the checker driver failed: ${err || out}`);
  return JSON.parse(out) as Record<string, any>;
}

test("an @retroplug import a package does not declare is refused, naming the file, the import and the owner", async () => {
  const report = await drive("undeclared-package");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "@retroplug/beta", which is the beta package (packages/beta), and alpha ' +
      "does not declare it in its dependencies (its tests may also use devDependencies)",
  ]);
});

test("a relative reach into the shell is refused, naming the file, the import and the shell", async () => {
  const report = await drive("relative-reach");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/gamma/ts/index.ts: imports "../../retroplug/src/shell", which resolves into the shell ' +
      "(packages/retroplug), outside packages/gamma/ts: a package reaches another as @retroplug/<name>, and the shell " +
      "not at all",
  ]);
});

test("a third-party module a package does not declare is refused", async () => {
  const report = await drive("undeclared-third-party");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/delta/ts/index.ts: imports "zod", a third-party module, which delta does not declare in its ' +
      "dependencies (its tests may also use devDependencies)",
  ]);
});

test("a cycle in the declared dependencies is refused, naming the path", async () => {
  const report = await drive("cycle");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual(["the packages' declared dependencies are a cycle: epsilon -> zeta -> epsilon"]);
});

test("an #include that climbs out of the package is refused, naming the file, the include and the owner", async () => {
  const report = await drive("climbing-include");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/theta/native/src/x.cpp: includes "../../../other/native/src/y.hpp", which resolves into the other ' +
      "package (packages/other), outside packages/theta: a package's native sources include their own by path",
  ]);
});

test("a dependency on the shell is refused, declared in the manifest and imported alike", async () => {
  const report = await drive("shell-dependency");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/iota/package.json: declares @retroplug/retroplug in its dependencies, which is the shell " +
      "(packages/retroplug): no package may depend on the shell",
    'packages/iota/ts/index.ts: imports "@retroplug/retroplug", which is the shell (packages/retroplug): no package ' +
      "may depend on the shell",
  ]);
});

test("an import nothing resolves is reported by its file and specifier, and the package is still judged", async () => {
  const report = await drive("unresolvable");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/kappa/ts/index.ts:3:22: cannot be resolved: ./nope names no file',
    'packages/kappa/ts/other.ts: imports "zod", a third-party module, which kappa does not declare in its ' +
      "dependencies (its tests may also use devDependencies)",
  ]);
});

test("a package that declares everything it reaches holds, its own interface and a third-party subpath included", async () => {
  const report = await drive("allowed");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.warnings).toEqual([]);
  expect(report.stale).toEqual([]);
});

test("type-only imports are judged: an import type, an export type ... from and an import(\"x\").T alike", async () => {
  const report = await drive("type-only-reach");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/a", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>, and the shell " +
      "not at all",
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/b", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>, and the shell " +
      "not at all",
    'packages/alpha/ts/index.ts: imports "../../retroplug/src/c", which resolves into the shell ' +
      "(packages/retroplug), outside packages/alpha/ts: a package reaches another as @retroplug/<name>, and the shell " +
      "not at all",
  ]);
});

test("a devDependency is a test file's: a source file that imports one is refused", async () => {
  const report = await drive("dev-dependency-source");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    'packages/alpha/ts/index.ts: imports "@retroplug/testing", which is the testing package (packages/testing), and ' +
      "alpha does not declare it in its dependencies (its tests may also use devDependencies)",
  ]);
});

test("a declared package that does not exist is refused on its own", async () => {
  const report = await drive("declared-missing");
  console.log(`# ${(report.refused as string[]).join("\n# ")}`);
  expect(report.status).toBe(1);
  expect(report.refused).toEqual([
    "packages/alpha/package.json: declares @retroplug/ghost, which is not a package in packages",
  ]);
});

test("the real tree holds its boundaries through the EXCEPTIONS stage 3 removes", async () => {
  const report = await drive("--tree");
  const allowed = report.allowed as string[];
  console.log(`# ${allowed.length} allowed by EXCEPTIONS:`);
  for (const line of allowed) console.log(`#   ${line}`);
  expect(report.status).toBe(0);
  expect(report.refused).toEqual([]);
  expect(report.stale).toEqual([]);
  // Every entry is the shell reached by a relative path from a package's TS: core's half (bliptoaster, devtest) and the
  // test harness (n8's tests).
  expect(allowed.length).toBe(12);
  expect(allowed.every((line) => line.includes("resolves into the shell (packages/retroplug), outside"))).toBe(true);
  expect(allowed.filter((line) => line.includes("stage 3 (core's TS half)")).length).toBe(5);
  expect(allowed.filter((line) => line.includes("stage 3 (@retroplug/testing)")).length).toBe(7);
});

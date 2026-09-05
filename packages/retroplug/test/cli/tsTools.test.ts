// The pure parts of `retroplug-cli test` / `run`: argument parsing, file selection, and - the one with
// real teeth - the source-dir -> build-dir mapping.
//
// buildDirFor is worth guarding carefully because it is what makes stripped tests work at all. Stripping
// preserves an import specifier verbatim, so the emitted `.js` MUST sit at the same directory depth as
// the `.ts` it came from, or a sibling `./helper.js` resolves somewhere else. The "." vs ".." case below
// is not hypothetical: getting it wrong made every test fail to resolve its imports.
// (The SDK is exempt - it is a bare specifier served from the module registry, never a path.)
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { buildDirFor, isStrippableTs, outputName, buildTsDir, resolveBuildDir } from "../../cli/tsStrip";
import { sdkDirFor } from "../../cli/sdkAssets";
import { parseTestArgs, selectTests } from "../../cli/sessions/test";
import { parseRunArgs, splitPath } from "../../cli/sessions/run";

test("tsStrip: buildDirFor puts the build dir alongside the source dir, at the same depth", () => {
  // A bare name is a child of the working directory, so its parent is "." - ".." would place the build
  // dir one level too high and break every relative import out of it.
  expect(buildDirFor("tests")).toBe("./.rp-test-build");
  expect(buildDirFor("kit/tests")).toBe("kit/.rp-test-build");
  expect(buildDirFor("/abs/kit/tests")).toBe("/abs/kit/.rp-test-build");
  expect(buildDirFor("kit/tests/")).toBe("kit/.rp-test-build"); // trailing slash tolerated
});

test("sdkAssets: sdkDirFor sits alongside the SOURCE tests", () => {
  // Only the .d.ts is written, and only editors/tsc read it - nothing resolves it at run time (the SDK
  // itself is a bare specifier served from the module registry), so it keys off the source dir.
  expect(sdkDirFor("kit/tests")).toBe("kit/sdk");
  expect(sdkDirFor("/abs/kit/tests")).toBe("/abs/kit/sdk");
  expect(sdkDirFor("tests")).toBe("./sdk"); // bare name: same "." rule as buildDirFor
});

test("tsStrip: only non-declaration .ts is strippable, and .ts maps to .js", () => {
  expect(isStrippableTs("a.test.ts")).toBe(true);
  expect(isStrippableTs("a.d.ts")).toBe(false); // types only, nothing to emit
  expect(isStrippableTs("a.js")).toBe(false);
  expect(outputName("a.test.ts")).toBe("a.test.js");
  expect(outputName("helper.js")).toBe("helper.js"); // copied through unchanged
});

test("tsStrip: buildTsDir strips .ts, copies .js, and skips .d.ts and subdirectories", () => {
  const backend = new MockBackend();
  backend.writeFile("/kit/tests/a.test.ts", new TextEncoder().encode("const x: number = 1;"));
  backend.writeFile("/kit/tests/helper.js", new TextEncoder().encode("export const h = 1;"));
  backend.writeFile("/kit/tests/types.d.ts", new TextEncoder().encode("export declare const t: number;"));

  // Stand in for the compiled-in stripper (a pure-TS test has no native hook). Returning a marker lets
  // us prove the .ts went through it and the .js did not.
  const g = globalThis as { __stripTypes?: (s: string, f?: string) => string };
  const prev = g.__stripTypes;
  g.__stripTypes = (src) => `/*stripped*/${src}`;
  try {
    const { emitted, outDir, needsSdk } = buildTsDir(backend, "/kit/tests");
    expect(outDir).toBe("/kit/.rp-test-build");
    expect(emitted.sort().join(",")).toBe("a.test.js,helper.js"); // no types.d.ts
    // Nothing here imports the SDK, so none should be written next to it.
    expect(needsSdk).toBe(false);

    const dec = new TextDecoder();
    expect(dec.decode(backend.readFile("/kit/.rp-test-build/a.test.js")!)).toBe("/*stripped*/const x: number = 1;");
    expect(dec.decode(backend.readFile("/kit/.rp-test-build/helper.js")!)).toBe("export const h = 1;");
  } finally {
    g.__stripTypes = prev;
  }
});

test("tsStrip: buildTsDir reports needsSdk from the BARE specifier", () => {
  const backend = new MockBackend();
  backend.writeFile(
    "/kit/tests/a.test.ts",
    new TextEncoder().encode(`import { test } from "retroplug-cli";`),
  );
  const g = globalThis as { __stripTypes?: (s: string, f?: string) => string };
  const prev = g.__stripTypes;
  g.__stripTypes = (src) => src;
  try {
    expect(buildTsDir(backend, "/kit/tests").needsSdk).toBe(true);
  } finally {
    g.__stripTypes = prev;
  }
});

test("test: parseTestArgs takes dir + filter positionally and options anywhere", () => {
  const a = parseTestArgs(["tests"]);
  expect(a.dir).toBe("tests");
  expect(a.filter).toBe("");
  expect(a.rom).toBe(null);

  const b = parseTestArgs(["tests", "pulse", "--rom", "r.nes", "--out", "/tmp/o"]);
  expect(b.dir).toBe("tests");
  expect(b.filter).toBe("pulse");
  expect(b.rom).toBe("r.nes");
  expect(b.out).toBe("/tmp/o");

  // Options before the positionals, and `--` splitting off extra session args.
  const c = parseTestArgs(["--rom", "r.nes", "tests", "--", "-v", "--trace"]);
  expect(c.dir).toBe("tests");
  expect(c.rom).toBe("r.nes");
  expect(c.passthrough.join(" ")).toBe("-v --trace");
});

test("test: selectTests picks *.test.js only, applies the filter, and sorts", () => {
  const emitted = ["z.test.js", "helper.js", "a.test.js", "notes.js", "m.test.js"];
  expect(selectTests(emitted, "").join(",")).toBe("a.test.js,m.test.js,z.test.js");
  expect(selectTests(emitted, "m.").join(",")).toBe("m.test.js");
  expect(selectTests(emitted, "nope").length).toBe(0);
});

test("run: parseRunArgs keeps the session path first and forwards the rest verbatim", () => {
  const a = parseRunArgs(["repro/probe.ts", "rom.nes", "--flag"]);
  expect(a.session).toBe("repro/probe.ts");
  expect(a.sessionArgs.join(" ")).toBe("rom.nes --flag");

  // --out is ours only BEFORE the session path; after it, it belongs to the session.
  const b = parseRunArgs(["--out", "/tmp/o", "p.ts", "--out", "theirs"]);
  expect(b.out).toBe("/tmp/o");
  expect(b.session).toBe("p.ts");
  expect(b.sessionArgs.join(" ")).toBe("--out theirs");
});

test("run: splitPath treats a bare name as living in the current directory", () => {
  expect(splitPath("a/b/c.ts").dir).toBe("a/b");
  expect(splitPath("a/b/c.ts").file).toBe("c.ts");
  expect(splitPath("c.ts").dir).toBe(".");
  expect(splitPath("c.ts").file).toBe("c.ts");
});

// resolveBuildDir: the sibling build dir when it is writable; a temp-dir fallback when it is not (a session
// straight under /tmp maps to /.rp-test-build, which nobody can write); --out verbatim, never probed.
function backendRefusing(prefix: string): MockBackend & { writes: string[] } {
  const b = new MockBackend() as MockBackend & { writes: string[] };
  b.writes = [];
  const orig = b.writeFile.bind(b);
  b.writeFile = (path: string, bytes: Uint8Array) => {
    b.writes.push(path);
    return path.startsWith(prefix) ? false : orig(path, bytes);
  };
  return b;
}

test("resolveBuildDir keeps the writable sibling and removes its probe marker", () => {
  const b = backendRefusing("/nowhere");
  const r = resolveBuildDir(b, "kit/tests", null, "/tmp");
  expect(r).toEqual({ outDir: "kit/.rp-test-build", fellBack: false });
  expect(b.writes).toEqual(["kit/.rp-test-build/.rp-writable"]);
  expect(b.fileExists("kit/.rp-test-build/.rp-writable")).toBe(false); // the probe leaves nothing behind
});

test("resolveBuildDir falls back under the temp dir when the sibling cannot be written", () => {
  const b = backendRefusing("/.rp-test-build");
  const r = resolveBuildDir(b, "/tmp", null, "/var/tmp/");
  expect(r.fellBack).toBe(true);
  expect(r.outDir).toBe("/var/tmp/rp-test-build/tmp"); // keyed on the source path, trailing slash trimmed
  // A different source dir with the same unwritable sibling gets its own fallback, so two sessions never
  // share stripped output.
  expect(resolveBuildDir(b, "/probes", null, "/var/tmp").outDir).toBe("/var/tmp/rp-test-build/probes");
  // ...while one whose sibling IS writable (/tmp/probes -> /tmp/.rp-test-build) stays put.
  expect(resolveBuildDir(b, "/tmp/probes", null, "/var/tmp")).toEqual({ outDir: "/tmp/.rp-test-build", fellBack: false });
});

test("resolveBuildDir honours --out without probing anything", () => {
  const b = backendRefusing("/");
  expect(resolveBuildDir(b, "/tmp", "/somewhere/out", "/var/tmp")).toEqual({ outDir: "/somewhere/out", fellBack: false });
  expect(b.writes).toEqual([]);
});

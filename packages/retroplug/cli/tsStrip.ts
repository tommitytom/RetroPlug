// The TypeScript seam for `retroplug-cli test` / `run`: strip type annotations so the binary can execute
// `.ts` directly, with no Node, npm or bundler on the consumer's machine.
//
// The stripper itself (ts-blank-space + the TypeScript parser) is compiled into the binary as global-code
// bytecode and loaded ON DEMAND through the native __rp_loadTsStripper hook (packages/native/cli/main.cpp),
// so the ~4 MB never touches the startup path of `render` / `n8-bridge` / any other command.
//
// Stripping replaces types with WHITESPACE rather than re-emitting code, so byte offsets and line/column
// numbers survive: a stack trace from a stripped file points at the right line of the original `.ts`, with
// no source map. It also means the output is always the same length as the input, which the tests assert.

import type { HostBackend } from "../src/backend";

type StripFn = (source: string, filename?: string) => string;

let stripper: StripFn | null = null;

/** Install the compiled-in stripper (once) and return it. Throws if the host bound no hook - i.e. this is
 *  a build without the stripper bundle, or a host other than retroplug-cli. */
function loadStripper(): StripFn {
  if (stripper) return stripper;
  const g = globalThis as { __rp_loadTsStripper?: () => void; __stripTypes?: StripFn };
  if (typeof g.__stripTypes !== "function") {
    if (typeof g.__rp_loadTsStripper !== "function")
      throw new Error("no TypeScript stripper in this build (__rp_loadTsStripper is missing)");
    g.__rp_loadTsStripper();
  }
  if (typeof g.__stripTypes !== "function")
    throw new Error("the TypeScript stripper failed to install __stripTypes");
  stripper = g.__stripTypes;
  return stripper;
}

/** Strip `source`. Throws with `file:line:col` on syntax that cannot be erased (enum / namespace /
 *  constructor parameter properties all emit runtime code). */
export function stripTs(source: string, filename?: string): string {
  return loadStripper()(source, filename);
}

/** `dir`'s sibling build directory - where stripped output goes. Keeping the same directory DEPTH is what
 *  lets a test's own import specifiers resolve unchanged: from `<kit>/.rp-test-build/x.js`,
 *  `../sdk/retroplug-cli.js` still reaches `<kit>/sdk/` and `./helper.js` stays local. Rewriting
 *  specifiers would defeat the whole point of a position-preserving strip. */
export function buildDirFor(dir: string): string {
  const clean = dir.replace(/\/+$/, "");
  const slash = clean.lastIndexOf("/");
  // No slash means `dir` is a direct child of the working directory, so its parent is "." - NOT "..",
  // which would put the build dir one level too high and break every `../sdk/...` import.
  return (slash < 0 ? "." : clean.slice(0, slash)) + "/.rp-test-build";
}

/** A `.ts` source file that should be stripped: not a `.d.ts` (types only, nothing to emit). */
export function isStrippableTs(name: string): boolean {
  return name.endsWith(".ts") && !name.endsWith(".d.ts");
}

/** The emitted name for a source file: `x.test.ts` -> `x.test.js`. Plain `.js` files are copied through
 *  unchanged, so a directory can mix both. */
export function outputName(name: string): string {
  return isStrippableTs(name) ? name.slice(0, -3) + ".js" : name;
}

export interface BuildResult {
  /** Emitted file names (basenames), in directory order. */
  emitted: string[];
  /** The directory they were written to. */
  outDir: string;
}

/**
 * Strip every `.ts` in `srcDir` into `outDir` (default: `buildDirFor(srcDir)`), copying any plain `.js`
 * across too. Whole-directory rather than following imports: predictable, and a test that imports a
 * sibling helper just works without resolving the module graph ourselves.
 */
export function buildTsDir(backend: HostBackend, srcDir: string, outDir?: string): BuildResult {
  const dir = srcDir.replace(/\/+$/, "");
  const out = outDir ?? buildDirFor(dir);
  const emitted: string[] = [];

  for (const entry of backend.listDir(dir)) {
    if (entry.endsWith("/")) continue; // listDir marks directories with a trailing slash
    if (!isStrippableTs(entry) && !entry.endsWith(".js")) continue;

    const srcPath = `${dir}/${entry}`;
    const bytes = backend.readFile(srcPath);
    if (!bytes) throw new Error(`could not read ${srcPath}`);

    const source = new TextDecoder().decode(bytes);
    const code = isStrippableTs(entry) ? stripTs(source, srcPath) : source;

    const outName = outputName(entry);
    // writeFile creates parent dirs on demand, so outDir needs no mkdir.
    if (!backend.writeFile(`${out}/${outName}`, new TextEncoder().encode(code)))
      throw new Error(`could not write ${out}/${outName}`);
    emitted.push(outName);
  }

  return { emitted, outDir: out };
}

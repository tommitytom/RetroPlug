// Build the TypeScript type stripper that gets COMPILED INTO retroplug-cli, so the binary can run
// `.ts` test files with no Node, no npm and no bundler on the consumer's machine. Mirrors
// tools/build-session.js / build-cli-sdk.mjs, but emits GLOBAL CODE (an IIFE, not an ES module):
// cli/main.cpp loads it with JS_ReadObject + JS_EvalFunction on demand, the same way RenderHost loads
// the render worker.
//
//   node tools/build-ts-stripper.mjs [outFile]     (default: build/native/ts-stripper-bundle.js)
//
// Defines exactly one global:
//
//   globalThis.__stripTypes(source, filename) -> javascript      (throws on non-erasable syntax)
//
// WHY ts-blank-space: it replaces type annotations with WHITESPACE rather than re-emitting code, so
// byte offsets and line/column numbers survive untouched and no source map is needed for a stack
// trace to point at the original .ts. It is ~700 lines because it delegates parsing to TypeScript's
// own parser, which is also why this bundle is ~3.4 MB: almost all of it is that parser.
//
// (The obvious alternative, @swc/wasm-typescript - what Node uses via amaro - was tried first and
// rejected: under the txiki build's WAMR interpreter it core-dumped on 3 of 7 real test files and
// SILENTLY returned empty output on 2 more. An empty test file reports "1..0" and passes.)
import { buildSync } from "esbuild";
import { mkdirSync, writeFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const outFile = resolve(process.argv[2] ?? resolve(REPO, "build/native/ts-stripper-bundle.js"));

mkdirSync(dirname(outFile), { recursive: true });

// TypeScript's package references these four node builtins from its system/host layer. ts-blank-space
// only ever reaches the PARSER, so they are never called; stub them so the bundle stays runtime-free.
const stub = resolve(REPO, "build/native/ts-stripper-stub.js");
writeFileSync(stub, "export default {};\n");

// The entry, written inline so the wrapper and its one critical detail live next to the explanation.
//
// onError is OPTIONAL in ts-blank-space, and omitting it makes unsupported syntax pass through
// VERBATIM - `enum E { A, B }` and `constructor(public q: string)` survive into the output as invalid
// JavaScript. Supplying it is what enforces the erasable-syntax-only subset (the same rule Node's
// --experimental-strip-types applies), so it is not optional here.
//
// The position is derived from the input string rather than node.getSourceFile(), which is not
// reachable in this parser-only bundle. Note that TypeScript's node.pos includes LEADING TRIVIA - it
// starts where the previous token ended - so it must be advanced past whitespace and comments first,
// or an offending statement preceded by a comment block reports the comment's position instead.
const entry = resolve(REPO, "build/native/ts-stripper-entry.mjs");
writeFileSync(
  entry,
  `import tsBlankSpace from "ts-blank-space";

/** Advance past whitespace and comments, the way node.getStart() would. */
function skipTrivia(source, pos) {
  let i = pos;
  for (;;) {
    while (i < source.length && /\\s/.test(source[i])) i++;
    if (source[i] === "/" && source[i + 1] === "/") {
      while (i < source.length && source[i] !== "\\n") i++;
    } else if (source[i] === "/" && source[i + 1] === "*") {
      const end = source.indexOf("*/", i + 2);
      i = end < 0 ? source.length : end + 2;
    } else {
      return i;
    }
  }
}

globalThis.__stripTypes = (source, filename) => {
  const bad = [];
  const out = tsBlankSpace(source, (node) => {
    const pos = skipTrivia(source, typeof node?.pos === "number" ? node.pos : 0);
    const upto = source.slice(0, pos);
    const line = upto.split("\\n").length;
    const col = pos - (upto.lastIndexOf("\\n") + 1) + 1;
    const text = source.slice(pos, node?.end ?? pos).trim().split("\\n")[0].slice(0, 60);
    bad.push(
      (filename ?? "<input>") + ":" + line + ":" + col +
      ": unsupported non-erasable TypeScript syntax: " + text
    );
  });
  if (bad.length) {
    throw new Error(
      bad.join("\\n") +
      "\\n(retroplug-cli strips types only - enum, namespace and constructor parameter properties " +
      "emit runtime code and cannot be erased. Rewrite using erasable syntax.)"
    );
  }
  return out;
};
`,
);

buildSync({
  entryPoints: [entry],
  bundle: true,
  format: "iife", // global code: JS_ReadObject + JS_EvalFunction, no module loader involved
  platform: "neutral",
  mainFields: ["module", "main"],
  target: "es2020",
  minify: true,
  outfile: outFile,
  define: { "process.env.NODE_ENV": '"production"' },
  alias: {
    // The repo pins typescript@7 (the Go port) for typechecking; ts-blank-space needs the 5.x JS
    // parser API, so it gets its own pinned copy (root devDep "typescript-5").
    typescript: "typescript-5",
    fs: stub,
    path: stub,
    os: stub,
    inspector: stub,
  },
});

console.log(`wrote ${outFile}`);

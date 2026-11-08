#!/usr/bin/env node
// The device-pack toolchain (docs/modular-graph-phase-e4-design.md section 6): a script pack the manifest marks for the
// device, TypeScript to MicroQuickJS bytecode, refused at build time for what the box could not run. The steps are
// check:portable's (feature/launchpad's scripts/check-portable-core.mjs), generalised for packs:
//
// - The bundle: esbuild bundles the pack's entry, and a module outside its allowlist fails the build: the pack's own
//   directory and the portable node SDK (SDK below), so no zod, no backend, no native seam, no UI. The bundle is built
//   with RP_DEVICE_PACK defined true, which is how defineNode learns that its definitions are a device pack's
//   (packages/core/ts/defineNode.ts): only this toolchain defines it, and it builds device packs alone.
// - The lowering: TypeScript 5 lowers the bundle to ES5 (the workspace's TypeScript 7 no longer emits ES5), as
//   MicroQuickJS parses ES5 plus a little: classes, arrows, let/const, spread and template literals must go.
// - The scan: a library call MicroQuickJS does not have (Map, Set, Object.entries, async, ...), or a global of mqjs's
//   REPL that the box's engine will not have (setTimeout, print, console.warn, ...), fails the build naming the call
//   and the source line it came from (read back through esbuild's source map, and TypeScript's), rather than as a
//   TypeError deep in a run. It covers the pack as bundled, then as lowered (the helpers the lowering adds), the whole
//   SDK with every export kept, and, for a scenario, the whole of the package's portable core the same way. A read
//   behind its own `typeof` guard is accepted, as the lowering's helpers make them.
// - The reference run: the pack runs under Node with a short epilogue that prints every node type it registered (its
//   descriptor JSON). A definition the device refuses (an audio port, packages/core/ts/defineNode.ts) throws here, at build time,
//   with its own message. Node's run is the REFERENCE, nothing more: a vm context has every standard global whatever
//   it is handed, so it says nothing about what MicroQuickJS has.
// - The bytecode: `mqjs -o` compiles the pack at the host's width (64-bit, for the desktop's mquickjs host). It is
//   relocated to address 0 and names its source by the bundle's file name alone, so the same pack builds the same
//   bytes in any checkout.
// - The device's runtime: the bytecode every node of the pack runs, on every host, is the pack bundled a second time
//   with RP_DEVICE_RUNTIME defined true as well (and esbuild's syntax folding, which turns the define into dead
//   branches it then drops), over the device's runtime of the node SDK (packages/core/ts/defineNode.ts): a definition keeps its
//   id, its create, its layout and its idle flag, which is all running a node asks of one, and the checks, the
//   settings parse and the descriptor go, which the whole pack keeps for the desktop's control context. It is lowered
//   and scanned as the pack is, and compiled with `-m32` for the box and at 64 bits for the desktop. The pack's own
//   code is the same source in both bundles; the engine runs below hold the two to the same layouts and the same
//   output.
// - The engine's run, on the runner (retroplug-mqjs-run: RetroPlug's own standard library and the MicroQuickJS script
//   host, as the desktop and the box run a pack, not upstream's REPL): the pack with the epilogue, compiled as the pack
//   is, runs as bytecode and must print Node's transcript byte for byte; each type's layout for its default settings,
//   from the whole pack and from the device's runtime, each run on the engine, must be the same JSON; and the pack's
//   64-bit bytecode, set up as a host sets a pack up, must define its node types, create one node of each from its
//   default settings and run each for a run of blocks with messages on every events or bytes input (every first byte
//   among them), inside the heap its manifest declares, by the toolchain's heuristic: the whole context as the box runs
//   it, at 32 bits, which the runner's check takes 3/2 of at 64 bits (script/MqjsHost.hpp's kCheckHalves says why,
//   and why that is no guarantee: the box's boot line and the device check are the authority). The whole pack's
//   nodes run first, as the reference; then the device's runtime's, registered from the whole pack as the desktop
//   registers them, through the same run in the same heap: their output must be the reference's, block for block (the
//   runner's digest). The runner also finds the least heap each run needs by that check, which this prints beside the
//   declaration. The 32-bit bytecode runs on the box alone.
//
// Writes, under --out-dir and only once every check has passed: <name>.js (the lowered pack, what the whole pack's
// bytecode is compiled from), <name>.device.js (the device's runtime, lowered, what every node's bytecode is compiled
// from: a fault's line is a line of it), <name>.mqjs64.bin (the whole pack), <name>.device64.bin and <name>.mqjs32.bin
// (the device's runtime at each width), <name>.pack64.c and <name>.pack32.c (the bytecode as C, with the node type ids
// the definitions register, the declared heap and the 32-bit bytecode's size, for a binary to carry: graph-runtime's
// script/rp_mqjs_pack.h; the 64-bit one carries both bundles), and <name>.transcript.txt (the engine run's
// transcript), each only when its bytes change. Its work files are in a directory of their own beside them,
// removed whether the pack passes or not. Exits nonzero saying what refused the pack. CMake runs it for a package whose
// manifest declares a script pack (cmake/packages.cmake, retroplug_device_script_pack), with the manifest's entry and
// heap; the tests run it on fixtures of their own (test/graph/device-pack.test.ts).
//
// Usage: node build-device-pack.mjs --entry <file.ts> --name <pack> --heap-bytes <n> --out-dir <dir>
//                                   [--pack-dir <dir>] [--mqjs <path>] [--runner <path>]
//        node build-device-pack.mjs --scenario <file.ts> --name <scenario> --heap-bytes <n> --out-dir <dir>
//                                   [--pack-dir <dir>] [--mqjs <path>] [--runner <path>]
//   --pack-dir  the pack's own directory, every module under which the bundle may hold (default: the entry's)
//   --mqjs      the engine's host tool (default: MQJS, else build/bin/mqjs, the desktop build's `mqjs` target)
//   --runner    the runner (default: RUNNER, else build/bin/retroplug-mqjs-run, the desktop build's target)
//
// A scenario (--scenario, see the scenario section below) is a package's portable core proved end to end: it is
// bundled, lowered and scanned as a pack is, plus the whole core - every module under the package's ts/ but its tests,
// every export kept - and it may import the package's declared dependencies. It runs under Node and on the engine, whose
// transcripts must match byte for byte in --heap-bytes, and it writes no pack: no bytecode and no C, only
// the transcript the two agreed on.

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, realpathSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { runInNewContext } from "node:vm";

const require = createRequire(import.meta.url);
const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");

/** The portable node SDK, repo-relative: what a device pack may import beside its own modules. */
const SDK = [
  "packages/core/ts/defineNode.ts",
  "packages/core/ts/settingsSchema.ts",
  "packages/core/ts/descriptor.ts",
  "packages/core/ts/register.ts",
];

// The engine the bytecode is compiled for. Pinned in deps/mquickjs; named in what this prints.
const ENGINE = "MicroQuickJS (deps/mquickjs, 6d4d7eb)";
// The heap the transcript's run gets: that run checks the engine's behaviour, not the pack's heap, which the pack's own
// bytecode is held to after it.
const TRANSCRIPT_HEAP = 1 << 20;

// The globals of upstream mqjs's REPL stdlib (deps/mquickjs/mqjs_stdlib.c), which the engine runs below use and the
// box's stdlib will not have: timers (asynchrony a device pack may not have), print, gc, load, performance and
// scriptArgs.
const REPL_CALLS = ["setTimeout", "setInterval", "clearTimeout", "clearInterval", "setImmediate", "print", "gc",
                    "load"];
const REPL_GLOBALS = ["performance", "scriptArgs"];

/** Library calls MicroQuickJS lacks, which the ES5 lowering leaves alone: check:portable's list. A name here fails
 *  the build. Matched against the code with comments and string literals blanked, and with a read behind its own
 *  `typeof` guard blanked too (blankForScan), so neither a word in a message nor a helper's guarded fallback trips it.
 *  A row is [pattern, what it names, and, for a row that names bare globals, those names]: one of those the code
 *  declares itself in a scope enclosing the use (a local `load`, say) is its own and passes, as blankForScan decides
 *  on the syntax tree. Every other row names a method or a property, which no declaration makes the code's own.
 *  Checked against the pinned engine with `typeof`: it DOES have Number.parseInt/parseFloat/EPSILON/MAX_SAFE_INTEGER,
 *  Math.trunc/sign/fround/imul/clz32/log2/log10, String trim*, codePointAt, repeat and replaceAll,
 *  Object.create/keys/defineProperty/getPrototypeOf/setPrototypeOf, and Array.isArray. */
const MISSING = [
  [/\bnew (Map|Set|WeakMap|WeakSet|Proxy|Date)\b/, "Map/Set/Proxy, or a Date object (only Date.now exists)"],
  [/\b(Symbol|Reflect|Proxy|Promise|WeakRef|Intl)\b/, "Symbol/Reflect/Proxy/Promise/WeakRef/Intl"],
  [/\b(async|await)\b/, "async/await"],
  [/\bObject\.(values|entries|fromEntries|freeze|isFrozen|seal|is|getOwnPropertyNames|getOwnPropertySymbols|getOwnPropertyDescriptors?|defineProperties)\b/,
    "Object.values/entries/fromEntries/freeze/seal/is/getOwnPropertyNames/Symbols/Descriptor(s)/defineProperties"],
  // TypeScript's own __assign helper reads `Object.assign || function...` and falls back, so a guarded use is fine;
  // a bare one is not.
  [/\bObject\.assign\b(?!\s*\|\|)/, "Object.assign"],
  [/\bArray\.(from|of)\b/, "Array.from/of"],
  [/\bNumber\.(isInteger|isFinite|isNaN|isSafeInteger)\b/, "Number.isInteger and friends"],
  [/\bMath\.(hypot|cbrt|log1p|expm1|sinh|cosh|tanh|asinh|acosh|atanh)\b/, "ES2015 Math functions"],
  [/\bString\.raw\b|\.(padStart|padEnd|startsWith|endsWith|normalize)\(/,
    "String.raw / padStart/padEnd/startsWith/endsWith/normalize"],
  [/\.(find|findIndex|findLast|findLastIndex|includes|fill|flat|flatMap|at|copyWithin)\(/, "ES2015+ Array methods"],
  [/\.prototype\.(find|findIndex|includes|fill|flat|flatMap|at|padStart|padEnd|startsWith|endsWith)\b/,
    "ES2015+ methods via .prototype"],
  [/\.(keys|values|entries)\(\)/, "array/iterator .keys()/.values()/.entries()"],
  [/\.(localeCompare|toLocaleString|toLocaleLowerCase|toLocaleUpperCase|matchAll|toISOString|toJSON)\(/,
    "locale / matchAll / Date string methods"],
  [/\bObject\.hasOwn\b|\.substr\(|\.(toSorted|toReversed|toSpliced)\(/, "Object.hasOwn / substr / toSorted & co."],
  [/\b(DataView|BigInt|TextEncoder|TextDecoder|structuredClone|queueMicrotask|encodeURIComponent|decodeURIComponent|encodeURI|decodeURI|escape|unescape)\b/,
    "DataView / BigInt / TextEncoder / structuredClone / queueMicrotask / URI functions"],
  // REPL_CALLS called and REPL_GLOBALS read, not as a property. A stopgap until the engine runs below are on the box's
  // own stdlib: a call through globalThis or an alias (`var t = setTimeout`) passes, and a method shorthand of one of
  // these names in an object literal (`{ load() {} }`) is refused.
  [new RegExp(`(?<![.\\w$])(${REPL_CALLS.join("|")})\\s*\\(|(?<![.\\w$])(${REPL_GLOBALS.join("|")})\\b(?!\\s*:)`),
    "a global of mqjs's REPL (timers, print, gc, load, performance, scriptArgs) that the box's engine will not have",
    [...REPL_CALLS, ...REPL_GLOBALS]],
  // mqjs's console has log alone.
  [/\bconsole\.(?!log\b)\w+/, "a console method other than log, which the box's engine will not have"],
  // Typed-array slice/fill/indexOf are missing too, but read like the Array methods they shadow, so they are left to
  // the engine's run rather than flagging every array .slice() here.
];

// What runs after the pack, on both engines: every node type it registered (packages/core/ts/defineNode.ts's descriptor, through
// @retroplug/core's register.ts global), one JSON line each, so the two transcripts compare what the definitions resolved to.
const EPILOGUE = `
;(function () {
  var nodes = globalThis.__rp_scriptNodes || [];
  console.log("# " + nodes.length + " node type(s)");
  for (var i = 0; i < nodes.length; ++i) console.log(JSON.stringify(nodes[i].descriptor()));
})();
`;

// ---- the arguments -----------------------------------------------------------------------------------------------

function parseArgs(argv) {
  const known = { "--entry": "entry", "--scenario": "scenario", "--name": "name", "--heap-bytes": "heapBytes",
                  "--out-dir": "outDir", "--pack-dir": "packDir", "--mqjs": "mqjs", "--runner": "runner" };
  const out = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = known[argv[i]];
    if (!key || i + 1 >= argv.length) usage(`unknown or incomplete argument ${argv[i]}`);
    out[key] = argv[i + 1];
  }
  for (const need of ["name", "heapBytes", "outDir"]) if (!out[need]) usage(`--${need} is required`);
  if (!out.entry && !out.scenario) usage("--entry or --scenario is required");
  if (out.entry && out.scenario) usage("--entry and --scenario name different things; pass one");
  return out;
}

function usage(why) {
  console.error(`build-device-pack: ${why}\nusage: node build-device-pack.mjs --entry <file.ts> --name <pack> ` +
                "--heap-bytes <n> --out-dir <dir> [--pack-dir <dir>] [--mqjs <path>] [--runner <path>]\n" +
                "       node build-device-pack.mjs --scenario <file.ts> --name <scenario> --heap-bytes <n> " +
                "--out-dir <dir> [--pack-dir <dir>] [--mqjs <path>] [--runner <path>]");
  process.exit(2);
}

const args = parseArgs(process.argv.slice(2));
const NAME = args.name;
if (!/^[a-z0-9][a-z0-9._-]*$/.test(NAME)) usage(`--name ${NAME} is not a file-name-safe pack name`);
// A scenario is bundled instead of a node entry (see the scenario section below): a script that walks a package's
// portable core on both engines, where a node entry would be registered and laid out.
const SCENARIO = args.scenario !== undefined;
const ENTRY = resolve(args.scenario ?? args.entry);
const PACK_DIR = resolve(args.packDir ?? dirname(ENTRY));
const OUT_DIR = resolve(args.outDir);
// The work files' own directory, beside the outputs: mqjs runs in it, and it goes whatever the outcome. One a process:
// a parallel build may run one pack's target from two of its goals at once (each a product that registers the pack).
const WORK = join(OUT_DIR, `.${NAME}.work-${process.pid}`);
const fail = (msg) => {
  rmSync(WORK, { recursive: true, force: true });
  console.error(`device pack ${NAME}: ${msg}`);
  process.exit(1);
};
if (!/^[1-9][0-9]*$/.test(args.heapBytes)) usage(`--heap-bytes ${args.heapBytes} is not a whole number of bytes`);
const HEAP_BYTES = Number(args.heapBytes);
if (!existsSync(ENTRY)) fail(`no entry ${ENTRY}`);
const MQJS = resolve(args.mqjs ?? process.env.MQJS ??
  join(REPO, "build/bin", process.platform === "win32" ? "mqjs.exe" : "mqjs"));
if (!existsSync(MQJS)) fail(`no mqjs at ${MQJS} (build the desktop build's \`mqjs\` target, or pass --mqjs)`);
const RUNNER = resolve(args.runner ?? process.env.RUNNER ??
  join(REPO, "build/bin", process.platform === "win32" ? "retroplug-mqjs-run.exe" : "retroplug-mqjs-run"));
if (!existsSync(RUNNER)) {
  fail(`no runner at ${RUNNER} (build the desktop build's \`retroplug-mqjs-run\` target, or pass --runner)`);
}

const repoPath = (p) => relative(REPO, p).split(sep).join("/");

// The environment mqjs and the runner run in: leak detection off, as a sanitizer build instruments them as it does
// everything (upstream's mqjs frees nothing before it exits), and a leak in a build tool is no failure of the pack's.
const TOOL_ENV = { ...process.env,
                   ASAN_OPTIONS: [process.env.ASAN_OPTIONS, "detect_leaks=0"].filter(Boolean).join(":") };

// An output written only when its bytes change, so what a build makes from it (the bytecode embedded in an image) is
// remade only then.
// Written beside it and renamed over it, so a compile reading it while another run of the same pack writes it never
// reads half a file.
function keep(file, bytes) {
  const data = typeof bytes === "string" ? Buffer.from(bytes) : bytes;
  if (existsSync(file) && readFileSync(file).equals(data)) return;
  const temp = `${file}.${process.pid}`;
  writeFileSync(temp, data);
  renameSync(temp, file);
}

// ---- the bundle and its allowlist --------------------------------------------------------------------------------

/** Every @retroplug package the scenario's own package.json declares as a dependency, by its real directory: a
 *  scenario may import what its package already depends on, and nothing else. The manifest is the nearest one above the
 *  pack's directory, and each dependency is found through that package's node_modules link (the workspace's), so a
 *  package moved or renamed is followed rather than guessed at from its specifier. A missing or unreadable manifest, and
 *  a dependency the workspace has not installed, are no dependency. */
function declaredDependencyDirs() {
  let dir = PACK_DIR;
  let manifest = null;
  for (;;) {
    const at = join(dir, "package.json");
    if (existsSync(at)) {
      manifest = at;
      break;
    }
    const up = dirname(dir);
    if (up === dir) return [];
    dir = up;
  }
  let json;
  try {
    json = JSON.parse(readFileSync(manifest, "utf8"));
  } catch {
    return [];
  }
  const out = [];
  for (const spec of Object.keys(json.dependencies ?? {})) {
    if (!spec.startsWith("@retroplug/")) continue;
    const link = join(dirname(manifest), "node_modules", spec);
    if (existsSync(link)) out.push(realpathSync(link));
  }
  return out;
}

const esbuild = require("esbuild");
const { tsOptions } = require("../../../tools/ts-options.js");
const bundleOptions = {
  absWorkingDir: REPO, // metafile and source-map paths are relative to this; run from anywhere and they still resolve
  bundle: true,
  write: false,
  metafile: true,
  format: "iife",
  platform: "neutral",
  target: "es2020",
  // The options the pack is compiled with, named rather than discovered: esbuild would otherwise read the nearest
  // tsconfig.json above the pack's entry, so a package's (or the workspace's) compiler settings would decide the pack's
  // bytes - an implied alwaysStrict alone heads the iife with a "use strict" directive, and this toolchain prepends its
  // own below. A pack's bytecode is what the box runs, and the packbytes and device-image legs hold it unchanged.
  tsconfigRaw: tsOptions(),
  logLevel: "silent",
  define: { RP_DEVICE_PACK: "true" },
};
const bundleFile = join(OUT_DIR, `${NAME}.bundle.js`); // never written: the source map's paths are relative to it
let built;
try {
  built = await esbuild.build({
    ...bundleOptions,
    entryPoints: [ENTRY],
    outfile: bundleFile,
    sourcemap: "external",
    sourcesContent: false,
  });
} catch (e) {
  fail(`bundling ${repoPath(ENTRY)} failed:\n${e.message}`);
}
const inPack = (p) => p === PACK_DIR || p.startsWith(PACK_DIR + sep);
// A scenario may import its package's DECLARED dependencies as well: a bridge package's scenario proves both halves,
// so it reaches @retroplug/launchpad and @retroplug/lsdj through the package.json that declares them. A node pack
// still may not: its bundle is what the box runs beside its own, and its imports are the SDK and itself.
const DEP_DIRS = SCENARIO ? declaredDependencyDirs() : [];
const allowed = (abs) => inPack(abs) || SDK.includes(repoPath(abs))
  || DEP_DIRS.some((dir) => abs === dir || abs.startsWith(dir + sep));
const offenders = [];
for (const input of Object.keys(built.metafile.inputs)) {
  const abs = resolve(REPO, input);
  if (allowed(abs)) continue;
  // Who brought it in, so the fix is plain.
  const by = Object.entries(built.metafile.inputs)
    .filter(([, other]) => other.imports.some((imp) => resolve(REPO, imp.path) === abs))
    .map(([from]) => repoPath(resolve(REPO, from)));
  offenders.push(`${repoPath(abs)}${by.length ? ` (imported by ${by.join(", ")})` : ""}`);
}
if (offenders.length > 0) {
  const what = SCENARIO ? "the scenario imports modules outside its allowlist (its own directory, " +
    `${repoPath(PACK_DIR)}, its package.json's declared @retroplug dependencies, and the portable node SDK in ` +
    `scripts/build-device-pack.mjs)` : `the pack imports modules outside its allowlist (its own directory, ` +
    `${repoPath(PACK_DIR)}, and the portable node SDK in scripts/build-device-pack.mjs)`;
  fail(`${what}:\n  ${offenders.join("\n  ")}`);
}
const bundleJs = built.outputFiles.find((f) => f.path.endsWith(".js")).text;
const bundleMap = JSON.parse(built.outputFiles.find((f) => f.path.endsWith(".map")).text);
console.log(`  ok: bundled ${Object.keys(built.metafile.inputs).length} modules, all in the pack or the SDK`);

// ---- the lowering to ES5 -----------------------------------------------------------------------------------------

const ts = require("typescript-5");

function lowerToEs5(code, sourceMap) {
  const out = ts.transpileModule(code, {
    reportDiagnostics: true,
    compilerOptions: {
      target: ts.ScriptTarget.ES5,
      module: ts.ModuleKind.CommonJS, // a script with no imports or exports, so this emits no module wrapper
      // Off on purpose: with it, for-of goes through Symbol.iterator. MicroQuickJS iterates arrays only, and the
      // plain lowering turns a for-of over an array into an index loop.
      downlevelIteration: false,
      removeComments: true,
      sourceMap,
    },
  });
  if (out.diagnostics?.length) {
    fail(ts.formatDiagnostics(out.diagnostics, { getCanonicalFileName: (f) => f, getCurrentDirectory: () => REPO,
                                                 getNewLine: () => "\n" }));
  }
  return { code: out.outputText.replace(/\n\/\/# sourceMappingURL=.*\s*$/, "\n"),
           map: sourceMap ? JSON.parse(out.sourceMapText) : null };
}

// MicroQuickJS is always strict; so is the reference run, so a sloppy-mode-only construct fails both.
const STRICT = '"use strict";\n';
const lowered = lowerToEs5(bundleJs, true);
const es5 = STRICT + lowered.code;
const es5Name = `${NAME}.js`;
console.log(`  ok: lowered to ES5 (${(es5.length / 1024).toFixed(1)} KB)`);

// ---- the library scan --------------------------------------------------------------------------------------------

const escapeRegExp = (text) => text.replace(/[.*+?^$()|[\]\\{}]/g, "\\$&");

// A name or a dotted chain of names (Symbol, Object.getOwnPropertySymbols): what a guard may test, spelt; or null.
function dottedName(node) {
  if (ts.isIdentifier(node)) return node.text;
  if (ts.isPropertyAccessExpression(node) && ts.isIdentifier(node.name)) {
    const head = dottedName(node.expression);
    return head && `${head}.${node.name.text}`;
  }
  return null;
}

// `typeof X === "function"` (or `!== "undefined"`, either equality), X a name or a dotted chain: X, or null.
function guardedName(node) {
  if (!ts.isBinaryExpression(node)) return null;
  const op = node.operatorToken.kind;
  const eq = op === ts.SyntaxKind.EqualsEqualsEqualsToken || op === ts.SyntaxKind.EqualsEqualsToken;
  const ne = op === ts.SyntaxKind.ExclamationEqualsEqualsToken || op === ts.SyntaxKind.ExclamationEqualsToken;
  if (!ts.isTypeOfExpression(node.left) || !ts.isStringLiteral(node.right)) return null;
  if (!((eq && node.right.text === "function") || (ne && node.right.text === "undefined"))) return null;
  return dottedName(node.left.expression);
}

// Whether a function-like node, a block or the file declares `name` in its own scope: a parameter, a function's own
// name, or a function, var, let or const among its statements. A var in a nested block is not looked for, so such a
// name is still refused (the safe side).
function declares(scope, name) {
  if (ts.isFunctionLike(scope)) {
    if (scope.name && ts.isIdentifier(scope.name) && scope.name.text === name) return true;
    if (scope.parameters.some((p) => ts.isIdentifier(p.name) && p.name.text === name)) return true;
    return false;
  }
  const statements = ts.isBlock(scope) || ts.isSourceFile(scope) ? scope.statements : [];
  for (const st of statements) {
    if (ts.isFunctionDeclaration(st) && st.name?.text === name) return true;
    if (ts.isVariableStatement(st) &&
        st.declarationList.declarations.some((d) => ts.isIdentifier(d.name) && d.name.text === name)) return true;
  }
  return false;
}

// Whether an identifier names a binding the code declares in a scope enclosing it, rather than a global.
function declaredAround(id) {
  for (let scope = id.parent; scope; scope = scope.parent) if (declares(scope, id.text)) return true;
  return false;
}

// The names a condition's guards test, anywhere in it (`a && typeof X === "function"` guards X).
function guardsIn(node, out = []) {
  const name = guardedName(node);
  if (name) out.push(name);
  else if (ts.isBinaryExpression(node) && node.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken) {
    guardsIn(node.left, out);
    guardsIn(node.right, out);
  } else if (ts.isParenthesizedExpression(node)) guardsIn(node.expression, out);
  return out;
}

/** The code with every comment, and every string and template literal's contents, blanked, for the library scan, so a
 *  word in a message or in a comment (esbuild heads each module's code with a comment naming its file) cannot trip it;
 *  with every read of a name (or a dotted chain) behind its own `typeof` guard blanked, the guard included
 *  (`typeof X === "function" && X(...)`, `if (typeof X === "function") ... X ...`, `typeof X === "function" ? X : Y`,
 *  and `!== "undefined"` for `=== "function"`), which is how the lowering's helpers reach for what an engine may lack
 *  (__rest's Object.getOwnPropertySymbols, __generator's Symbol); and with each of `ownable`'s names blanked where the
 *  code declares it in an enclosing scope. A guard in any other form is not read as one, so what it guards is refused:
 *  `||`, an else-branch, a negated guard, `typeof X === "undefined" ? fallback : X`, and an early
 *  `if (typeof X !== "function") return`. Done on TypeScript's parse of the code, not with a regex: a regex cannot
 *  tell a quote inside a regex literal (`/"/`) from the start of a string, and would then blank real code, exactly
 *  where a forbidden call could hide. */
function blankForScan(code, ownable) {
  const file = ts.createSourceFile("bundle.js", code, ts.ScriptTarget.ES5, /*setParentNodes*/ true, ts.ScriptKind.JS);
  const chars = code.split("");
  const blankRange = (from, to) => {
    for (let i = from; i < to; i++) if (chars[i] !== "\n") chars[i] = " ";
  };
  const blankComments = (pos) => {
    const ranges = [...(ts.getLeadingCommentRanges(code, pos) ?? []),
                    ...(ts.getTrailingCommentRanges(code, pos) ?? [])];
    for (const r of ranges) blankRange(r.pos, r.end);
  };
  const guarded = []; // [from, to, name]: where `name` is read behind its guard
  const visit = (node) => {
    blankComments(node.pos);
    blankComments(node.end);
    if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) {
      blankRange(node.getStart(file) + 1, node.end - 1);
    } else if (ts.isTemplateExpression(node)) {
      blankRange(node.head.getStart(file) + 1, node.head.end - 1);
      for (const span of node.templateSpans) blankRange(span.literal.getStart(file) + 1, span.literal.end - 1);
    }
    // The guarded part: the guard and the rest of its && chain, an if's then-branch, or a conditional's true side.
    let condition = null;
    let end = node.end;
    if (ts.isBinaryExpression(node) && node.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken) {
      condition = node.left;
    } else if (ts.isIfStatement(node)) {
      condition = node.expression;
      end = node.thenStatement.end;
    } else if (ts.isConditionalExpression(node)) {
      condition = node.condition;
      end = node.whenTrue.end;
    }
    if (condition) for (const name of guardsIn(condition)) guarded.push([node.getStart(file), end, name]);
    // One of the bare globals a row names, declared by the code around it: the code's own, not the global.
    if (ts.isIdentifier(node) && ownable.has(node.text) && declaredAround(node)) {
      blankRange(node.getStart(file), node.end);
    }
    ts.forEachChild(node, visit);
  };
  visit(file);
  let text = chars.join("");
  for (const [from, to, name] of guarded) {
    const re = new RegExp(`(?<![\\w$.])${escapeRegExp(name)}(?![\\w$])`, "g");
    const part = text.slice(from, to).replace(re, (m) => " ".repeat(m.length));
    text = text.slice(0, from) + part + text.slice(to);
  }
  return text;
}

// A source map's mappings, decoded: per generated line, its segments as [column, source, line, column], each 0-based.
const VLQ = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
function decodeMappings(mappings) {
  const lines = [];
  let source = 0, line = 0, column = 0;
  for (const text of mappings.split(";")) {
    const segments = [];
    let generated = 0;
    for (const segment of text.split(",")) {
      if (!segment) continue;
      const values = [];
      let value = 0, shift = 0;
      for (const ch of segment) {
        const digit = VLQ.indexOf(ch);
        value += (digit & 31) << shift;
        if (digit & 32) shift += 5;
        else {
          values.push(value & 1 ? -(value >>> 1) : value >>> 1);
          value = shift = 0;
        }
      }
      generated += values[0];
      if (values.length >= 4) {
        source += values[1];
        line += values[2];
        column += values[3];
        segments.push([generated, source, line, column]);
      }
    }
    lines.push(segments);
  }
  return lines;
}

// Where a generated position came from, through one map: the last segment at or before the column on its line (or
// the line's first), or null for code the map does not cover (a helper's).
function lookup(map, decoded, line, column) {
  const segments = decoded[line] ?? [];
  let hit = null;
  for (const s of segments) if (s[0] <= column || hit === null) hit = s;
  return hit && { source: map.sources[hit[1]], line: hit[2], column: hit[3] };
}

const loweredDecoded = decodeMappings(lowered.map.mappings);
const bundleDecoded = decodeMappings(bundleMap.mappings);

// The module and line a position came from, by esbuild's map, with the line's text; null where the map says nothing.
function moduleLine(line, column) {
  const at = lookup(bundleMap, bundleDecoded, line, column);
  if (!at) return null;
  const file = resolve(dirname(bundleFile), at.source);
  let text = "";
  try {
    text = readFileSync(file, "utf8").split("\n")[at.line]?.trim() ?? "";
  } catch {}
  return { where: `${repoPath(file)}:${at.line + 1}`, text };
}

// The bare-global names the MISSING rows list, which code may declare for itself.
const OWNABLE = new Set(MISSING.flatMap((row) => row[2] ?? []));

// For a hit in a helper (code no map covers: one esbuild or the ES5 lowering wrote), the helper's name and the source
// line that pulled it in: the first call to it that maps to a module, followed through helpers that call helpers.
function helperCaller(lines, i, origin) {
  const chain = [];
  for (let at = i, depth = 0; depth < 4; ++depth) {
    let name = null;
    for (let k = at; k >= 0 && !name; --k) name = /^\s*var (__\w+) = /.exec(lines[k])?.[1] ?? null;
    if (!name) break;
    chain.push(name);
    const call = new RegExp(`\\b${escapeRegExp(name)}\\(`);
    let next = -1;
    for (let k = 0; k < lines.length; ++k) {
      const m = call.exec(lines[k]);
      if (!m) continue;
      const from = origin ? origin(k, m.index) : null;
      if (from) return { chain, from };
      if (next < 0) next = k;
    }
    if (next < 0) break;
    at = next;
  }
  return { chain, from: null };
}

// Each MISSING kind's hit in `code`, named by the module line `origin` maps it to: the first that maps, else the first
// in a helper, named with the source line that called the helper. With no origin (the node SDK's own scan, which has
// no source map), a hit is named as the SDK's, with its line of the lowered code.
function scan(code, origin, where = "the node SDK") {
  const scanned = blankForScan(code, OWNABLE);
  const lines = scanned.split("\n");
  const hits = [];
  for (const [re, what] of MISSING) {
    let helperHit = null;
    let found = null;
    for (let i = 0; i < lines.length && !found; ++i) {
      const m = re.exec(lines[i]);
      if (!m) continue;
      const from = origin ? origin(i, m.index) : null;
      if (from) found = `${from.where}: ${m[0].trim()} (${what}): ${from.text.slice(0, 120)}`;
      else if (!origin) found = `${m[0].trim()} (${what}) in ${where}: ${code.split("\n")[i].trim().slice(0, 120)}`;
      else if (!helperHit) helperHit = { i, m };
    }
    if (!found && helperHit) {
      const { i, m } = helperHit;
      const { chain, from } = helperCaller(lines, i, origin);
      const helper = chain.length ? ` in the helper ${chain[0]}` : "";
      const through = chain.length > 1 ? ` (through ${chain.slice(1).join(", ")})` : "";
      const pulled = from ? `, which ${from.where} pulls in${through}: ${from.text.slice(0, 120)}`
                          : `: ${code.split("\n")[i].trim().slice(0, 120)}`;
      found = `${m[0].trim()} (${what})${helper} that the bundler or the ES5 lowering wrote${pulled}`;
    }
    if (found) hits.push(found);
  }
  return hits;
}

// The pack as esbuild bundled it, where every line but a helper's is a module's (so `async` is found at its own line,
// before the lowering turns it into a Promise in a helper), then as lowered, where the helpers the lowering adds are
// scanned too: a line of the lowered pack maps back through TypeScript's map to the bundle and esbuild's to the module.
let packHits = scan(bundleJs, moduleLine);
if (packHits.length === 0) {
  packHits = scan(es5, (line, column) => {
    if (line === 0) return null; // the "use strict" line is ours
    const inBundle = lookup(lowered.map, loweredDecoded, line - 1, column);
    return inBundle && moduleLine(inBundle.line, inBundle.column);
  });
}
if (packHits.length > 0) fail(`calls library functions MicroQuickJS does not have:\n  ${packHits.join("\n  ")}`);
// The whole SDK with every export kept, as check:portable scans its whole core: bundling drops what the pack does not
// import, and the scan is the only guard a path the pack never RUNS has. MicroQuickJS parses everything, so syntax is
// caught either way, but a missing library call fails only when executed.
const whole = await esbuild.build({
  ...bundleOptions,
  stdin: { contents: SDK.map((p) => `export * from ${JSON.stringify("./" + p)};`).join("\n"), resolveDir: REPO,
           loader: "ts" },
  format: "esm",
  metafile: false,
  treeShaking: false,
});
const wholeEs5 = lowerToEs5(whole.outputFiles[0].text.replace(/^export \{[\s\S]*?\};?\s*$/m, ""), false).code;
const sdkHits = scan(wholeEs5, null);
if (sdkHits.length > 0) {
  fail(`the node SDK calls library functions MicroQuickJS does not have:\n  ${sdkHits.join("\n  ")}`);
}
console.log("  ok: no library calls outside MicroQuickJS's stdlib, in the pack or anywhere in the SDK");

// ---- a scenario ---------------------------------------------------------------------------------------------------

/** The package's portable core: every module under its ts/ but its tests. A scenario is how a package proves that core
 *  on the engine, and the scenario's own bundle keeps only what it reaches, so the scan has to see the rest. */
function coreModules() {
  const out = [];
  const walk = (dir) => {
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const at = join(dir, e.name);
      if (e.isDirectory()) {
        if (e.name !== "test" && e.name !== "testSupport") walk(at);
      } else if (/[.]tsx?$/.test(e.name) && !/[.]test[.]tsx?$/.test(e.name)) {
        out.push(at);
      }
    }
  };
  walk(join(PACK_DIR, "ts"));
  return out.sort();
}

// A scenario is not a node: it is a script that reaches into the package's portable core and prints what it does, so
// a path a node never runs is checked too. It is bundled, lowered and scanned as a pack is (above), then run under
// Node and on the engine: the transcripts must be byte for byte the same, in the heap `--heap-bytes` names. It writes
// no pack, only the transcript the two agreed on; the run is the proof.
const printed = (run) => run.stdout.replace(/\r\n/g, "\n");
// How a run ended when it did not exit: a signal (POSIX), or an exception code as its status (Windows, 0xC0000005 and
// the like, past any exit status a program returns). Null for an ordinary exit.
const crash = (run) => run.signal ?? (run.status > 255 ? `0x${run.status.toString(16).toUpperCase()}` : null);
// The least 64-bit context the engine sets itself up in, with mqjs's REPL stdlib, as measured: below it JS_NewContext
// writes past its buffer and dereferences the allocation that failed, rather than throwing. The manifest refuses a pack's
// heapBytes below it (cmake/packages.cmake), and a scenario's --heap-bytes with it; the box's own floor, per width and
// stdlib, is its loader's to enforce.
const LEAST_CONTEXT = 4096;
// What a failed run said, for a message: its error printing colours the text for a terminal.
const said = (run) => `${run.stderr}${printed(run)}`.replace(/\x1b\[[0-9;]*m/g, "").trim();

if (SCENARIO) {
  if (HEAP_BYTES < LEAST_CONTEXT) {
    fail(`its heap is ${HEAP_BYTES} bytes, under the engine's least context of ${LEAST_CONTEXT}`);
  }
  // The package's whole core with every export kept, as the SDK above: a library call the engine lacks fails only on
  // the path that runs it, and the scenario walks the paths it knows about. A package with no core to scan names no
  // core, so this refuses rather than passing on nothing.
  const core = coreModules();
  if (core.length === 0) fail(`no core to scan: no module under ${repoPath(PACK_DIR)}/ts`);
  const coreBuilt = await esbuild.build({
    ...bundleOptions,
    stdin: { contents: core.map((at) => `export * from ${JSON.stringify("./" + repoPath(at))};`).join("\n"),
             resolveDir: REPO, loader: "ts" },
    format: "esm",
    metafile: false,
    treeShaking: false,
  });
  const coreEs5 = lowerToEs5(coreBuilt.outputFiles[0].text.replace(/^export \{[\s\S]*?\};?\s*$/m, ""), false).code;
  const coreHits = scan(coreEs5, null, `${repoPath(PACK_DIR)}/ts`);
  if (coreHits.length > 0) {
    fail(`the package's core calls library functions MicroQuickJS does not have:\n  ${coreHits.join("\n  ")}`);
  }
  console.log(`  ok: no library calls outside MicroQuickJS's stdlib, in the core's ${core.length} module(s)`);
  const lines = [];
  try {
    runInNewContext(es5, { console: { log: (s) => lines.push(String(s)) } }, { filename: es5Name });
  } catch (e) {
    fail(`the scenario threw under Node: ${e?.message ?? e}`);
  }
  if (lines.length === 0) fail("the scenario prints nothing, so it proves nothing ran");
  const reference = (lines.join("\n") + "\n").replace(/\r\n/g, "\n");

  rmSync(WORK, { recursive: true, force: true });
  mkdirSync(WORK, { recursive: true });
  const scenarioName = `${NAME}.scenario.js`;
  writeFileSync(join(WORK, scenarioName), es5);
  compile(scenarioName, `${NAME}.scenario.bin`, true);
  const run = spawnSync(RUNNER, ["script", `${NAME}.scenario.bin`, "--context-bytes", String(HEAP_BYTES)],
                        { cwd: WORK, encoding: "utf8", env: TOOL_ENV });
  if (run.status !== 0) {
    const how = crash(run) ? `the engine crashed (${crash(run)})` : `exit ${run.status}`;
    fail(`its bytecode failed on ${ENGINE} (${how})${said(run) ? `: ${said(run)}` : ""}`);
  }
  const engineTranscript = printed(run);
  if (engineTranscript !== reference) {
    const a = reference.split("\n");
    const b = engineTranscript.split("\n");
    let i = 0;
    while (i < a.length && a[i] === b[i]) ++i;
    fail(`${ENGINE} disagrees with Node at line ${i + 1} of the scenario's transcript:\n  node: ${a[i]}\n` +
         `  mqjs: ${b[i]}`);
  }
  rmSync(WORK, { recursive: true, force: true });
  // Kept beside the outputs, as the pack path's transcript is, so what the two engines agreed on can be read rather than
  // taken on trust. No bytecode and no C: a scenario is no pack.
  keep(join(OUT_DIR, `${NAME}.transcript.txt`), reference);
  console.log(`  ok: the scenario runs on ${ENGINE} and matches Node byte for byte (${lines.length} lines, ` +
              `${HEAP_BYTES} bytes of heap)`);
  console.log(`device pack scenario ${NAME} OK (${repoPath(ENTRY)})`);
  process.exit(0);
}

// ---- the reference run, under Node -------------------------------------------------------------------------------

const lines = [];
const sandbox = { console: { log: (s) => lines.push(String(s)) } };
try {
  runInNewContext(es5 + EPILOGUE, sandbox, { filename: es5Name });
} catch (e) {
  fail(`defining its node types threw: ${e?.message ?? e}`);
}
// Line ends as "\n" on both sides of the comparison below (the engine's are read the same way, printed()), so a line a
// definition logs with its own "\r\n" compares as the line it is.
const reference = (lines.join("\n") + "\n").replace(/\r\n/g, "\n");
// The ids the definitions register, in their order: what a binary that carries the pack registers its types under.
const typeIds = lines.filter((l) => l.startsWith("{")).map((l) => JSON.parse(l).id);
if (typeIds.length === 0) fail("registers no node type (graph/register.ts's registerScriptNodes)");
console.log(`  ok: defined ${typeIds.length} node type(s) under Node`);

// ---- the bytecode ------------------------------------------------------------------------------------------------

rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });
writeFileSync(join(WORK, es5Name), es5);

// mqjs in the work directory, with the bundle named by its file name alone: the bytecode carries the name it was
// compiled from, so an absolute path would make the bytes depend on the checkout. mqjs reports a parse error and still
// exits 0, writing nothing, so a missing output is the failure.
function compile(source, out, wide) {
  const run = spawnSync(MQJS, [...(wide ? [] : ["-m32"]), "-o", out, source],
                        { cwd: WORK, encoding: "utf8", env: TOOL_ENV });
  if (run.status !== 0 || run.stderr || !existsSync(join(WORK, out))) {
    fail(`mqjs could not compile ${source}${wide ? "" : " (-m32)"}:\n${run.stderr}${run.stdout}`);
  }
  return readFileSync(join(WORK, out));
}
const bytecode64 = compile(es5Name, `${NAME}.mqjs64.bin`, true);
console.log(`  ok: compiled to bytecode, ${bytecode64.length} bytes at 64-bit`);

// ---- the device's runtime ----------------------------------------------------------------------------------------

// The same entry, bundled with RP_DEVICE_RUNTIME as well and esbuild's syntax folding, which makes the define's tests
// constants, drops the branches they rule out and then what only those reached: the box's bundle (the header says
// what it keeps). No source map: what it calls is the full bundle's, scanned above with every line mapped, less what
// the folding dropped.
let runtimeBuilt;
try {
  runtimeBuilt = await esbuild.build({
    ...bundleOptions,
    define: { ...bundleOptions.define, RP_DEVICE_RUNTIME: "true" },
    minifySyntax: true,
    entryPoints: [ENTRY],
    outfile: bundleFile,
  });
} catch (e) {
  fail(`bundling ${repoPath(ENTRY)} for the device's runtime failed:\n${e.message}`);
}
const runtimeEs5 = STRICT + lowerToEs5(runtimeBuilt.outputFiles.find((f) => f.path.endsWith(".js")).text, false).code;
const runtimeHits = scan(runtimeEs5, null, "the device's runtime of the pack");
if (runtimeHits.length > 0) {
  fail(`the device's runtime calls library functions MicroQuickJS does not have:\n  ${runtimeHits.join("\n  ")}`);
}
const runtimeName = `${NAME}.device.js`;
writeFileSync(join(WORK, runtimeName), runtimeEs5);
const runtime64 = compile(runtimeName, `${NAME}.device64.bin`, true);
const bytecode32 = compile(runtimeName, `${NAME}.mqjs32.bin`, false);
console.log(`  ok: the device's runtime, ${(runtimeEs5.length / 1024).toFixed(1)} KB lowered, compiled to ` +
            `${bytecode32.length} bytes at 32-bit (the box's) and ${runtime64.length} at 64-bit (the desktop's)`);

// ---- the bytecode on the engine ----------------------------------------------------------------------------------

// mqjs writes its output through the C runtime's stdout, which on Windows is in text mode: each line ends "\r\n" there.
// Read as lines, as Node's transcript is, so the comparison is of what was printed.
writeFileSync(join(WORK, `${NAME}.transcript.js`), es5 + EPILOGUE);
compile(`${NAME}.transcript.js`, `${NAME}.transcript.bin`, true);
const run = spawnSync(RUNNER, ["script", `${NAME}.transcript.bin`, "--context-bytes", String(TRANSCRIPT_HEAP)],
                      { cwd: WORK, encoding: "utf8", env: TOOL_ENV });
if (run.status !== 0) {
  const how = crash(run) ? `the engine crashed (${crash(run)})` : `exit ${run.status}`;
  fail(`its bytecode failed on ${ENGINE} (${how})${said(run) ? `: ${said(run)}` : ""}`);
}
const engineTranscript = printed(run);
if (engineTranscript !== reference) {
  const a = reference.split("\n");
  const b = engineTranscript.split("\n");
  let i = 0;
  while (i < a.length && a[i] === b[i]) ++i;
  fail(`${ENGINE} disagrees with Node at line ${i + 1} of the transcript:\n  node: ${a[i]}\n  mqjs: ${b[i]}`);
}
console.log(`  ok: ${ENGINE} runs the bytecode and matches Node byte for byte (${lines.length} lines)`);

// Each type's layout for its default settings, from both bundles, each run on the engine (the runner's script): the
// whole pack's definitions parse the defaults and lay them out, and print both; the device's runtime's lay the same
// settings out (as a node's context gets them, JSON), and must print the same lines. The bundles' code is one source,
// but the runtime's definitions are defineNode's device form (orderedLayout, no checks), so this holds them to the
// whole pack's layouts directly rather than through what the nodes then send. The lines are marked, so what a pack
// prints of its own as it runs is not compared.
const LAYOUTS = (settings) => `
;(function () {
  var nodes = globalThis.__rp_scriptNodes || [];
  var settings = ${settings ?? "null"};
  console.log("@@ " + nodes.length + " node type(s)");
  for (var i = 0; i < nodes.length; ++i) {
    var s = settings ? settings[i] : nodes[i].parseSettings({});
    console.log("@@settings " + nodes[i].id + " " + JSON.stringify(s));
    console.log("@@layout " + JSON.stringify(nodes[i].layout(s)));
  }
})();
`;
function layoutsOf(source, file, settings) {
  writeFileSync(join(WORK, file), source + LAYOUTS(settings));
  const bin = file.replace(/\.js$/, ".bin");
  compile(file, bin, true);
  const ran = spawnSync(RUNNER, ["script", bin, "--context-bytes", String(TRANSCRIPT_HEAP)],
                        { cwd: WORK, encoding: "utf8", env: TOOL_ENV });
  if (ran.status !== 0) {
    const how = crash(ran) ? `the engine crashed (${crash(ran)})` : `exit ${ran.status}`;
    fail(`laying out its node types from ${file} failed on ${ENGINE} (${how})${said(ran) ? `: ${said(ran)}` : ""}`);
  }
  return printed(ran).split("\n").filter((l) => l.startsWith("@@"));
}
const fullLayouts = layoutsOf(es5, `${NAME}.layouts.js`, null);
const defaults = fullLayouts.filter((l) => l.startsWith("@@settings "))
  .map((l) => l.slice(l.indexOf(" ", "@@settings ".length) + 1));
const runtimeLayouts = layoutsOf(runtimeEs5, `${NAME}.device-layouts.js`, `[${defaults.join(", ")}]`);
const parted = fullLayouts.findIndex((l, i) => l !== runtimeLayouts[i]);
if (parted >= 0 || runtimeLayouts.length !== fullLayouts.length) {
  const i = parted >= 0 ? parted : fullLayouts.length;
  fail(`the device's runtime lays a node out unlike the pack, at line ${i + 1}:\n  pack:    ${fullLayouts[i]}\n` +
       `  runtime: ${runtimeLayouts[i]}`);
}
console.log(`  ok: the device's runtime lays its ${typeIds.length} node type(s) out as the pack does, for their ` +
            "default settings");

// The pack on the runner, in its declared heap: the nodes run from `nodes` (the whole pack's bytecode, the reference,
// or the device's runtime's, registered from the whole pack's as the desktop registers them). Its result line's
// fields, or a failure saying which step failed and why.
function runPack(nodes) {
  const runtime = nodes !== `${NAME}.mqjs64.bin`;
  const run = spawnSync(RUNNER, ["pack", nodes, "--name", NAME, "--heap-bytes", String(HEAP_BYTES),
                                 "--types", typeIds.join(","), ...(runtime ? ["--control", `${NAME}.mqjs64.bin`] : [])],
                        { cwd: WORK, encoding: "utf8", env: TOOL_ENV });
  const whose = runtime ? "the device's runtime of the pack" : "its bytecode";
  if (run.status !== 0) {
    // The runner says which step failed first: "define", "create" or "run", then the node and the engine's words.
    const what = { define: "define its node types", create: "create its nodes", run: "run its nodes" };
    const said_ = said(run);
    const step = /^(define|create|run): /.exec(said_);
    const small = HEAP_BYTES * 3 / 2 < LEAST_CONTEXT
      ? `, as it does in a context under its least, ${LEAST_CONTEXT} bytes` : "";
    const how = crash(run) ? `the engine crashed (${crash(run)})${small}${said_ ? `: ${said_}` : ""}`
                           : step ? said_.slice(step[0].length) : said_;
    fail(`${whose} does not ${step ? what[step[1]] : "run"} in its declared heap of ${HEAP_BYTES} bytes ` +
         `(the manifest's heapBytes, the box's context, checked as 3/2 of it at 64 bits): ${how}`);
  }
  const fit = /ok least=(\d+) declared=(\d+) live=(\d+) fixed=(\d+) digest=([0-9a-f]+)/.exec(run.stdout);
  if (!fit) fail(`the runner's result for ${whose} is not one it prints: ${printed(run).trim()}`);
  return { least: Number(fit[1]), live: Number(fit[3]), digest: fit[5], notes: printed(run).split("\n")
    .filter((line) => line.startsWith("note: ")) };
}
const full = runPack(`${NAME}.mqjs64.bin`);
// A node that faulted on its default settings for any reason but the heap is the pack's own business, and said.
for (const note of full.notes) console.log(`  ${note}`);
console.log(`  ok: the pack defines, creates and runs its nodes in its declared heap by the 3/2 check (${HEAP_BYTES} ` +
            `bytes, the box's context; ${HEAP_BYTES * 3 / 2} at 64 bits; the check would pass ${full.least}, and the ` +
            `run leaves ${full.live} bytes live at 64 bits; the box's boot line says what it takes there)`);
const device = runPack(`${NAME}.device64.bin`);
if (device.digest !== full.digest) {
  fail(`the device's runtime of the pack does not do what the pack does: over the same blocks its nodes' output ` +
       `digests to ${device.digest}, the full bundle's to ${full.digest}`);
}
console.log(`  ok: the device's runtime, which every host runs the nodes from, runs the same blocks to the same ` +
            `output (digest ${full.digest}) in its declared heap; the check would pass ${device.least} for it`);

// The bytecode as C, for a binary to carry (script/rp_mqjs_pack.h): one source per width, the same symbol in each, so a
// binary builds the one of its width. The pack's name is a C identifier there with every character that cannot be one
// made an underscore.
const symbol = `rp_script_${NAME.replace(/[^A-Za-z0-9_]/g, "_")}`;
function packSource(bytecode, width) {
  const rows = (bytes) => {
    const out = [];
    for (let i = 0; i < bytes.length; i += 16) {
      const row = Array.from(bytes.subarray(i, i + 16), (b) => `0x${b.toString(16).padStart(2, "0")},`);
      out.push(`    ${row.join(" ")}`);
    }
    return out.join("\n");
  };
  // The 64-bit source carries the whole pack too, for the desktop's control context; the box has none.
  const control = width === 64
    ? `\nstatic const uint8_t control[${bytecode64.length}] = {\n${rows(bytecode64)}\n};\n` : "";
  return `/* Generated by packages/retroplug/scripts/build-device-pack.mjs from the ${NAME} script pack: its
 * ${width}-bit MicroQuickJS bytecode (the device's runtime of the pack, which every node runs${width === 64
    ? ", and the whole pack, which the desktop's control context runs" : ""}), the node type ids its definitions
 * register, its declared heap and its 32-bit bytecode's size. Do not edit. */
#include "script/rp_mqjs_pack.h"

static const uint8_t bytecode[${bytecode.length}] RP_MQJS_BYTECODE_PLACEMENT = {
${rows(bytecode)}
};
${control}
static const char* const typeIds[${typeIds.length}] = {
${typeIds.map((id) => `    ${JSON.stringify(id)},`).join("\n")}
};

const rp_mqjs_pack_source ${symbol} = {
    ${JSON.stringify(NAME)}, bytecode, ${bytecode.length}, ${HEAP_BYTES}, ${typeIds.length}, typeIds,
    ${bytecode32.length}, ${width === 64 ? `control, ${bytecode64.length}` : "0, 0"}};
`;
}

// ---- the outputs, now that every check has passed ----------------------------------------------------------------

mkdirSync(OUT_DIR, { recursive: true });
keep(join(OUT_DIR, es5Name), es5);
keep(join(OUT_DIR, runtimeName), runtimeEs5);
keep(join(OUT_DIR, `${NAME}.mqjs64.bin`), bytecode64);
keep(join(OUT_DIR, `${NAME}.device64.bin`), runtime64);
keep(join(OUT_DIR, `${NAME}.mqjs32.bin`), bytecode32);
keep(join(OUT_DIR, `${NAME}.pack64.c`), packSource(runtime64, 64));
keep(join(OUT_DIR, `${NAME}.pack32.c`), packSource(bytecode32, 32));
keep(join(OUT_DIR, `${NAME}.transcript.txt`), reference);
rmSync(WORK, { recursive: true, force: true });
console.log(`device pack ${NAME} OK (${repoPath(OUT_DIR)})`);

#!/usr/bin/env node
// The device-pack toolchain (docs/modular-graph-phase-e4-design.md section 6): a script pack the manifest marks for the
// device, TypeScript to MicroQuickJS bytecode, refused at build time for what the box could not run. The steps are
// check:portable's (feature/launchpad's scripts/check-portable-core.mjs), generalised for packs:
//
// - The bundle: esbuild bundles the pack's entry, and a module outside its allowlist fails the build: the pack's own
//   directory and the portable node SDK (SDK below), so no zod, no backend, no native seam, no UI. The bundle is built
//   with RP_DEVICE_PACK defined true, which is how defineNode learns that its definitions are a device pack's
//   (graph/defineNode.ts): only this toolchain defines it, and it builds device packs alone.
// - The lowering: TypeScript 5 lowers the bundle to ES5 (the workspace's TypeScript 7 no longer emits ES5), as
//   MicroQuickJS parses ES5 plus a little: classes, arrows, let/const, spread and template literals must go.
// - The scan: a library call MicroQuickJS does not have (Map, Set, Object.entries, async, ...), or a global of mqjs's
//   REPL that the box's engine will not have (setTimeout, print, console.warn, ...), fails the build naming the call
//   and the source line it came from (read back through esbuild's source map, and TypeScript's), rather than as a
//   TypeError deep in a run. It covers the pack as bundled, then as lowered (the helpers the lowering adds), and the
//   whole SDK with every export kept. A read behind its own `typeof` guard is accepted, as the lowering's helpers make
//   them.
// - The reference run: the pack runs under Node with a short epilogue that prints every node type it registered (its
//   descriptor JSON). A definition the device refuses (an audio port, graph/defineNode.ts) throws here, at build time,
//   with its own message. Node's run is the REFERENCE, nothing more: a vm context has every standard global whatever
//   it is handed, so it says nothing about what MicroQuickJS has.
// - The bytecode: `mqjs -o` compiles the pack at the host's width (64-bit, for the desktop's mquickjs host) and with
//   `-m32` (the box's). It is relocated to address 0 and names its source by the bundle's file name alone, so the same
//   pack builds the same bytes in any checkout.
// - The engine's run: the pack with the epilogue, compiled as the pack is, runs as bytecode on the engine and must
//   print Node's transcript byte for byte; and the pack's own 64-bit bytecode must define its node types inside the
//   heap its manifest declares (`mqjs --memory-limit`, which holds the whole context, so the figure is the context's
//   size, and a 64-bit context is the larger of the two). The 32-bit bytecode runs on the box alone. Both runs are on
//   upstream's REPL (mqjs.c and its stdlib), not yet on the box's own stdlib.
//
// Writes, under --out-dir and only once every check has passed: <name>.js (the lowered pack, what the bytecode is
// compiled from), <name>.mqjs64.bin and <name>.mqjs32.bin (the bytecode), and <name>.transcript.txt (the engine run's
// transcript), each only when its bytes change. Its work files are in a directory of their own beside them, removed
// whether the pack passes or not. Exits nonzero saying what refused the pack. CMake runs it for a package whose
// manifest declares a script pack (cmake/packages.cmake, retroplug_script_pack), with the manifest's entry and heap;
// the tests run it on fixtures of their own (test/graph/device-pack.test.ts).
//
// Usage: node build-device-pack.mjs --entry <file.ts> --name <pack> --heap-bytes <n> --out-dir <dir>
//                                   [--pack-dir <dir>] [--mqjs <path>]
//   --pack-dir  the pack's own directory, every module under which the bundle may hold (default: the entry's)
//   --mqjs      the engine's host tool (default: MQJS, else build/bin/mqjs, the desktop build's `mqjs` target)

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { runInNewContext } from "node:vm";

const require = createRequire(import.meta.url);
const PKG = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const REPO = resolve(PKG, "../..");

/** The portable node SDK, repo-relative: what a device pack may import beside its own modules. */
const SDK = [
  "packages/retroplug/src/graph/defineNode.ts",
  "packages/retroplug/src/graph/settingsSchema.ts",
  "packages/retroplug/src/graph/descriptor.ts",
  "packages/retroplug/src/script/register.ts",
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

// What runs after the pack, on both engines: every node type it registered (graph/defineNode.ts's descriptor, through
// script/register.ts's global), one JSON line each, so the two transcripts compare what the definitions resolved to.
const EPILOGUE = `
;(function () {
  var nodes = globalThis.__rp_scriptNodes || [];
  console.log("# " + nodes.length + " node type(s)");
  for (var i = 0; i < nodes.length; ++i) console.log(JSON.stringify(nodes[i].descriptor()));
})();
`;

// ---- the arguments -----------------------------------------------------------------------------------------------

function parseArgs(argv) {
  const known = { "--entry": "entry", "--name": "name", "--heap-bytes": "heapBytes", "--out-dir": "outDir",
                  "--pack-dir": "packDir", "--mqjs": "mqjs" };
  const out = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = known[argv[i]];
    if (!key || i + 1 >= argv.length) usage(`unknown or incomplete argument ${argv[i]}`);
    out[key] = argv[i + 1];
  }
  for (const need of ["entry", "name", "heapBytes", "outDir"]) if (!out[need]) usage(`--${need} is required`);
  return out;
}

function usage(why) {
  console.error(`build-device-pack: ${why}\nusage: node build-device-pack.mjs --entry <file.ts> --name <pack> ` +
                "--heap-bytes <n> --out-dir <dir> [--pack-dir <dir>] [--mqjs <path>]");
  process.exit(2);
}

const args = parseArgs(process.argv.slice(2));
const NAME = args.name;
if (!/^[a-z0-9][a-z0-9._-]*$/.test(NAME)) usage(`--name ${NAME} is not a file-name-safe pack name`);
const ENTRY = resolve(args.entry);
const PACK_DIR = resolve(args.packDir ?? dirname(ENTRY));
const OUT_DIR = resolve(args.outDir);
// The work files' own directory, beside the outputs: mqjs runs in it, and it goes whatever the outcome.
const WORK = join(OUT_DIR, `.${NAME}.work`);
const fail = (msg) => {
  rmSync(WORK, { recursive: true, force: true });
  console.error(`device pack ${NAME}: ${msg}`);
  process.exit(1);
};
if (!/^[1-9][0-9]*$/.test(args.heapBytes)) usage(`--heap-bytes ${args.heapBytes} is not a whole number of bytes`);
const HEAP_BYTES = Number(args.heapBytes);
if (!existsSync(ENTRY)) fail(`no entry ${ENTRY}`);
const MQJS = args.mqjs ?? process.env.MQJS ??
  join(REPO, "build/bin", process.platform === "win32" ? "mqjs.exe" : "mqjs");
if (!existsSync(MQJS)) fail(`no mqjs at ${MQJS} (build the desktop build's \`mqjs\` target, or pass --mqjs)`);

const repoPath = (p) => relative(REPO, p).split(sep).join("/");

// An output written only when its bytes change, so what a build makes from it (the bytecode embedded in an image) is
// remade only then.
function keep(file, bytes) {
  const data = typeof bytes === "string" ? Buffer.from(bytes) : bytes;
  if (existsSync(file) && readFileSync(file).equals(data)) return;
  writeFileSync(file, data);
}

// ---- the bundle and its allowlist --------------------------------------------------------------------------------

const esbuild = require("esbuild");
const bundleOptions = {
  absWorkingDir: REPO, // metafile and source-map paths are relative to this; run from anywhere and they still resolve
  bundle: true,
  write: false,
  metafile: true,
  format: "iife",
  platform: "neutral",
  target: "es2020",
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
const offenders = [];
for (const input of Object.keys(built.metafile.inputs)) {
  const abs = resolve(REPO, input);
  if (inPack(abs) || SDK.includes(repoPath(abs))) continue;
  // Who brought it in, so the fix is plain.
  const by = Object.entries(built.metafile.inputs)
    .filter(([, other]) => other.imports.some((imp) => resolve(REPO, imp.path) === abs))
    .map(([from]) => repoPath(resolve(REPO, from)));
  offenders.push(`${repoPath(abs)}${by.length ? ` (imported by ${by.join(", ")})` : ""}`);
}
if (offenders.length > 0) {
  fail(`the pack imports modules outside its allowlist (its own directory, ${repoPath(PACK_DIR)}, and the portable ` +
       `node SDK in scripts/build-device-pack.mjs):\n  ${offenders.join("\n  ")}`);
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
function scan(code, origin) {
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
      else if (!origin) found = `${m[0].trim()} (${what}) in the node SDK: ${code.split("\n")[i].trim().slice(0, 120)}`;
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
const types = lines.filter((l) => l.startsWith("{")).length;
if (types === 0) fail("registers no node type (graph/register.ts's registerScriptNodes)");
console.log(`  ok: defined ${types} node type(s) under Node`);

// ---- the bytecode ------------------------------------------------------------------------------------------------

rmSync(WORK, { recursive: true, force: true });
mkdirSync(WORK, { recursive: true });
writeFileSync(join(WORK, es5Name), es5);

// mqjs in the work directory, with the bundle named by its file name alone: the bytecode carries the name it was
// compiled from, so an absolute path would make the bytes depend on the checkout. mqjs reports a parse error and still
// exits 0, writing nothing, so a missing output is the failure.
function compile(source, out, wide) {
  const run = spawnSync(MQJS, [...(wide ? [] : ["-m32"]), "-o", out, source], { cwd: WORK, encoding: "utf8" });
  if (run.status !== 0 || run.stderr || !existsSync(join(WORK, out))) {
    fail(`mqjs could not compile ${source}${wide ? "" : " (-m32)"}:\n${run.stderr}${run.stdout}`);
  }
  return readFileSync(join(WORK, out));
}
const bytecode64 = compile(es5Name, `${NAME}.mqjs64.bin`, true);
const bytecode32 = compile(es5Name, `${NAME}.mqjs32.bin`, false);
console.log(`  ok: compiled to bytecode, ${bytecode64.length} bytes at 64-bit and ${bytecode32.length} at 32-bit`);

// ---- the bytecode on the engine ----------------------------------------------------------------------------------

// mqjs writes its output through the C runtime's stdout, which on Windows is in text mode: each line ends "\r\n" there.
// Read as lines, as Node's transcript is, so the comparison is of what was printed.
const printed = (run) => run.stdout.replace(/\r\n/g, "\n");
// How a run ended when it did not exit: a signal (POSIX), or an exception code as its status (Windows, 0xC0000005 and
// the like, past any exit status a program returns). Null for an ordinary exit.
const crash = (run) => run.signal ?? (run.status > 255 ? `0x${run.status.toString(16).toUpperCase()}` : null);
// The least 64-bit context the engine sets itself up in, with mqjs's REPL stdlib, as measured: below it JS_NewContext
// writes past its buffer and dereferences the allocation that failed, rather than throwing. The manifest refuses a
// figure below it (cmake/packages.cmake); the box's own floor, per width and stdlib, is its loader's to enforce.
const LEAST_CONTEXT = 4096;
// What a failed run said, for a message: its error printing colours the text for a terminal.
const said = (run) => `${run.stderr}${printed(run)}`.replace(/\x1b\[[0-9;]*m/g, "").trim();

writeFileSync(join(WORK, `${NAME}.transcript.js`), es5 + EPILOGUE);
compile(`${NAME}.transcript.js`, `${NAME}.transcript.bin`, true);
const run = spawnSync(MQJS, ["--memory-limit", String(TRANSCRIPT_HEAP), "-b", `${NAME}.transcript.bin`],
                      { cwd: WORK, encoding: "utf8" });
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

const own = spawnSync(MQJS, ["--memory-limit", String(HEAP_BYTES), "-b", `${NAME}.mqjs64.bin`],
                      { cwd: WORK, encoding: "utf8" });
if (own.status !== 0) {
  const small = HEAP_BYTES < LEAST_CONTEXT ? `, as it does in a context under its least, ${LEAST_CONTEXT} bytes` : "";
  const how = crash(own) ? `the engine crashed (${crash(own)})${small}${said(own) ? `: ${said(own)}` : ""}` : said(own);
  fail(`its bytecode does not define its node types in its declared heap of ${HEAP_BYTES} bytes ` +
       `(the manifest's heapBytes): ${how}`);
}
console.log(`  ok: the pack's bytecode defines its node types in its declared heap (${HEAP_BYTES} bytes)`);

// ---- the outputs, now that every check has passed ----------------------------------------------------------------

mkdirSync(OUT_DIR, { recursive: true });
keep(join(OUT_DIR, es5Name), es5);
keep(join(OUT_DIR, `${NAME}.mqjs64.bin`), bytecode64);
keep(join(OUT_DIR, `${NAME}.mqjs32.bin`), bytecode32);
keep(join(OUT_DIR, `${NAME}.transcript.txt`), reference);
rmSync(WORK, { recursive: true, force: true });
console.log(`device pack ${NAME} OK (${repoPath(OUT_DIR)})`);

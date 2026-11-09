#!/usr/bin/env node
// Walks the app shell's sources for a cart or console literal, for test/source-literals/no-cart-literals.test.ts
// (docs/modular-graph-phase-f4a-design.md section 7.2). The walk is over TypeScript's own syntax tree (the pinned
// `typescript-5` the boundary check loads), so a comment is trivia and every string a program can hold is read: a quoted
// string, a template with no substitution, and each span of a template that has one. It also reads the names a table's
// keys are written with - an object literal's property, a method's, a shorthand's, an interface member's and an enum
// member's (an identifier, a string or a numeric key) - and a regular expression's pattern, because a `Record<Platform,
// X>` keyed by identifiers and an `/\.(gb|gbc)$/` filter are the two shapes the old platform tables took. A regular
// expression or a template with a substitution cannot move the walk's place, which a bare token scan loses at the
// `${...}` that closes a span.
//
//   node test/source-literals/run-scan.mjs --platforms <id,...> --extensions <.ext,...> \
//     --cores <id,...> --node-types <type,...> --carts <id,...> [--dirs src,cli] [--root <dir>]
//
// The guarded sets are the console registry's, not this file's: the test imports the generated CONSOLES list and passes
// its platform ids, extensions, cores and node types, so a console a product registers later is guarded without an edit
// here. A value is a hit when it is one of those, the `*.gb`-style glob a file filter would take, or text that ends in a
// guarded extension (the `rom.gb` tail a suffix test reads). A pattern is a hit when its source spells an escaped dot and
// it matches `rom<ext>` for a guarded extension but not the extension's bare letters: the escaped dot keeps a character
// class that happens to hold one (a filename sanitiser) out, and the bare-letter test keeps a permissive pattern such as
// /^[\x20-\x7e]*$/ out. What it cannot see is what a program computes rather than spells: a platform assembled at
// runtime (["g", "b"].join("")), a table keyed by a computed name, and a dotless pattern such as /(gb|gbc)$/.
// Prints one JSON line: { literals: [{ file, value }] }, src/generated/ not scanned.
import { readdirSync, readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ts = createRequire(import.meta.url)("typescript-5");
const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");

/** The value of `--name`, or undefined. */
function option(name) {
  const at = process.argv.indexOf(name);
  return at === -1 ? undefined : process.argv[at + 1];
}

const platforms = option("--platforms");
const extensions = option("--extensions");
const cores = option("--cores");
const nodeTypes = option("--node-types");
// `--carts` may be EMPTY and the other four may not: a fixture tree whose generated package list declares no cart is
// scanned with no cart guard, which is right, while every tree has consoles. An absent flag is still not the same as
// an empty one - it would scan a real tree with none of the cart ids - so it is tested for undefined.
const carts = option("--carts");
if (!platforms || !extensions || !cores || !nodeTypes || carts === undefined) {
  console.error(
    "run-scan: --platforms, --extensions, --cores, --node-types and --carts are required: they are the console and " +
      "package registries', which the caller reads from the generated lists rather than this file keeping a second " +
      "copy of them.",
  );
  process.exit(2);
}
const root = resolve(option("--root") ?? REPO);
const dirs = (option("--dirs") ?? "src").split(",").filter((entry) => entry !== "");
const list = (csv) => csv.split(",").filter((entry) => entry !== "");
const PLATFORMS = new Set(list(platforms));
const EXTENSIONS = new Set(list(extensions));
const CORES = new Set(list(cores));
const NODE_TYPES = new Set(list(nodeTypes));
const CARTS = new Set(list(carts));

/** True for an exact guarded value: a platform id, an extension, a core id, a node type, a cart id (a kit format's,
 *  an embed's or a tracker integration's), or the `*.gb` glob. */
const isGuarded = (value) =>
  PLATFORMS.has(value) ||
  EXTENSIONS.has(value) ||
  CORES.has(value) ||
  NODE_TYPES.has(value) ||
  CARTS.has(value) ||
  (value.startsWith("*.") && EXTENSIONS.has(value.slice(1)));

/** True for text that ends in a guarded extension: the `rom.gb` tail a suffix test or a template builds. A string with
 *  whitespace in it is prose (a help text naming a file a user would type), not a path a program tests: a path has no
 *  whitespace, and the CLI's help texts name their inputs. */
const endsInExtension = (value) => {
  if (/\s/.test(value)) return false;
  for (const extension of EXTENSIONS) {
    if (value.length > extension.length && value.endsWith(extension)) return true;
  }
  return false;
};
const isHit = (value) => isGuarded(value) || endsInExtension(value);

/** True for a pattern that reads a guarded extension: its source spells an escaped dot, it matches `rom<ext>`, and it
 *  does not match the extension's bare letters (so a printable-character class or a bare `/gb/` is not one). */
const isPatternHit = (source, flags) => {
  if (!source.includes("\\.")) return false;
  let pattern;
  try {
    pattern = new RegExp(source, flags);
  } catch {
    return false;
  }
  const matches = (text) => {
    pattern.lastIndex = 0;
    return pattern.test(text);
  };
  for (const extension of EXTENSIONS) {
    if (matches(`rom${extension}`) && !matches(extension.slice(1))) return true;
  }
  return false;
};

/** Every .ts/.tsx file under `dir`, in a stable order, without the generated registries or the SDK's declaration file
 *  (a consumer's view of the SDK, written for an editor, not the shell's own code). */
function walk(dir, out = []) {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name !== "generated") walk(path, out);
    } else if (/\.(ts|tsx)$/.test(entry.name) && entry.name !== "sdk-types.d.ts") out.push(path);
  }
  return out;
}

/** The name a property is written with, when it is one a program can compare: an identifier, a string or a numeric key.
 *  A computed name is not read here; a string literal inside one is a value the walk visits. */
function nameOf(name) {
  if (!name) return undefined;
  if (ts.isIdentifier(name) || ts.isPrivateIdentifier(name)) return name.text;
  if (ts.isStringLiteral(name) || ts.isNumericLiteral(name)) return name.text;
  return undefined;
}

/** The key a node writes, for the node kinds that declare one. */
function keyOf(node) {
  if (
    ts.isPropertyAssignment(node) ||
    ts.isShorthandPropertyAssignment(node) ||
    ts.isMethodDeclaration(node) ||
    ts.isGetAccessorDeclaration(node) ||
    ts.isSetAccessorDeclaration(node) ||
    ts.isPropertySignature(node) ||
    ts.isPropertyDeclaration(node) ||
    ts.isEnumMember(node)
  ) {
    return nameOf(node.name);
  }
  return undefined;
}

/** True for a string literal that is a property's name, which `keyOf` reads: collecting it twice would report one hit
 *  as two. */
function isKeyName(node) {
  const parent = node.parent;
  if (!parent) return false;
  return (
    (ts.isPropertyAssignment(parent) ||
      ts.isMethodDeclaration(parent) ||
      ts.isGetAccessorDeclaration(parent) ||
      ts.isSetAccessorDeclaration(parent) ||
      ts.isPropertySignature(parent) ||
      ts.isPropertyDeclaration(parent)) &&
    parent.name === node
  );
}

/** The text a pattern reads: a regular expression literal holds `/source/flags` in `text`. */
function patternOf(node) {
  const text = node.text;
  const last = text.lastIndexOf("/");
  return last <= 0 ? { source: text, flags: "" } : { source: text.slice(1, last), flags: text.slice(last + 1) };
}

/** The values and patterns of one file the walk reads, in source order: the literals a program can compare, the names
 *  its tables are keyed by, and the patterns it filters with - not the trivia around them. A template's head and each
 *  span's literal are nodes of their own, which the tree does not visit as children of the expression that holds them. */
function hitsOf(file, text) {
  const source = ts.createSourceFile(
    file,
    text,
    ts.ScriptTarget.Latest,
    true,
    file.endsWith(".tsx") ? ts.ScriptKind.TSX : ts.ScriptKind.TS,
  );
  const found = [];
  const visit = (node) => {
    if (ts.isTemplateExpression(node)) {
      for (const value of [node.head.text, ...node.templateSpans.map((span) => span.literal.text)]) {
        if (isHit(value)) found.push(value);
      }
      for (const span of node.templateSpans) visit(span.expression);
      return;
    }
    if (ts.isRegularExpressionLiteral(node)) {
      const { source: pattern, flags } = patternOf(node);
      if (isPatternHit(pattern, flags)) found.push(`/${pattern}/${flags}`);
    } else if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) {
      if (!isKeyName(node) && isHit(node.text)) found.push(node.text);
    }
    const key = keyOf(node);
    if (key !== undefined && isHit(key)) found.push(key);
    ts.forEachChild(node, visit);
  };
  visit(source);
  return found;
}

const literals = [];
for (const dir of dirs) {
  for (const file of walk(join(root, "packages/retroplug", dir))) {
    for (const value of hitsOf(file, readFileSync(file, "utf8"))) {
      literals.push({ file: relative(root, file).split("\\").join("/"), value });
    }
  }
}
console.log(JSON.stringify({ literals }));

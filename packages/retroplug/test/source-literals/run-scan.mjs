#!/usr/bin/env node
// Walks the app shell's sources for a platform or ROM-extension literal, for
// test/source-literals/no-cart-literals.test.ts (docs/modular-graph-phase-f4a-design.md section 7.2). The walk is over
// TypeScript's own syntax tree (the pinned `typescript-5` the boundary check loads), so a comment is trivia and every
// string a program can hold is read: a quoted string, a template with no substitution, and each span of a template
// that has one. A regular expression or a template with a substitution cannot move the walk's place, which a bare
// token scan loses at the `${...}` that closes a span.
//
//   node test/source-literals/run-scan.mjs --platforms <id,...> --extensions <.ext,...> [--root <dir>]
//
// The guarded sets are the console registry's, not this file's: the test imports the generated CONSOLES list and passes
// its platform ids and extensions, so a console a product registers later is guarded without an edit here. A value is a
// hit when it is a platform id, one of the descriptors' extensions, or the `*.gb`-style glob a file filter would take.
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
if (!platforms || !extensions) {
  console.error(
    "run-scan: --platforms and --extensions are required: they are the console registry's ids and extensions, which " +
      "the caller reads from the generated CONSOLES list rather than this file keeping a second copy of them.",
  );
  process.exit(2);
}
const root = resolve(option("--root") ?? REPO);
const list = (csv) => csv.split(",").filter((entry) => entry !== "");
const PLATFORMS = new Set(list(platforms));
const EXTENSIONS = new Set(list(extensions));
const isExtensionPattern = (value) => value.startsWith("*.") && EXTENSIONS.has(value.slice(1));
const isHit = (value) => PLATFORMS.has(value) || EXTENSIONS.has(value) || isExtensionPattern(value);

/** Every .ts/.tsx file under `dir`, in a stable order, without the generated registries. */
function walk(dir, out = []) {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name !== "generated") walk(path, out);
    } else if (/\.(ts|tsx)$/.test(entry.name)) out.push(path);
  }
  return out;
}

/** The text of every string the tree holds, in source order: the literals a program can compare, not the trivia around
 *  them. A template's head and each span's literal are nodes of their own, which the tree does not visit as children of
 *  the expression that holds them. */
function stringsOf(file, text) {
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
      found.push(node.head.text);
      for (const span of node.templateSpans) {
        found.push(span.literal.text);
        visit(span.expression);
      }
      return;
    }
    if (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) found.push(node.text);
    ts.forEachChild(node, visit);
  };
  visit(source);
  return found;
}

const literals = [];
for (const file of walk(join(root, "packages/retroplug/src"))) {
  for (const value of stringsOf(file, readFileSync(file, "utf8"))) {
    if (isHit(value)) literals.push({ file: relative(root, file).split("\\").join("/"), value });
  }
}
console.log(JSON.stringify({ literals }));

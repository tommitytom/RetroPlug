#!/usr/bin/env node
// Runs packages/retroplug/scripts/check-boundaries.mjs over a fixture tree, for
// test/boundaries/boundaries-checker.test.ts.
//
// The checker is Node's (it reads TypeScript's scanner and the filesystem), so a test that runs under txiki spawns this
// instead of importing it, as test/packages/run-generator.mjs runs the generator.
//
//   node test/boundaries/run-checker.mjs <case>          one tree: fixtures/<case>/, a workspace root of its own
//   node test/boundaries/run-checker.mjs --tree          the real tree
//
// Prints one JSON line: { status, refused: [<the checker's "refused:" lines>], allowed: [...], warnings: [...],
// stale: [...] }, which is the checker's own output read back, so a test holds the message a user would see.
import { spawnSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const CHECKER = join(REPO, "packages/retroplug/scripts/check-boundaries.mjs");
const caseName = process.argv[2];
if (!caseName) {
  console.error("run-checker.mjs: name a fixture case, or --tree for the real tree");
  process.exit(2);
}
const root = caseName === "--tree" ? REPO : join(HERE, "fixtures", caseName);

const child = spawnSync(process.execPath, [CHECKER, "--root", root, "--verbose"], { encoding: "utf8" });
const lines = `${child.stdout ?? ""}${child.stderr ?? ""}`.split("\n");
const pick = (prefix) =>
  lines
    .filter((line) => line.startsWith(`${prefix}: `))
    .map((line) => line.slice(prefix.length + 2))
    .filter((line) => !line.startsWith("check-boundaries:"));
console.log(
  JSON.stringify({
    status: child.status,
    refused: pick("refused"),
    allowed: pick("allowed"),
    warnings: pick("warning"),
    stale: pick("stale"),
  }),
);

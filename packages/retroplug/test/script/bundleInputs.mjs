#!/usr/bin/env node
// The driver behind test/script/bundle-no-zod.test.ts: builds the script-node bundle the way
// packages/native/cmake/bundles.cmake builds it (tools/build-session.js with its own options, an iife) and prints one
// JSON line - the modules that went in, the bundle's size, and the modules the product's packages contribute as their
// `app` member. Those are the role registrations, each built on zod, and the bundle must carry none of them: reading
// them from the manifests is what makes a cart package that contributes a role denied here the moment it is listed in
// the product, with no edit to the test. A package's `protocol` member is deliberately NOT among them: those are script
// nodes, which the bundle exists to carry (the test asserts core's midiRoute.ts is in it).
//
// The paths are printed as esbuild's metafile spells them - relative to this process's working directory, which is
// packages/retroplug when the test suite runs - so the test compares like with like.
import { readFileSync } from "node:fs";
import { dirname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { bundleInputs } from "../../../../tools/build-session.js";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");

/** Every `retroplug.app` member in the desktop product's packages, as a path from the working directory. */
function appModules() {
  const products = JSON.parse(readFileSync(resolve(REPO, "package.json"), "utf8")).retroplug.products.desktop;
  if (!Array.isArray(products)) throw new Error(`${REPO}/package.json has no retroplug.products.desktop list`);
  const out = [];
  for (const name of products) {
    const dir = resolve(REPO, "packages", name);
    const manifest = JSON.parse(readFileSync(resolve(dir, "package.json"), "utf8"));
    const app = manifest?.retroplug?.app;
    if (typeof app !== "string") continue;
    out.push(relative(process.cwd(), resolve(dir, app)).split(sep).join("/"));
  }
  return out;
}

const built = await bundleInputs("src/script/bundle.ts", "iife");
console.log(JSON.stringify({ inputs: built.inputs, bytes: built.bytes, appModules: appModules() }));

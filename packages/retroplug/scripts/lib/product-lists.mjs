#!/usr/bin/env node
// Prints the root manifest's product lists, one per line, as `<name>=<package>,<package>,...` for the configure
// (CMakeLists.txt reads retroplug.products into RP_PRODUCT_PACKAGES_<name>): CMake's JSON reader takes a member by name
// and cannot enumerate an object's keys, so the enumeration is here, beside the generator that reads the same lists.
//
//   node packages/retroplug/scripts/lib/product-lists.mjs <root package.json>
//
// A list's own shape (names, no duplicate, no shell) is validated by the generator, which reads the same JSON; this
// script only reports, and refuses a manifest with no products at all.
import { readFileSync } from "node:fs";

const manifestPath = process.argv[2];
if (!manifestPath) {
  console.error("usage: node packages/retroplug/scripts/lib/product-lists.mjs <root package.json>");
  process.exit(2);
}
const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
const products = manifest.retroplug?.products;
const names = Object.keys(products ?? {});
if (names.length === 0) {
  console.error(`${manifestPath} has no retroplug.products lists`);
  process.exit(1);
}
for (const name of names) {
  const list = products[name];
  if (!Array.isArray(list) || list.some((entry) => typeof entry !== "string")) {
    console.error(`${manifestPath}: retroplug.products.${name} is not a list of package names`);
    process.exit(1);
  }
  // Comma-separated: CMake lists are semicolon-separated, and a line is read as one list element before it is split.
  console.log(`${name}=${list.join(",")}`);
}

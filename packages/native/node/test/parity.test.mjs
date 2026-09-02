// Node-API codec parity: the SAME host-facet operation matrix (ops.mjs) run through both hosts:
// retroplug-cli's QuickJS codec and retroplug.node's N-API codec must produce identical results.
//
//   node --test packages/native/node/test/parity.test.mjs
//
// Requires both artifacts:
//   cmake --build build --target retroplug-cli -j$(nproc)
//   ./build.sh -DRETROPLUG_NODE_ADDON=ON   (then: cmake --build build --target retroplug-node)
//
// This is the real assertion of the spike. "It works on Node" is weak; "it produces byte-identical
// results to the shipping QuickJS host over binary, structs, vectors, optionals and error envelopes"
// is the claim that matters, and it is checked here rather than eyeballed.

import { test, before } from "node:test";
import assert from "node:assert/strict";
import { build } from "esbuild";
import { spawnSync } from "node:child_process";
import { mkdtempSync, existsSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "../../../..");
const ADDON = join(REPO, "build/node/retroplug.node");
const CLI = join(REPO, "build/bin/retroplug-cli");

const require = createRequire(import.meta.url);

function requireArtifacts() {
  const missing = [];
  if (!existsSync(ADDON)) missing.push(`${ADDON} (./build.sh -DRETROPLUG_NODE_ADDON=ON)`);
  if (!existsSync(CLI)) missing.push(`${CLI} (cmake --build build --target retroplug-cli)`);
  if (missing.length) throw new Error(`missing build artifacts:\n  ${missing.join("\n  ")}`);
}

/** Run the matrix in-process through the N-API addon. */
async function runNode(tmp) {
  const { runOps } = await import("./ops.mjs");
  const addon = require(ADDON);
  return runOps(addon.rpcSend, tmp);
}

/** Bundle the session and run the matrix out-of-process through the QuickJS host. */
async function runQuickJs(tmp, outDir) {
  const bundle = join(outDir, "parity-session.js");
  await build({
    entryPoints: [join(HERE, "session.mjs")],
    bundle: true,
    format: "esm",
    platform: "neutral",
    mainFields: ["module", "main"],
    target: "es2020",
    outfile: bundle,
    logLevel: "warning",
  });

  const res = spawnSync(CLI, [bundle, tmp], { encoding: "utf8" });
  const line = (res.stdout || "").split("\n").find((l) => l.startsWith("__PARITY__"));
  if (!line) {
    throw new Error(
      `retroplug-cli produced no parity output (status ${res.status})\n` +
        `stdout: ${res.stdout}\nstderr: ${res.stderr}`,
    );
  }
  return JSON.parse(line.slice("__PARITY__".length));
}

let nodeResults;
let qjsResults;

before(async () => {
  requireArtifacts();
  const scratch = mkdtempSync(join(tmpdir(), "rp-node-parity-"));
  const nodeTmp = join(scratch, "node");
  const qjsTmp = join(scratch, "qjs");
  mkdirSync(nodeTmp);
  mkdirSync(qjsTmp);

  nodeResults = await runNode(nodeTmp);
  qjsResults = await runQuickJs(qjsTmp, scratch);
});

test("both hosts run the whole matrix", () => {
  assert.ok(nodeResults.length > 0, "no results from the Node host");
  assert.deepStrictEqual(
    nodeResults.map(([label]) => label),
    qjsResults.map(([label]) => label),
    "the two hosts ran a different set of operations",
  );
});

test("every operation agrees between the N-API and QuickJS codecs", () => {
  for (let i = 0; i < nodeResults.length; i++) {
    const [label, nodeValue] = nodeResults[i];
    const [, qjsValue] = qjsResults[i];
    assert.deepStrictEqual(nodeValue, qjsValue, `mismatch on "${label}"`);
  }
});

test("binary crosses as Uint8Array, not an array of numbers", () => {
  // canon() records `kind: "u8"` only for a real Uint8Array; a degraded number[] would canonicalize
  // to a plain array and fail here. This is what the codec's NativeAstCodec opt-in buys.
  const byLabel = Object.fromEntries(nodeResults);
  for (const label of ["readFile", "readFilePrefix", "zip", "pngEncode"]) {
    assert.equal(byLabel[label]?.kind, "u8", `${label} did not return a Uint8Array`);
  }
  assert.equal(byLabel["unzip"][1].bytes.kind, "u8", "unzip entry bytes were not a Uint8Array");
  assert.equal(byLabel["pngDecode"].rgba.kind, "u8", "pngDecode rgba was not a Uint8Array");
});

test("binary survives a write/read round trip intact", async () => {
  const { payload, fnv1a } = await import("./ops.mjs");
  const byLabel = Object.fromEntries(nodeResults);
  const sent = payload();

  // ops.mjs wrote payload() and read it back: the bytes that came out of native must checksum to
  // the bytes that went in. This is the end-to-end binary fidelity check (Writer -> JS -> Reader).
  assert.equal(byLabel["readFile"].len, sent.length);
  assert.equal(byLabel["readFile"].hash, fnv1a(sent));

  // readFilePrefix(16) is the first 16 bytes of that same blob.
  assert.equal(byLabel["readFilePrefix"].len, 16);
  assert.equal(byLabel["readFilePrefix"].hash, fnv1a(sent.subarray(0, 16)));

  // The unzip entry carrying the blob must match it too (nested Bytestring in a vector<struct>).
  const blobEntry = byLabel["unzip"].find((e) => e.name === "b.bin");
  assert.equal(blobEntry.bytes.hash, fnv1a(sent));
  // zero-length binary round-trips as an empty Uint8Array, not null.
  assert.equal(byLabel["readFile:empty"].kind, "u8");
  assert.equal(byLabel["readFile:empty"].len, 0);
});

test("an absent std::optional maps to null on both hosts", () => {
  const byLabel = Object.fromEntries(nodeResults);
  assert.equal(byLabel["readFile:absent"], null);
});

test("error envelopes match", () => {
  const byLabel = Object.fromEntries(nodeResults);
  assert.equal(byLabel["error:unknownMethod"].error.code, -32601);
  assert.ok(byLabel["error:badParams"].error, "a bad param type should produce an rpc error");
});

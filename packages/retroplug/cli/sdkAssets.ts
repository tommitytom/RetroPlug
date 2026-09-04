// Materializing the test SDK next to a consumer's tests, from the copy embedded in the binary.
//
// A consumer used to carry `sdk/retroplug-cli.js` + `.d.ts` synced alongside the binary, which meant a
// copy could silently lag a newer binary - a test calling a method the synced SDK predates would fail
// with a confusing error rather than a clear one. The binary now owns both files: `test` / `run` write
// them out when they are missing or their stamp does not match, so they cannot go stale, and a consumer
// repo does not have to track them at all.
//
// The stamp is a content hash baked in at build time (tools/embed-sdk.mjs), so the check is a short
// string compare rather than rehashing ~1.7 MB on every run.

import type { HostBackend } from "../src/backend";

type AssetFn = (name: "js" | "d.ts" | "hash") => string | null;

function assets(): AssetFn | null {
  const g = globalThis as { __rp_sdkAsset?: AssetFn };
  return typeof g.__rp_sdkAsset === "function" ? g.__rp_sdkAsset : null;
}

/** Where the SDK must land, given the directory the stripped files were written to.
 *
 *  A test resolves `../sdk/retroplug-cli.js` relative to ITS OWN location, which is the OUTPUT dir - not
 *  the source dir. Those coincide for the default `.rp-test-build` sibling, but diverge the moment
 *  `--out` points somewhere else, so this must key off the output dir or an explicit `--out` run puts
 *  the SDK where nothing imports it. */
export function sdkDirFor(outDir: string): string {
  const clean = outDir.replace(/\/+$/, "");
  const slash = clean.lastIndexOf("/");
  return (slash < 0 ? "." : clean.slice(0, slash)) + "/sdk";
}

/**
 * Write the embedded SDK into `sdkDir` unless it is already current. Returns true if it wrote.
 *
 * No-ops when the host exposes no embedded SDK (a build without it), leaving whatever the consumer has
 * in place - so this can only ever add a working SDK, never break an existing one.
 */
export function ensureSdk(backend: HostBackend, sdkDir: string): boolean {
  const asset = assets();
  if (!asset) return false;

  const hash = asset("hash");
  if (!hash) return false;

  const stampPath = `${sdkDir}/.rp-sdk-stamp`;
  const jsPath = `${sdkDir}/retroplug-cli.js`;
  const dtsPath = `${sdkDir}/retroplug-cli.d.ts`;

  if (backend.fileExists(jsPath) && backend.fileExists(dtsPath)) {
    const stamp = backend.readFile(stampPath);
    if (stamp && new TextDecoder().decode(stamp).trim() === hash) return false; // current
  }

  const enc = new TextEncoder();
  const js = asset("js");
  const dts = asset("d.ts");
  if (js == null || dts == null) return false;

  // writeFile creates parent dirs on demand, so sdkDir needs no mkdir.
  if (!backend.writeFile(jsPath, enc.encode(js))) throw new Error(`could not write ${jsPath}`);
  if (!backend.writeFile(dtsPath, enc.encode(dts))) throw new Error(`could not write ${dtsPath}`);
  backend.writeFile(stampPath, enc.encode(hash + "\n"));
  return true;
}

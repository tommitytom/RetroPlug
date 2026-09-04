// Materializing the SDK's TYPE DECLARATIONS next to a consumer's tests, from the copy embedded in the
// binary.
//
// Only the `.d.ts`. The authoring layer itself never becomes a file: it is registered as the module
// `retroplug-cli` before a session evaluates (cli/main.cpp registerSdkModule), and QuickJS resolves a
// bare specifier from its loaded-module table before consulting the module loader, so there is nothing
// on disk to sync or let go stale.
//
// Declarations are the exception because editors and `tsc` read them from the filesystem; a test's
// `import ... from "retroplug-cli"` reaches this file through a tsconfig `paths` entry.
//
// The stamp is a content hash baked in at build time (tools/embed-sdk.mjs), so the check is a short
// string compare rather than rehashing ~1.7 MB on every run.

import type { HostBackend } from "../src/backend";

type AssetFn = (name: "d.ts" | "hash") => string | null;

function assets(): AssetFn | null {
  const g = globalThis as { __rp_sdkAsset?: AssetFn };
  return typeof g.__rp_sdkAsset === "function" ? g.__rp_sdkAsset : null;
}

/** Where the declarations go: alongside the SOURCE tests, since that is what an editor and tsc read.
 *  (Unlike the old runtime copy, this is not resolved by any import at run time, so it keys off the
 *  source dir rather than the output dir.) */
export function sdkDirFor(srcDir: string): string {
  const clean = srcDir.replace(/\/+$/, "");
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
  const dtsPath = `${sdkDir}/retroplug-cli.d.ts`;

  if (backend.fileExists(dtsPath)) {
    const stamp = backend.readFile(stampPath);
    if (stamp && new TextDecoder().decode(stamp).trim() === hash) return false; // current
  }

  const dts = asset("d.ts");
  if (dts == null) return false;

  const enc = new TextEncoder();
  // writeFile creates parent dirs on demand, so sdkDir needs no mkdir.
  if (!backend.writeFile(dtsPath, enc.encode(dts))) throw new Error(`could not write ${dtsPath}`);
  backend.writeFile(stampPath, enc.encode(hash + "\n"));
  return true;
}

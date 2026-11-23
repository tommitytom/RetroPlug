// The CLI dispatcher's packs (docs/downloadable-packs-m2c-plan.md): their cli halves, and the app and consoles halves a
// tool's session reads.
import "../tools";
import { cliToolsRegistry } from "../contributions/toolRegistries";
import { APP_BUILDERS, loadPacks, type HalfBuilder } from "../../src/packs/loadPacks";
import { nativePackHost, type HalfKind } from "../../src/packs/packHost";
import type { CliTool } from "@retroplug/core/cliTool";

export const CLI_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  ...APP_BUILDERS,
  cli: (pkg, exported) => {
    if (!Array.isArray(exported)) throw new Error("its cli half's default export is not a defineCliTools([...]) list");
    return [{ registry: cliToolsRegistry, pkg, items: exported as CliTool[] }];
  },
};

export function loadCliPacks(): Promise<void> {
  return loadPacks(nativePackHost(), CLI_BUILDERS);
}

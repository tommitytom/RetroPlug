// The UI's packs (docs/downloadable-packs-m2c-plan.md): their ui and views halves, and their app and consoles halves
// when no control plane loaded them first (the UI test harness). A commit re-renders the UI (M2a), so the load runs
// beside the first render rather than before it.
import "../contributions/menus";
import "../contributions/views";
import { uiRegistry, viewsRegistry } from "../contributions/uiRegistries";
import { APP_BUILDERS, loadPacks, objectExport, type HalfBuilder } from "../../src/packs/loadPacks";
import { nativePackHost, type HalfKind } from "../../src/packs/packHost";
import type { UiContribution, ViewsContribution } from "@retroplug/ui/contribution";

export const UI_BUILDERS: Partial<Record<HalfKind, HalfBuilder>> = {
  ...APP_BUILDERS,
  ui: (pkg, exported) => [{ registry: uiRegistry, pkg, items: [objectExport("ui", exported) as unknown as UiContribution] }],
  views: (pkg, exported) => [
    { registry: viewsRegistry, pkg, items: (objectExport("views", exported) as unknown as ViewsContribution).views ?? [] },
  ],
};

export function loadUiPacks(): Promise<void> {
  return loadPacks(nativePackHost(), UI_BUILDERS);
}

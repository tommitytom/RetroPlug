// The kinds fixture pack's app half (docs/downloadable-packs-m2f-plan.md): one controller app's control half, which the
// launchpad's registry offers in its menu as "TS Fixture App".
import { definePackage } from "@retroplug/core/definePackage";
import type {} from "@retroplug/launchpad/packageContributions";

export default definePackage({
  controllerApps: [{ id: "tsfix-app", label: "TS Fixture App", config: { parse: () => ({}) } }],
});

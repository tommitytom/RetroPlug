// The render fixture pack's app half (docs/downloadable-packs-m2d-plan.md): a feature role every ROM is given, whose
// construct hook says so on the console, so a render's console shows the worker loaded, committed and used the pack.
// The role has no protocol node and changes nothing of the system it is on, so the render's bytes are a render's
// without it.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{
    kind: "tsfix-render",
    category: "feature",
    schema: { parse: () => ({}) },
    onConstruct: (spec) => {
      console.log("tsfix-render: constructed");
      return spec;
    },
  }],
  romProviders: [() => [{ kind: "tsfix-render", config: {} }]],
});

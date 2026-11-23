// The TS fixture pack's app half (docs/downloadable-packs-m2c-plan.md): one feature role, with a schema of its own (no
// zod), which the UI's role check parses.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{
    kind: "tsfix",
    category: "feature",
    schema: { parse: (c: unknown) => ({ level: typeof (c as { level?: unknown })?.level === "number" ? (c as { level: number }).level : 3 }) },
  }],
});

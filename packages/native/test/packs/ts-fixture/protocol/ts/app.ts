// The protocol fixture pack's app half (docs/downloadable-packs-m2e-plan.md): the role its node is placed as, given to
// every ROM, and the project section its placer reads. The role writes no system input and changes nothing a render
// hears.
import { definePackage } from "@retroplug/core/definePackage";

export default definePackage({
  roles: [{ kind: "tsfix-proto", category: "feature", schema: { parse: () => ({}) } }],
  romProviders: [() => [{ kind: "tsfix-proto", config: {} }]],
  projectSections: [{ key: "tsfixProto", schema: { parse: () => ({}) }, defaults: () => ({}) }],
});

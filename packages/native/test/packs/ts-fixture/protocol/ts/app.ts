// The protocol fixture pack's app half (docs/downloadable-packs-m2e-plan.md): the role its node is placed as, given
// to every ROM, the project section its placer reads, and the service that publishes that section's model (a placer
// runs only for a model a service publishes). The role writes no system input and changes nothing a render hears.
import { definePackage } from "@retroplug/core/definePackage";
import type {} from "@retroplug/tracker/packageContributions";

export default definePackage({
  roles: [{ kind: "tsfix-proto", category: "feature", schema: { parse: () => ({}) } }],
  romProviders: [() => [{ kind: "tsfix-proto", config: {} }]],
  projectSections: [{ key: "tsfixProto", schema: { parse: () => ({}) }, defaults: () => ({}) }],
  projectServices: [{ key: "tsfixProto", create: () => ({ graphModel: () => ({}) }) }],
});

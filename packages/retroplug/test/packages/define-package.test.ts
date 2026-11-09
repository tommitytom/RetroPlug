// The app-contribution contract (packages/core/ts/definePackage.ts, docs/modular-graph-phase-f1-design.md
// section 3):
// definePackage is the identity with types, and the types are CHECKED rather than inferred. A generic parameter
// inferred from the literal would swallow a misspelt key - `romProvider` for `romProviders` - and the contribution
// would silently never register; the parameter's own type refuses it. The `@ts-expect-error` lines below are what
// holds that, since tsc fails an unused directive: if one of these calls compiles again, `pnpm typecheck` does.
//
// The four lists a package may contribute come from three owners: core declares `roles` and `romProviders`,
// @retroplug/tracker
// augments the interface with `trackerIntegrations` and @retroplug/dsp with `kitFormats`. Both augmentations are
// imported here so
// this file is typed on its own, not on some other module having pulled them into the program.
import { test, expect } from "@retroplug/testing";
import { definePackage } from "@retroplug/core/definePackage";
import { SAMEBOY_ROLE } from "@retroplug/sameboy";
import { MESEN_ROLE } from "@retroplug/mesen";
import type {} from "@retroplug/tracker/packageContributions";
import type {} from "@retroplug/dsp/packageContributions";
import { lsdjIntegration } from "@retroplug/lsdj/lsdjIntegration";
import { LSDJ_KIT_FORMAT } from "@retroplug/lsdj/kitFormat";

test("definePackage is the identity", () => {
  const roles = [SAMEBOY_ROLE, MESEN_ROLE];
  const contributions = definePackage({ roles });
  expect(contributions.roles === roles).toBe(true);
});

// Every owner's keys are part of the contract, so a package's app module may list all four kinds...
const accepted = definePackage({
  roles: [SAMEBOY_ROLE, MESEN_ROLE],
  trackerIntegrations: [lsdjIntegration],
  kitFormats: [LSDJ_KIT_FORMAT],
});

// ...and any other key is an excess property of the parameter's type.
const misspeltCoreKey = () =>
  definePackage({
    // @ts-expect-error: the contract's key is `roles`, not `role`
    role: [SAMEBOY_ROLE, MESEN_ROLE],
  });

const misspeltAugmentedKey = () =>
  definePackage({
    // @ts-expect-error: the tracker's key is `trackerIntegrations`, not `trackerIntegration`
    trackerIntegration: [lsdjIntegration],
  });

test("a contribution key's name is checked, not inferred", () => {
  console.log(`# the contract takes roles, romProviders, trackerIntegrations and kitFormats`);
  expect(typeof accepted).toBe("object");
  expect(typeof misspeltCoreKey).toBe("function");
  expect(typeof misspeltAugmentedKey).toBe("function");
});

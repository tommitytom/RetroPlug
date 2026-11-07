// The app-contribution contract (src/core/definePackage.ts, docs/modular-graph-phase-f1-design.md section 3):
// definePackage is the identity with types, and the types are CHECKED rather than inferred. A generic parameter
// inferred from the literal would swallow a misspelt key - `romProvider` for `romProviders` - and the contribution
// would silently never register; the parameter's own type refuses it. The `@ts-expect-error` lines below are what
// holds that, since tsc fails an unused directive: if one of these calls compiles again, `pnpm typecheck` does.
//
// The four lists a package may contribute come from two owners: core declares `roles` and `romProviders`, and the
// tracker and dsp modules augment the interface with `trackerIntegrations` and `kitFormats`.
import { test, expect } from "../../testing/harness";
import { definePackage } from "../../src/core/definePackage";
import { CORE_ROLES } from "../../src/coreRoles";
import { lsdjIntegration } from "../../src/tracker/integrations";
import { LSDJ_KIT_FORMAT } from "../../src/kit/compile";

test("definePackage is the identity", () => {
  const contributions = definePackage({ roles: CORE_ROLES });
  expect(contributions.roles === CORE_ROLES).toBe(true);
});

// Every owner's keys are part of the contract, so a package's app module may list all four kinds...
const accepted = definePackage({
  roles: CORE_ROLES,
  trackerIntegrations: [lsdjIntegration],
  kitFormats: [LSDJ_KIT_FORMAT],
});

// ...and any other key is an excess property of the parameter's type.
const misspeltCoreKey = () =>
  definePackage({
    // @ts-expect-error: the contract's key is `roles`, not `role`
    role: CORE_ROLES,
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

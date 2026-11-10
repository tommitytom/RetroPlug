// The aliases a bundle of the generator's fixture tree needs, in one place: test/packages/run-generator.mjs bundles
// the fixture tree's generated modules to run their checks, and test/script/bundle-views.mjs bundles them to prove
// the views split, and the two must name the same files or the second would test a tree the first does not.
//
// A fixture tree lives outside the workspace, so nothing stands in for the pnpm links a real build resolves: each
// fixture package's contribution is aliased by the specifier its manifest exports, the ui contract by the real one, and
// the fixture shell's src/*.ts shims by the real shell's binding vocabulary (an action's defaults are then checked
// against the real key names, pad names and shell defaults rather than against a copy of them). React is aliased by
// none of them, which is what the views split's absence case rests on.
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const SHELL = resolve(HERE, "../..");

/** The alias map for a fixture tree at `root`. */
export function fixtureAliases(root) {
  return {
    "@retroplug/alpha/app": join(root, "packages/alpha/ts/app.ts"),
    "@retroplug/beta/app": join(root, "packages/beta/ts/app.ts"),
    "@retroplug/alpha/consoles": join(root, "packages/alpha/ts/consoles.ts"),
    "@retroplug/beta/consoles": join(root, "packages/beta/ts/consoles.ts"),
    "@retroplug/alpha/ui": join(root, "packages/alpha/ts/ui/index.ts"),
    "@retroplug/beta/ui": join(root, "packages/beta/ts/ui/index.ts"),
    "@retroplug/zeta/ui": join(root, "packages/zeta/ts/ui/index.ts"),
    "@retroplug/alpha/views": join(root, "packages/alpha/ts/ui/views.ts"),
    "@retroplug/beta/views": join(root, "packages/beta/ts/ui/views.ts"),
    "@retroplug/zeta/views": join(root, "packages/zeta/ts/ui/views.ts"),
    // The ui package itself is a workspace dependency of the shell, not a fixture package: a real build resolves it
    // through the pnpm link, so the caller hands the bundle the tree's own copy.
    "@retroplug/ui/contribution": join(SHELL, "../ui/ts/contribution.ts"),
    "@retroplug/shell/keyCodes": join(SHELL, "src/keyCodes.ts"),
    "@retroplug/shell/bindingMap": join(SHELL, "src/bindingMap.ts"),
  };
}

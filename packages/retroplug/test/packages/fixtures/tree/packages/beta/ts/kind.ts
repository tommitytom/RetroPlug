// A fixture declaring package's control-plane half, standing in for a real pack's (test/packages/run-generator.mjs):
// the load-time `accept` that fills the package's own registry. A `check` is the declaring package's own rules over its
// data, which the fixture does not copy: the real controllerApps check has its messages tested by the package that owns
// them (packages/launchpad/ts/test/kind.test.ts), and this suite holds the generic mechanism over a kind that is not a
// real pack's (alpha's `widgets`).
const accepted: { id: string; label?: string }[] = [];

export function acceptedApps(): readonly { id: string; label?: string }[] {
  return accepted;
}

export default {
  accept: (items: readonly { id: string; label?: string }[]) => {
    for (const item of items) accepted.push(item);
  },
};

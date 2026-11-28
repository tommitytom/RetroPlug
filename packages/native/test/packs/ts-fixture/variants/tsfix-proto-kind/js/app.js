// A role placed as a protocol node, beside a kinds half: loaded live, every running Engine refuses the whole pack
// (its kind items wait for the next launch), so the role is registered in TS and its node is a type the Engine does
// not have.
export default {
  roles: [{ kind: "tsfix-proto-kind", category: "feature", schema: { parse: () => ({}) } }],
  controllerApps: [{ id: "tsfix-proto-kind-app", label: "TS Fixture Proto Kind App", config: { parse: () => ({}) } }],
};

// A fixture package's app contribution. The generator reads the manifest, not this file's contents, so an app module
// only has to exist for it; the loaded registry sees these objects, which is what the pairing checks read.
export default {
  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v: unknown) => v ?? {} } }],
  // A project section and a project service, which the generated registry collects in product order and refuses to see
  // twice (one key is one block of a .rplg, and one model of the graph).
  projectSections: [{ key: "alpha", schema: { parse: (v: unknown) => v }, defaults: () => ({}) }],
  projectServices: [{ key: "alpha", create: () => ({}) }],
};

// A fixture package's app contribution. The generator reads the manifest, not this file's contents, so an app module
// only has to exist for it; the loaded registry sees these objects, which is what the pairing checks read.
export default {
  controllerApps: [{ id: "alpha-app", label: "Alpha App", config: { parse: (v: unknown) => v ?? {} } }],
};

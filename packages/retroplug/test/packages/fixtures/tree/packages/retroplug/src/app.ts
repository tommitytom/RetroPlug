// A fixture package's app contribution. The generator reads the manifest, not this file's contents, so an app module
// only has to exist; a real one default-exports definePackage({ roles, romProviders, trackerIntegrations, kitFormats }).
// The shell closes the product, so its section and service are the last the generated lists carry.
export default {
  projectSections: [{ key: "shell", schema: { parse: (v) => v }, defaults: () => ({}) }],
  projectServices: [{ key: "shell", create: () => ({}) }],
};

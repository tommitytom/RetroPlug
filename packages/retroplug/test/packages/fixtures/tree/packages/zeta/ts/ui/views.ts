// A fixture package's views module: the generator reads the manifest, not this file's contents. It is data alone (a
// readout tile whose open draws nothing), so nothing here needs a bundler alias, and the generated module's checks are
// what a views mode exercises by rewriting this file.
export default {
  views: [{ id: "zeta-view", label: "Zeta View", placement: "tile", body: { kind: "readout", open: () => null } }],
};

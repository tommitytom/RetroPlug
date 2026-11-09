// A fixture package's UI contribution. The generator reads the manifest, not this file's contents: the menus module it
// emits imports this default export, and its load-time checks read the rows' ids and role kinds and the builders'
// placements, so a mode rewrites this file to exercise one refusal each.
export default {
  rows: [
    {
      id: "alpha-model",
      key: "model",
      label: "Model",
      role: "alpha-role",
      choices: [{ value: "a", label: "A" }],
    },
  ],
  menus: [{ placement: "instance.footer", build: () => [] }],
};

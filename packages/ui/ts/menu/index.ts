// The menu data model (docs/modular-graph-phase-f4b-design.md section 4.1): the item kinds a tree is built from and the
// builders that spell each one, moved unchanged out of the shell so a package's UI module and the shell share one
// definition. The React menu that draws them, and the shell's tree builders, are not here: this module is data, so it
// imports neither React nor the shell (the tree builders stay in packages/retroplug/ui/screens/menu).
export * from "./menuTree";
export * from "./menuItems";

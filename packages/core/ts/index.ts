// Core's TS half (docs/modular-graph-phase-f1-design.md section 2): the node contract every package's nodes are built
// with, the graph document's writer, the role contract and the app-contribution contract.
//
// This barrel is deliberately neutral: it holds the modules a package's node code or app module may need and nothing
// that would drag a console's policy into a MicroQuickJS device pack (so not the protocol nodes, the MIDI routing, the
// tick clock or the port capacities: a consumer of those imports their own module). A device pack imports the modules
// it uses by subpath (@retroplug/core/defineNode, /settingsSchema, /register), as its allowlist names.
export * from "./canonicalJson";
export * from "./constructSpec";
export * from "./defineNode";
export * from "./definePackage";
export * from "./descriptor";
export * from "./graphDocument";
export * from "./platform";
export * from "./register";
export * from "./settingsSchema";
export * from "./systemRoles";

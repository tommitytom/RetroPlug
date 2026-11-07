// The script-node bundle (docs/modular-graph-phase-b3a-design.md section 2.5): every TS node type the engine runs.
// The product's entry is the generated module (src/generated/scriptNodes.ts, from the packages' protocol members),
// which the build bundles and embeds (packages/native/cmake/bundles.cmake, retroplug-script-bundle); this path is the
// shell's fixed name for the same bundle, which test/script/bundle-no-zod.test.ts holds to a list without zod. An
// entry point only: importing it registers.
import "../generated/scriptNodes";

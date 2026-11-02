// The kit compilers as offline graph jobs (docs/modular-graph-phase-d2-design.md section 5): compile.ts runs a kit's
// slots, chain.ts builds each pass's document, pack.ts packs what the passes recorded.
export { compileKit, compileDmc, type KitCompileResult, type KitJobRunner } from "./compile";
export { KitSpecError } from "./chain";

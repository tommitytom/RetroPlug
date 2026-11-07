// dsp's TS half (docs/modular-graph-phase-f1-design.md section 2): the kit chain, which builds each pass's offline
// job document, the integer packing that turns what the jobs recorded into the bank's bytes, and the compiler contract
// the audio driver dispatches to a cart's format by.
//
// The compilers' shared half is kitJobs.ts: decode and window a source, run and hold every pass, start each honoured
// slot at once. Each cart's format is its package's kitFormat.ts (it knows its own chain and bank assembly), registered
// in the cart's app module and checked against packageContributions, which augments core's PackageContributions. The
// shell's src/kit dispatches by the id a format names. The shape of the job they run is the engine's, in core
// (offlineJob.ts).
export * from "./chain";
export * from "./kitContract";
export * from "./kitEffect";
export * from "./kitJobs";
export * from "./pack";

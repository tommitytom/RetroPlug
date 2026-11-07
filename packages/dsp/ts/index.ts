// dsp's TS half (docs/modular-graph-phase-f1-design.md section 2): the kit chain, which builds each pass's offline
// job document, the integer packing that turns what the jobs recorded into the bank's bytes, and the compiler contract
// the audio driver dispatches to a cart's format by.
//
// The compilers themselves are the shell's still (packages/retroplug/src/kit/compile.ts): they assemble the carts'
// banks through lsdj's and risa's ROM code, which are the carts' own. The kit formats a cart contributes are registered
// in its app module and checked against packageContributions, which augments core's PackageContributions. The shape of
// the job they run is the engine's, in core (offlineJob.ts).
export * from "./chain";
export * from "./kitContract";
export * from "./kitEffect";
export * from "./pack";

// The kit compilers' door (docs/modular-graph-phase-d2-design.md section 5): a kit bank's compile is its cart package's
// format (an LSDj 16 KB rotated-nibble kit is @retroplug/lsdj/kitFormat's, a risa 8 KB DPCM kit is
// @retroplug/risa/kitFormat's), and a compile is a chain of offline graph jobs over the host. Native decodes, windows
// and does every float the bytes depend on (the effects, the resample, the dither, the DMC encode, each a node in a
// job's document, @retroplug/dsp/chain); TypeScript builds the documents, cuts the buffers to their lengths, packs the
// codes and bits (@retroplug/dsp/pack) and assembles the bank.
//
// The shared half of a compile is @retroplug/dsp/kitJobs' (the window, the passes, the refusals, the slots started at
// once). Which formats exist is the product's (each cart's app contribution registers its own,
// src/generated/packages.ts), so the id lookup and the driver that dispatches through it are the shell's
// (src/kitFormats.ts), and nothing here reads the registry: a compiler is handed the format it compiles.

// The compilers and the contract they share live in their packages, re-exported here so a caller of this door has one
// spelling for them.
export { compileLsdjKit, LSDJ_KIT_FORMAT } from "@retroplug/lsdj/kitFormat";
export { compileRisaKit, RISA_KIT_FORMAT } from "@retroplug/risa/kitFormat";
export { NO_SLOT_REASON } from "@retroplug/dsp/kitJobs";
export type { KitCompileResult, KitJobRunner } from "@retroplug/dsp/kitContract";
export type { KitFormat } from "@retroplug/dsp/kitContract";

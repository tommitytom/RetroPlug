// The kit compilers as offline graph jobs (docs/modular-graph-phase-d2-design.md section 5): compile.ts runs a kit's
// slots here, and dsp's chain builds each pass's document and its pack packs what the passes recorded.
//
// Which formats exist is the product's (each cart's app contribution registers its own, src/generated/packages.ts), so
// the id lookup and the driver that dispatches through it are the shell's (src/kitFormats.ts), and nothing here reads
// the registry: a compiler is handed the format it compiles.
export {
  compileLsdjKit,
  compileRisaKit,
  LSDJ_KIT_FORMAT,
  RISA_KIT_FORMAT,
  NO_SLOT_REASON,
  type KitCompileResult,
  type KitJobRunner,
} from "./compile";
export { KitSpecError } from "@retroplug/dsp";

// The kit chain's effect spec (docs/modular-graph-phase-d2-design.md section 5), as a kit compiler's spec carries it:
// discriminated on `type`, applied per sample in the chain's order. A field a spec leaves out takes the node's default
// (chain.ts). The shell's audioDriver re-exports these types, so its compile specs and the kit callers keep one spelling.
export type KitGainEffect = { type: "gain"; normalize?: boolean; gain?: number };
export type KitFilterEffect = {
  type: "filter";
  filterType?: "LowPass" | "HighPass" | "BandPass" | "BandStop" | "Peak" | "LowShelf" | "HighShelf" | "AllPass";
  frequency?: number;
  q?: number;
  gain?: number;
};
export type KitDitherEffect = {
  type: "dither";
  ditherType?: "HighPassTPDF" | "ShapedTPDF" | "ErrorDiffusion" | "JJN" | "SierraLite";
};
export type KitEffect = KitGainEffect | KitFilterEffect | KitDitherEffect;

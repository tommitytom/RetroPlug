// The NES DMC kit-compile spec (docs/modular-graph-phase-f4b-design.md section 5.2): one source sample per slot, and a
// whole bank. It lives in tracker because the format is SHARED - risa's kit format compiles it, and BlipToaster's CLI
// tool compiles the same 8 KB bank through that format by its id - so a package that is not risa can name it without
// depending on risa. The types are dsp's KitSampleSpec plus the DMC slot's own knobs.
import type { KitSampleSpec } from "@retroplug/dsp/kitContract";

/** One source sample for the DMC compiler: a kit sample plus the PAL DPCM playback-rate index (0..15, default 12), a
 *  loop flag, and whether to peak-normalize into the 7-bit domain (default true). */
export interface NesDmcSampleSpec extends KitSampleSpec {
  rate?: number;
  loop?: boolean;
  normalize?: boolean;
}

/** A whole DMC kit compile: a 6-char kit name and up to 16 samples. */
export interface NesDmcKitSpec {
  name: string;
  samples: NesDmcSampleSpec[];
}

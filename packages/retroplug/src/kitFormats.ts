// The kit bank formats this product compiles (docs/modular-graph-phase-f1-design.md section 3): which formats exist is
// the packages' business (each cart's app contribution registers its own, src/generated/packages.ts), so the id to
// format lookup is the shell's, beside src/romProviders.ts and src/trackerRegistry.ts, and the kit compile driver
// (src/audioDriver.ts' compileKitFormat) compiles the format it is handed.
import { KIT_FORMATS } from "./generated/packages";
import type { KitFormat } from "@retroplug/dsp/kitContract";

/** The product's kit formats, in package order: what kitFormat resolves an id through. */
export { KIT_FORMATS };

/** The kit bank format `id` names, as the product's packages registered it; a driver refuses an id no package
 *  registers, naming it, rather than compiling a bank nothing assembles. */
export function kitFormat<S>(id: string): KitFormat<S> {
  return resolveKitFormat(id, KIT_FORMATS) as KitFormat<S>;
}

/** The format `id` names among `formats`, or refused: an id no format names, or one two formats name (two packages
 *  would otherwise compile their bank or the other's by the list's order alone). Exported so the refusal is held
 *  without two packages actually registering one id. */
export function resolveKitFormat(id: string, formats: readonly KitFormat<any>[]): KitFormat<any> {
  const named = formats.filter((f) => f.id === id);
  if (named.length > 1) throw new Error(`two packages register the kit format "${id}"`);
  if (!named.length) throw new Error(`no package registers the kit format "${id}"`);
  return named[0];
}

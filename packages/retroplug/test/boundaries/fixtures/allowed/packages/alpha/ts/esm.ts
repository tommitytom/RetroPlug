// A specifier that spells a module's emitted name resolves to the source beside it, as `tsc`'s bundler resolution and
// the bundlers do: "./types.js" names types.ts, and "./types.mjs" names types.mts.
import type { Iface } from "./types.js";
import { ifaceModule } from "./types.mjs";

export const esm = (value: Iface): number => value.x + ifaceModule;

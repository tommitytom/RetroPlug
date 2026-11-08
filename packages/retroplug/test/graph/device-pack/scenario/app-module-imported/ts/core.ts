// The package's portable core, importing the app module: the control plane's half (zod, the stores) may not reach a core,
// and a pack's bundle would carry it onto the engine.
import { half } from "./app";

export function whole(n: number): number {
  return half(n) + 1;
}

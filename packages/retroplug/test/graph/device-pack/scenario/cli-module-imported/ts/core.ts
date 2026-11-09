// The package's portable core, reaching the CLI half: the dispatcher's code would ride a pack's bundle onto the engine.
import { half } from "./cli";

export function whole(n: number): number {
  return half(n) + 1;
}

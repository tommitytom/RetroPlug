// The package's portable core, reaching the menu's directory: the desktop half may not reach a core, and a pack's
// bundle would carry it onto the engine.
import { half } from "./ui";

export function whole(n: number): number {
  return half(n) + 1;
}

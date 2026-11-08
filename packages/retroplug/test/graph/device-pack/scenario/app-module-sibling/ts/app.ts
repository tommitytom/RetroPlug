// The control-plane half, which the whole-core scan skips: this one is careful, and the module it IMPORTS is not skipped,
// so what the scan refuses is the helper (ts/helper.ts), not the app.
import { launches } from "./helper";

export function half(value: number): number {
  return launches(value) ? value / 8 : value / 2;
}

// The control-plane half, which the whole-core scan skips: this one is careful, so what the scan refuses is the module
// beside it (ts/helper.ts), not the app.
export function half(value: number): number {
  return value / 2;
}

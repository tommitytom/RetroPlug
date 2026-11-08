// The control-plane half, the module the manifest names as `app`: the whole-core scan skips it, and a core module
// importing it is the mistake the toolchain refuses by name.
export function half(value: number): number {
  return value / 2;
}

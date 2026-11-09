// The desktop CLI half, the module the manifest names as `cli`: a core module importing it is what the toolchain refuses.
export function half(value: number): number {
  return value / 2;
}

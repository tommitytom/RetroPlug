// The package's portable core, which the scan does see: a whole-number check the ES5 way, as a device pack must write it.
export function whole(n: number): boolean {
  return typeof n === "number" && Math.floor(n) === n;
}

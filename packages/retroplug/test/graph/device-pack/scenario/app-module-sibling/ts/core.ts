// The package's portable core, scanned like any other module of it.
export function whole(n: number): boolean {
  return typeof n === "number" && Math.floor(n) === n;
}

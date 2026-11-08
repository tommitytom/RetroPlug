// A core module whose missing call nothing in the scenario reaches: the whole-core scan is the only guard a path
// neither the scenario nor a node runs has.

/** Whole-number check the ES5 way, as the portable core has to write it. */
export function whole(n: number): boolean {
  return typeof n === "number" && Math.floor(n) === n;
}

/** The launchable row check, which MicroQuickJS would fail on: it has no Number.isInteger. Nothing calls it. */
export function launchable(row: number): boolean {
  return Number.isInteger(row) && row >= 0 && row <= 253;
}

// A core module, so the toolchain reaches the run this fixture is about.
export function payload(): { [key: string]: number } {
  const key = "__proto__";
  const out: { [key: string]: number } = {};
  out[key] = 1;
  return out;
}

// A package's own interface, imported without the `type` keyword: TypeScript's scanner lists it all the same, and it
// stays inside the package.
export interface Iface {
  x: number;
}

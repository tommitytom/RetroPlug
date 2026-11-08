// A core module, so the toolchain reaches the run this fixture is about.
export function broken(): never {
  throw new Error("the cart is not connected");
}

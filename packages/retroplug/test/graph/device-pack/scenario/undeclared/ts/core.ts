// A core module, so the toolchain gets as far as the allowlist check this fixture is about.
export const name = "undeclared";

export function greet(): string {
  return "hello " + name;
}

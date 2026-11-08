// A package's control-plane half: the manifest's `app` module, the one module the toolchain's whole-core scan skips,
// because it is the desktop's (it carries the app's zod schema, which the engine has none of). A call the engine lacks
// is fine HERE and nowhere else, so this module calls one.
export function whole(row: number): boolean {
  return Number.isInteger(row);
}

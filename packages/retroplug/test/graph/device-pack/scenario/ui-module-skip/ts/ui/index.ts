// A package's UI half: the manifest's `ui` module, which the toolchain's whole-core scan skips, because a menu's code is
// the DESKTOP's (it reaches @retroplug/ui and the host, which the engine has none of). A call the engine lacks is fine
// HERE and in the rest of ts/ui, so this module calls one.
export function shown(row: number): boolean {
  return Number.isInteger(row);
}

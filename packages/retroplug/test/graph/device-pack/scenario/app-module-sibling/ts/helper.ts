// A module a control-plane half IMPORTS: the scan sees it, so a helper of the app's may not call what the engine lacks
// either. The rule this pins: every zod-using control-plane module must BE the manifest's `app` module, not a module it
// imports.
export function launches(row: number): boolean {
  return Number.isInteger(row);
}

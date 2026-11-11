// The control surface's config section (docs/modular-graph-phase-f5b-design.md sections 6.1 and 6.2): the machine's
// port pair, where it lives in config.json. The key, its four fields and their order are the ones config.json always
// carried, so the pack, not the shell, decides what a control surface's stored state is.
//
// HOST state rather than project state: port names belong to the machine, and one process holds one device for every
// project it has open. The port names are opaque - often an interface's, not the device's, since a Launchpad also
// speaks TRS/DIN - and `device` is what a scan read off the hardware ("" = never scanned), shown and never
// interpreted.
//
// The parse is written out rather than made of zod fields, as the pack's project section is: the section contract asks
// only for a `parse`, and junk snaps to the default instead of failing the whole file.
import type { ProjectSection } from "@retroplug/core/definePackage";

export interface ControlSurfaceSettings {
  input: string; // "" = none chosen
  output: string;
  enabled: boolean; // the user's Connect: claim the pair now, and again at the next launch
  device: string;
}

/** The section's root key in config.json, which is also what the shell's `config` host group reads it by. */
export const CONTROL_SURFACE_SECTION_KEY = "controlSurface";

const isObject = (v: unknown): v is Record<string, unknown> =>
  v !== null && typeof v === "object" && !Array.isArray(v);

const stringOf = (v: unknown): string => (typeof v === "string" ? v : "");

function parseControlSurface(raw: unknown): ControlSurfaceSettings {
  const block = isObject(raw) ? raw : {};
  return {
    input: stringOf(block.input),
    output: stringOf(block.output),
    enabled: block.enabled === true,
    device: stringOf(block.device),
  };
}

/** The section the generated registry hands the shell: no `version` (nothing about it has needed a migration), so its
 *  bytes are what they always were and a block this build does not understand is kept as it came. */
export const controlSurfaceSection: ProjectSection<ControlSurfaceSettings> = {
  key: CONTROL_SURFACE_SECTION_KEY,
  schema: { parse: parseControlSurface },
  defaults: () => parseControlSurface({}),
};

// The controller's project section (docs/modular-graph-phase-f5a-design.md section 6.1): the block a .rplg keeps under
// `settings.controller`, which the Launchpad package owns. The key, its contents and its bytes are the ones the file
// always carried (`controller` stays where it was), so a project that has one loads as it always did and this
// module only says how it is parsed and what a patch does to it.
//
// The parse is written out rather than made of zod fields: the control plane's section contract asks only for a
// `parse`, and this section's schema is small enough to spell (the shapes zod's field builders give it: junk snaps
// to the default instead of failing the whole settings object, which is what keeps a malformed block from resetting the
// project's layout and zoom).
import type { ProjectSection } from "@retroplug/core/definePackage";
import { CONTROLLER_TARGET_VALUES, type ControllerTarget } from "../enums";
import { defaultControllerApp } from "../registry";

/** The control-surface session a project stores: which app runs, where its launches go and the app's own config. Only
 *  the user's CHOICES live here; the derived song-timing table the predictor needs is built at publish time and never
 *  persisted. */
export interface ControllerSection {
  enabled: boolean;
  /** A controller-app id, e.g. "lsdj-midimap" (the first the product registers is the default). */
  app: string;
  target: ControllerTarget;
  /** 0 = the first system. */
  systemId: number;
  /** The chosen app's OWN knobs (quantise, follow, ...), validated by that app's schema rather than here: the registry
   *  owns their shape, so a new app needs no change to the project's. Small readable JSON, persisted inline like any
   *  other setting. */
  appConfig: Record<string, unknown>;
}

/** The section's key under `settings`, and the model key its placer and service read. */
export const CONTROLLER_SECTION_KEY = "controller";

const isObject = (v: unknown): v is Record<string, unknown> =>
  v !== null && typeof v === "object" && !Array.isArray(v);

/** A raw block as an object: a malformed one (null, an array, a number) reads as an empty block and takes the defaults,
 *  rather than failing the whole settings object. */
const blockOf = (raw: unknown): Record<string, unknown> => (isObject(raw) ? raw : {});

/** The block as the session reads it: defaults filled, the target snapped to a launch target this build knows, the
 *  system id a clamped integer. Never throws on junk. */
export function parseControllerSection(raw: unknown): ControllerSection {
  const block = blockOf(raw);
  const target =
    typeof block.target === "string" && (CONTROLLER_TARGET_VALUES as readonly string[]).includes(block.target)
      ? (block.target as ControllerTarget)
      : "system";
  const systemId =
    typeof block.systemId === "number" && Number.isFinite(block.systemId) ? Math.round(block.systemId) : 0;
  return {
    enabled: typeof block.enabled === "boolean" ? block.enabled : false,
    app: typeof block.app === "string" ? block.app : defaultControllerApp(),
    target,
    systemId: Math.max(0, Math.min(0x7fffffff, systemId)),
    appConfig: { ...blockOf(block.appConfig) },
  };
}

/** A patch merged into the section: a patch naming a different app starts from the patch's own `appConfig`, because the
 *  old app's keys mean nothing to the new one and merging kept them in the .rplg forever; the same app merges one knob
 *  at a time, so a menu row can set one without knowing the others. A `target` that is not a launch target THROWS,
 *  naming the value: the patch would be written into the live settings, the published document and the next save
 *  unread, so a bad value would be a controller that launches nowhere - a package's bug, refused loudly rather than
 *  dropped. */
export function mergeControllerSection(
  current: ControllerSection,
  patch: Readonly<Record<string, unknown>>,
): ControllerSection {
  const switching = patch.app !== undefined && patch.app !== current.app;
  const appConfig = switching
    ? { ...blockOf(patch.appConfig) }
    : { ...current.appConfig, ...blockOf(patch.appConfig) };
  const merged = { ...current, ...patch, appConfig };
  if (typeof merged.target !== "string" || !(CONTROLLER_TARGET_VALUES as readonly string[]).includes(merged.target)) {
    throw new Error(
      `controller target '${String(merged.target)}' is not one of ${CONTROLLER_TARGET_VALUES.join(", ")}`,
    );
  }
  return parseControllerSection(merged);
}

/** The section the generated registry hands the shell: no `version`, because nothing about it has ever needed a
 *  migration, so a project's bytes for a controller are what they always were (an unstamped section is not stamped). */
export const controllerSection: ProjectSection<ControllerSection> = {
  key: CONTROLLER_SECTION_KEY,
  schema: { parse: parseControllerSection },
  defaults: () => parseControllerSection({}),
  merge: mergeControllerSection,
};

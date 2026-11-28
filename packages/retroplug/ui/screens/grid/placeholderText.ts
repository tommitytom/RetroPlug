// A placeholder tile's lines (docs/downloadable-packs-m3-design.md section 8), as pure data the tile renders and the
// pure tier checks: the platform's label (its id when no console registers it, the usual case), the ROM's file name
// (or the embedded ROM's id), then each distinct reason its needs give. Every string comes from the view and its needs.
import type { SystemView } from "../../../src/systemsStore";
import { registeredPlatforms } from "../../../src/consoles";
import { basename } from "../../../src/pathUtil";
import { needText } from "../../../src/packs/needs";

export function placeholderLines(view: SystemView): string[] {
  const label = registeredPlatforms().find((p) => p.id === view.platform)?.label ?? view.platform;
  const rom = basename(view.romPath) || view.embeddedRom;
  const reasons = [...new Set((view.placeholder?.needs ?? []).map(needText))];
  return [label, ...(rom ? [rom] : []), ...reasons];
}

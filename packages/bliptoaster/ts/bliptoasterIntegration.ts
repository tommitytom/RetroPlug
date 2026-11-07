// BlipToaster's tracker integration: an NES MIDI synth (not a tracker), so it has NO song battery - only ROM assets (a
// baked DMC kit plus the CHR font). An asset-only integration: `songs` is omitted, so only the asset submenus are built.
// The `bliptoaster` marker role its provider attaches is the menu's gate, exactly as "risa" gates risa's menu.
import type { TrackerIntegration } from "@retroplug/tracker/trackerIntegration";
import { bliptoasterAssetCatalog } from "./bliptoasterAssetCatalog";
import { blipToasterInfo, BLIPTOASTER_MARKER } from "./romDetect";

export const blipToasterIntegration: TrackerIntegration = {
  id: "bliptoaster",
  label: "BlipToaster",
  markerRole: "bliptoaster",
  assets: bliptoasterAssetCatalog,
  romName: (rom) => {
    const info = blipToasterInfo(rom); // decoded bliptoaster SIG block (null when the marker is absent)
    if (!info) return null;
    return `${BLIPTOASTER_MARKER} v${info.semver.join(".")}`; // marker doubles as the display name
  },
};

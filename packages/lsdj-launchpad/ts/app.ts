// The LSDj bridge's contribution to the app (docs/modular-graph-phase-f3-design.md sections 2 and 4): the control
// plane's half of the MI.MAP controller app - its id, the menu's label and the knobs' schema. The runtime half (the app,
// its playback model and its launch target) is the package's `protocol.apps` entry, ts/appRuntime.ts, and the two are
// joined by id. Which sync mode the cart those launches drive must be in is the same entry's `syncOverride` data
// (package.json), which graph/controller.ts applies: data, not code, because the planner must carry no app and no zod.
//
// The direction is the reason this package exists: neither lsdj nor launchpad knows the other, so neither names this
// app. The bridge imports both, contributes the app to the app's registry and to the script bundle's, and the control
// plane's schema is here too - so the control plane has the schema and the bundle has none of it.
import { definePackage } from "@retroplug/core/definePackage";
import { z } from "zod";
import { boolField, clampedInt, enumField } from "@retroplug/core/configFields";
import type { ControllerAppContribution } from "@retroplug/launchpad/registry";
import { MAX_PAGE, QUANTISE_VALUES } from "./lsdjMidiMap";
// The key the launchpad package adds to the contract (its ts/packageContributions.ts augments it): type-only, so nothing
// of it loads at run time.
import type {} from "@retroplug/launchpad/packageContributions";

/** MIDI note rows 254 and 255 are the MI.MAP handshake and clock sentinels, so the page's last row is one short. */
const MI_MAP: ControllerAppContribution = {
  id: "lsdj-midimap",
  label: "LSDj MI.MAP",
  // quantise: when a pressed pad's launch actually fires. Default `bar` - one bar is also exactly one LSDj phrase at the
  // factory groove, so launches land where the music does. follow: scroll the 16-row window to keep the playhead
  // visible. page: which window to start on.
  config: z.object({
    quantise: enumField(QUANTISE_VALUES, "bar"),
    follow: boolField(true),
    page: clampedInt(0, MAX_PAGE, 0),
  }),
};

export default definePackage({
  controllerApps: [MI_MAP],
});

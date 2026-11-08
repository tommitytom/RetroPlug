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
import { MAX_PAGE, QUANTISE_VALUES, type Quantise } from "./lsdjMidiMap";
// The key the launchpad package adds to the contract (its ts/packageContributions.ts augments it): type-only, so nothing
// of it loads at run time.
import type {} from "@retroplug/launchpad/packageContributions";

/** The row's text per quantise value: the words a player reads, which only this app has. */
const QUANTISE_LABELS: Record<Quantise, string> = {
  immediate: "Immediately",
  beat: "Next beat",
  bar: "Next bar",
  rowEnd: "End of row",
};

/** The control plane's half of MI.MAP: the id a project stores, the menu's label and the knobs' schema. */
const MI_MAP: ControllerAppContribution = {
  id: "lsdj-midimap",
  label: "LSDj MI.MAP",
  // quantise: when a pressed pad's launch actually fires. Default `bar` - one bar is also exactly one LSDj phrase at the
  // factory groove, so launches land where the music does. follow: scroll the 16-row window to keep the playhead
  // visible. page: which window to start on.
  config: z.object({
    quantise: enumField(QUANTISE_VALUES, "bar"),
    follow: boolField(true),
    // MIDI note rows 254 and 255 are the MI.MAP handshake and clock sentinels, so the page's last row is one short.
    page: clampedInt(0, MAX_PAGE, 0),
  }),
  // The rows the Launchpad submenu offers for those knobs, in order: the menu builds them from this list and the
  // schema's own defaults, so the shell names no app, no knob and no value. `page` has no row - it is the window the
  // session opens on, which the session's own paging moves.
  menu: [
    {
      key: "quantise",
      label: "Launches",
      // Built from the enum, so a value the schema gained cannot go missing from the row (the labels are the app's words).
      choices: QUANTISE_VALUES.map((value) => ({ value, label: QUANTISE_LABELS[value] })),
    },
    { key: "follow", label: "Follow Playhead", toggle: true },
  ],
};

export default definePackage({
  controllerApps: [MI_MAP],
});

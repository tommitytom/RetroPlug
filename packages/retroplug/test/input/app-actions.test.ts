// One press's app actions (docs/modular-graph-phase-f4c-design.md section 5.5): the shell's three and each package's
// contributed ones, dispatched in the order the resolved list gives, with one action's throw isolated from the rest.
// No display and no React: the dispatch is a function, and App only hands it the ids a key or button resolved to.
import { test, expect } from "@retroplug/testing";
import type { UiAction, UiHost } from "@retroplug/ui/contribution";

import { warned } from "../_warnings";
import { runActions, type ShellActions } from "../../ui/input/appActions";

/** The host a contributed action receives: none of the fakes read it. */
const host = {} as UiHost;

/** A shell body per id that records itself, plus the contributed actions under test. */
function press(): { log: string[]; shell: ShellActions } {
  const log: string[] = [];
  return {
    log,
    shell: {
      OpenMenu: () => log.push("OpenMenu"),
      CycleNext: () => log.push("CycleNext"),
      CyclePrev: () => log.push("CyclePrev"),
    },
  };
}

function action(id: string, log: string[]): { pkg: string; action: UiAction } {
  return { pkg: id.split(".")[0]!, action: { id, label: id, run: () => log.push(id) } };
}

test("a press runs its ids in order: the shell's actions and the contributed ones alike", () => {
  const { log, shell } = press();
  const contributed = [action("lsdj.debug-overlay", log), action("risa.debug-overlay", log)];
  // One backtick carries both overlays' actions; the list is the profile's order.
  runActions(["lsdj.debug-overlay", "risa.debug-overlay"], shell, contributed, host, 7);
  expect(log).toEqual(["lsdj.debug-overlay", "risa.debug-overlay"]);
  runActions(["CycleNext", "lsdj.debug-overlay"], shell, contributed, host, 7);
  expect(log).toEqual(["lsdj.debug-overlay", "risa.debug-overlay", "CycleNext", "lsdj.debug-overlay"]);
});

test("a contributed action gets the focused system, and null on the start screen", () => {
  const seen: (number | null)[] = [];
  const contributed = [
    {
      pkg: "lsdj",
      action: {
        id: "lsdj.debug-overlay",
        label: "LSDj Debug Overlay",
        run: (_h: UiHost, ctx: { systemId: number | null }) => seen.push(ctx.systemId),
      },
    },
  ];
  const { shell } = press();
  runActions(["lsdj.debug-overlay"], shell, contributed, host, 3);
  runActions(["lsdj.debug-overlay"], shell, contributed, host, null);
  expect(seen).toEqual([3, null]);
});

test("an id neither the shell nor the registry knows is skipped", () => {
  const { log, shell } = press();
  // A profile keeps the id of an action no product registers any more, as it can keep an unknown one today: skipped,
  // and no throw on the way out.
  runActions(["gone.action", "CyclePrev"], shell, [], host, 1);
  expect(log).toEqual(["CyclePrev"]);
});

test("a throwing action is reported and the rest of its press still runs", () => {
  const { log, shell } = press();
  const contributed = [
    {
      pkg: "lsdj",
      action: {
        id: "lsdj.debug-overlay",
        label: "LSDj Debug Overlay",
        run: () => {
          throw new Error("the overlay is broken");
        },
      },
    },
  ];
  const { warnings } = warned(() => runActions(["lsdj.debug-overlay", "CycleNext"], shell, contributed, host, 2));
  expect(warnings).toEqual(["[actions] lsdj.debug-overlay: its run threw: the overlay is broken"]);
  expect(log).toEqual(["CycleNext"]); // the shell's action on the same press still ran
});

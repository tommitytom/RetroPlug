// The menus held to what the OLD code built (docs/modular-graph-phase-f4b-design.md section 7.5, item 1), for the whole
// matrix the case file describes: every console, every tracker cart, a project that drives an N8 and one with a
// Launchpad running MI.MAP, a two-system project (the Link Group row), the start menu and the settings menus, a
// standalone host with a control-surface scan idle and in flight, the trackers with songs and a working song, an
// override and a pinned BlipToaster, a tracker whose ROM cannot be read, and the role and controller configs a
// hand-edited project can hold, a project whose roles lack the system's own core role among them (the one case recorded
// from the NEW code: its record is design section 6's E13 edge, and base's record of the same case showed the six core
// rows the old menu drew as dead rows - setRoleConfig refuses an absent role).
//
// ./recorded-matrix.json was recorded from 8a74339b9's menu code before F4b moved any of it: a `git archive 8a74339b9`
// tree, over the same ./recorded-cases.ts this test replays, printed `collect()`'s JSON from a throwaway recorder test
// that stayed outside the repository. Each case's tree carries every item's id, kind, label, `disabled` flag, children
// and an inline row's verbs in order; each cycler carries its starting label and, per step, the label it shows
// afterwards beside the project state the step wrote (a diff of the systems' role configs, the project settings and the
// user config) and the node MIDI it staged (by node position, and the bytes), so a row whose choice list is paired with
// the wrong values, whose step writes another value than the old one did, or whose step aims a message at another node
// fails here even though its labels read the same. Each case's card rows also carry the file dialogs they open (the
// N8's Load ROM filter among them), recorded from the same old tree and held here.
// RE-RECORDING (a deliberate behaviour change, never a refactor: re-taking a record to hide a diff is the one thing this
// file cannot be used for). The record is taken from the OLD tree, so it means running THIS file's CASES over that
// commit's menu code. As it was done for 8a74339b9:
//   1. `git archive 8a74339b9 | tar -x -C /tmp/rec` and link `/tmp/rec/node_modules` at this worktree's (the runner's
//      esbuild and typescript-5 resolve through it).
//   2. Copy ./recorded-cases.ts into `/tmp/rec/packages/retroplug/test/menu/` and repoint its four base-relative
//      imports: `@retroplug/ui/menu` -> `../../ui/screens/menu/menuTree`, `@retroplug/n8/ui` -> that tree's
//      `../../ui/screens/menu/n8SdOps` and `n8Devices`, `@retroplug/launchpad/ui` -> its
//      `../../ui/screens/menu/launchpadDevices`.
//   3. Copy this worktree's `test/systems/fixtures.ts` and `testing/mockBackend.ts` over base's. They carry two
//      additions the cases need and nothing else: `MockBackend.unseed`, and an SMS/GG region nibble in the Sega fixture.
//   4. Add a throwaway case there that prints `JSON.stringify(collect()[name])` over every case, and run it from
//      `/tmp/rec/packages/retroplug` as `RETROPLUG_TJS=<this worktree's build/dpfjs/.../txiki/tjs> node
//      scripts/run-tests.mjs menu/<recorder>`.
//   5. Paste the printed objects into ./recorded-matrix.json (JSON with an indent of 1 and one trailing newline) and
//      delete the recorder. Replay it here first: a deliberate change makes exactly the cases it changes fail.
// A case added on purpose AFTER the old commit (hand-no-core-role) cannot be recorded from it: its record is the new
// code's, and the design section 6 names it as the edge.
// Parts of the record are DELIBERATE SPLICES from a newer tree, which the recipe above does not cover: every case's two
// bindings menus carry F4c's four action rows (its display edge E1), and hand-controller's `lp-app-quantise` cycler
// carries F5a's starting label and step order (its display edge E1). Each was spliced from a throwaway recorder run on
// the tree that changed it, so replaying this file over the old tree fails exactly those rows, as it should.

import { test, expect } from "@retroplug/testing";
import recorded from "./recorded-matrix.json";
import { CASES, collect, type RecordedMatrix } from "./recorded-cases";

// txiki's file write, declared as the runner's other tests do.
declare const tjs: { env: Record<string, string | undefined>; writeFile(path: string, data: string): Promise<void> };

// One record for the whole file: rebuilding the matrix is the expensive part, and every case's own expectations are
// independent of the others (a case builds its own stores and stubs its own facets).
let recordedNow: RecordedMatrix | undefined;
function matrix(): RecordedMatrix {
  return (recordedNow ??= collect());
}

// A re-record is taken ONE NAMED CASE AT A TIME: `RP_MENU_MATRIX_UPDATE=<case>[,<case>...] pnpm test menu/recorded-trees`
// rewrites exactly the cases the variable names (and refuses a name the file does not have), leaving every other case
// the old tree's record. The F5b seam cases' record (docs/modular-graph-phase-f5b-design.md section 6.8) was taken this
// way, and it also carries the MIDI Clock submenu's rows in standalone-instance and standalone-scan: the fixtures now
// install the midi package's fake transport, which is a fixture change rather than a behaviour one.
const updateCases = (tjs.env.RP_MENU_MATRIX_UPDATE ?? "").split(",").map((s) => s.trim()).filter((s) => s !== "");

if (updateCases.length > 0) {
  test("re-record the cases the run names", async () => {
    const known = new Set(CASES.map((c) => c.name));
    for (const name of updateCases)
      if (!known.has(name)) throw new Error(`no recorded case is called ${name}`);
    const fresh = matrix();
    const merged = { ...(recorded as unknown as Record<string, unknown>) };
    for (const name of updateCases) merged[name] = fresh[name];
    await tjs.writeFile("test/menu/recorded-matrix.json", JSON.stringify(merged, null, 1) + "\n");
  });
}

for (const c of CASES) {
  test(`${c.name}: the menu tree is the recorded one`, () => {
    const want = (recorded as unknown as RecordedMatrix)[c.name];
    expect(matrix()[c.name].tree, `${c.name} tree`).toEqual(want.tree);
  });

  test(`${c.name}: every cycler reads and writes what was recorded`, () => {
    const want = (recorded as unknown as RecordedMatrix)[c.name];
    expect(matrix()[c.name].rows, `${c.name} rows`).toEqual(want.rows);
  });

  test(`${c.name}: every card dialog offers what was recorded`, () => {
    const want = (recorded as unknown as RecordedMatrix)[c.name];
    expect(matrix()[c.name].dialogs, `${c.name} dialogs`).toEqual(want.dialogs);
  });
}

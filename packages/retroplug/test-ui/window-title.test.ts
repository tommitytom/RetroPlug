// The standalone OS window title is pushed to native via the __rp_setWindowTitle seam. This spies that
// global (same technique as close-guard.test.ts spying __rp_quitWindow — the harness doesn't install the
// window seams, so the spy is the only handler). The App pushes the title on mount, so we spy then
// ui.reopen() (re-mounts the React tree) to observe it. With an empty project it's "RetroPlug v<version>"
// (the version proves the C++ Version.hpp → version() RPC → UI path); there is no ROM segment and, with
// no project name, no extra " - " segment.

import { test, expect, ui } from "ui-harness";

test("the window title is set to RetroPlug + version on (re)mount, with no project segment when empty", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);

  const titles: string[] = [];
  (globalThis as { __rp_setWindowTitle?: (t: string) => void }).__rp_setWindowTitle = (t) => {
    titles.push(t);
  };

  ui.reopen(); // re-mount so the title effect re-fires into the now-installed spy

  const last = titles[titles.length - 1];
  expect(last != null && /^RetroPlug v.+$/.test(last)).toBeTruthy(); // version present
  expect(!!last && last.includes(" - ")).toBeFalsy(); // empty project → no trailing segment

  delete (globalThis as { __rp_setWindowTitle?: unknown }).__rp_setWindowTitle;
});

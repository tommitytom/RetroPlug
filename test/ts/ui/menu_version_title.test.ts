// The menu chrome title shows the app version after the name, e.g.
// "RetroPlug v0.6.2" (sourced from the getVersion RPC -> Version.hpp). The
// regex keeps this robust to version bumps.
import { test, expect, ui } from "ui-harness";

test("the menu title shows 'RetroPlug v<semver>'", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40); // let the getVersion RPC resolve into the title

  const title = ui.findByTextContaining("RetroPlug");
  expect(title).toBeTruthy();
  expect(/^RetroPlug v\d+\.\d+\.\d+$/.test(title!.text)).toBeTruthy();
});

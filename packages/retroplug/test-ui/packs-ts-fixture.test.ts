// A pack's halves in the running UI (docs/downloadable-packs-m2c-plan.md): the TS fixture pack's app and ui halves load
// in the UI test harness (which has no control plane, so the UI loads the app half too), its start-menu row shows, and
// the two broken packs beside it are refused with their reasons while the good one loads. The runner gives this file
// the packs (run-ui-tests.mjs, PACK_TESTS).
import { test, expect, ui, navTo, Key } from "ui-harness";
import type { LaunchpadClient } from "@retroplug/launchpad/client";
import { FakeAudioDevice } from "@retroplug/audio/testSupport/fakeDevice";
import { FakeMidiDevice } from "@retroplug/midi/testSupport/fakeDevice";

type Status = { loaded: Record<string, string[]>; refused: Record<string, string> };
const status = (): Status => ((globalThis as Record<string, unknown>).__rp_packStatus as () => Status)();

test("the fixture pack's row shows, and the broken packs are refused by name", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(status().loaded.tsfix).toEqual(["app", "ui"]);
  expect(ui.findByText("TS Fixture Row") != null).toBeTruthy();
  expect(status().refused["tsfix-throws"]).toBe("the fixture half throws");
  expect(status().refused["tsfix-guard"]).toBe(
    "pack:tsfix-guard/app imports '@retroplug/sameboy/app', which is not a RetroPlug contract module");
});

// The kinds fixture pack's controller app is offered by the Launchpad submenu's "App" row, beside the built-in apps. The
// submenu shows only with a detected device, so the Launchpad's client is faked as ui-services.test.ts fakes it; the
// App is a prebuilt bundle with its own registry, so the menu is the one way to read what it holds.
const g = globalThis as Record<string, unknown>;
g.__rp_isStandalone = true;
new FakeAudioDevice().install();
new FakeMidiDevice({ inputs: [], outputs: [] }).install();
const idle = { connected: false, generation: 0, sent: 0, dropped: 0, input: "", output: "", error: "" };
g.__rp_launchpadClient = {
  ports: () => ({ inputs: ["LPProMK3 MIDI"], outputs: ["LPProMK3 MIDI"] }),
  status: () => idle,
  claim: () => idle,
  release: () => idle,
  send: () => true,
  restored: () => true,
  openInputs: () => [],
  openOutput: () => 0,
  sendOn: () => true,
  read: () => ({ messages: [], dropped: 0 }),
  close: () => true,
  portError: () => "",
} as unknown as LaunchpadClient;

test("the kinds fixture pack's controller app is offered by the Launchpad menu's App row", () => {
  expect(ui.boot()).toBeTruthy();
  expect(status().loaded["tsfix-kind"]).toEqual(["app", "kinds"]);
  ui.pump(30);
  ui.fileDrop(ui.romDir() + "/mGB.gb", 0, 0);
  ui.pump(30);
  ui.tapKey(Key.Esc); // the instance menu
  ui.pump(10);
  expect(navTo("Launchpad")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("App")).toBeTruthy();
  const seen: string[] = [];
  for (let i = 0; i < 32; i++) {
    const label = ui.findByTextContaining("App")?.text ?? "";
    if (seen.length > 0 && label === seen[0]) break;
    seen.push(label);
    ui.tapKey(Key.Right);
    ui.pump(4);
  }
  // The cycler wraps through every app, the pack's beside the built-in ones.
  expect(seen.includes("App: TS Fixture App")).toBeTruthy();
  expect(seen.includes("App: LSDj MI.MAP")).toBeTruthy();
});

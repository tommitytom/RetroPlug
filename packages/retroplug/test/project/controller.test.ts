// setController's app config: one knob at a time merges into the app's config, but a DIFFERENT app starts from what
// the patch gives it. Merging across a switch kept every key of every app ever chosen in the .rplg, handed to
// whichever app came next.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";

function newProject(): ProjectStore {
  const be = new MockBackend("/cfg");
  return new ProjectStore(be, new RecentStore(be), buildAppRegistry());
}

test("switching to a different app starts it from a clean config, not the old app's knobs", () => {
  const project = newProject();
  project.setController({ appConfig: { follow: false, quantise: "beat" } });
  project.setController({ app: "some-other-app", appConfig: {} });
  expect(project.settings().controller.appConfig).toEqual({});
  project.setController({ app: "some-other-app", appConfig: { x: 1 } }); // the same app: merges as before
  project.setController({ appConfig: { y: 2 } });
  expect(project.settings().controller.appConfig).toEqual({ x: 1, y: 2 });
});

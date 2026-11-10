// The controller section's merge (the Launchpad pack's projectSection.ts): one knob at a time merges into the app's
// config, but a DIFFERENT app starts from what the patch gives it. Merging across a switch would keep every key of
// every app ever chosen in the .rplg, handed to whichever app came next.
import { test, expect } from "@retroplug/testing";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import type { ControllerSection } from "@retroplug/launchpad/projectSection";

/** The project section the Launchpad pack contributes. */
const CONTROLLER_SECTION = "controller";
const sectionOf = (project: ProjectStore): ControllerSection => project.section(CONTROLLER_SECTION);

function newProject(): ProjectStore {
  const be = new MockBackend("/cfg");
  return new ProjectStore(be, new RecentStore(be), buildAppRegistry());
}

test("switching to a different app starts it from a clean config, not the old app's knobs", () => {
  const project = newProject();
  project.setSection(CONTROLLER_SECTION, { appConfig: { follow: false, quantise: "beat" } });
  project.setSection(CONTROLLER_SECTION, { app: "some-other-app", appConfig: {} });
  expect(sectionOf(project).appConfig).toEqual({});
  project.setSection(CONTROLLER_SECTION, { app: "some-other-app", appConfig: { x: 1 } }); // the same app: merges as before
  project.setSection(CONTROLLER_SECTION, { appConfig: { y: 2 } });
  expect(sectionOf(project).appConfig).toEqual({ x: 1, y: 2 });
});

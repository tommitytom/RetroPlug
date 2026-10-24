// The project MIDI-routing mode reaches the kernel the moment it changes. The `midi-routing` role is
// synthesized from the setting at projection time (kernelProjection), so a setter that only wrote the
// setting left the kernel routing by the old mode until some unrelated structural edit re-projected it.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";
import { projectKernelStructure } from "../../src/kernelProjection";

function newProject() {
  const be = new MockBackend("/cfg");
  return new ProjectStore(be, new RecentStore(be), buildAppRegistry());
}

test("setMidiRouting re-drives the kernel with the new mode", () => {
  const project = newProject();
  const pushed: unknown[] = [];
  project.setOnSystemsChange(() => {
    const routing = projectKernelStructure(project.systems.view(), project.settings().midiRouting).project?.[0];
    pushed.push(routing?.config.mode);
  });
  expect(project.setMidiRouting("oneChannelPerInstance")).toBeTruthy();
  expect(pushed).toEqual(["oneChannelPerInstance"]);
});

test("a refused MIDI-routing value pushes nothing", () => {
  const project = newProject();
  let pushes = 0;
  project.setOnSystemsChange(() => void pushes++);
  expect(project.setMidiRouting("everywhere" as never)).toBeFalsy();
  expect(pushes).toBe(0);
});

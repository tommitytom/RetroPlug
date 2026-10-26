// The project MIDI-routing mode reaches the kernel the moment it changes. The `midi-routing` role is
// synthesized from the setting at projection time (kernelProjection), so a setter that only wrote the
// setting left the kernel routing by the old mode until some unrelated structural edit published it.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";

function newProject() {
  const be = new MockBackend("/cfg");
  return { be, project: new ProjectStore(be, new RecentStore(be), buildAppRegistry()) };
}

test("setMidiRouting publishes the kernel's structure with the new mode", () => {
  const { be, project } = newProject();
  expect(project.setMidiRouting("oneChannelPerInstance")).toBeTruthy();
  expect(be.graphRequests.length).toBe(1);
  expect(be.kernelSettings()?.project).toEqual([{ config: { mode: "oneChannelPerInstance" }, kind: "midi-routing" }]); // keys sorted, as a document spells them
  expect(be.graphRequests[0].tap).toBe(undefined); // a routing of MIDI, not of audio: no re-arm
});

test("a refused MIDI-routing value publishes nothing", () => {
  const { be, project } = newProject();
  let notified = 0;
  project.setOnSystemsChange(() => void notified++);
  expect(project.setMidiRouting("everywhere" as never)).toBeFalsy();
  expect(be.graphRequests.length).toBe(0);
  expect(notified).toBe(0);
});

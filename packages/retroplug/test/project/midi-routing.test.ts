// The project MIDI-routing mode reaches the audio thread the moment it changes. The routing node is placed
// from the setting at each publish (buildGraph), so a setter that only wrote the setting left the audio
// thread routing by the old mode until some unrelated structural edit published it.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry } from "../../src/appHost";

function newProject() {
  const be = new MockBackend("/cfg");
  return { be, project: new ProjectStore(be, new RecentStore(be), buildAppRegistry()) };
}

test("setMidiRouting publishes the routing node with the new mode", () => {
  const { be, project } = newProject();
  expect(project.setMidiRouting("oneChannelPerInstance")).toBeTruthy();
  expect(be.graphRequests.length).toBe(1);
  expect(be.graphNode("routing")?.settings).toEqual({ mode: "oneChannelPerInstance", systems: [] });
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

// The project MIDI-routing mode reaches the kernel the moment it changes. The `midi-routing` role is
// synthesized from the setting at projection time (kernelProjection), so a setter that only wrote the
// setting left the kernel routing by the old mode until some unrelated structural edit re-projected it.
import { test, expect } from "../../testing/harness";
import { MockBackend } from "../../testing/mockBackend";
import { RecentStore } from "../../src/recentStore";
import { ProjectStore } from "../../src/projectStore";
import { buildAppRegistry, syncDspFromStore } from "../../src/appHost";
import type { DspRuntimeClient } from "../../src/dspRuntime";

function newProject() {
  const be = new MockBackend("/cfg");
  return new ProjectStore(be, new RecentStore(be), buildAppRegistry());
}

test("setMidiRouting re-drives the kernel with the new mode", () => {
  const project = newProject();
  const pushed: unknown[] = [];
  const dsp: DspRuntimeClient = {
    compileScript: () => new Uint8Array(0),
    loadKernel: () => true,
    setSystems: (struct) => {
      pushed.push(struct.project?.[0]?.config.mode);
      return true;
    },
  };
  project.setOnSystemsChange(() => void syncDspFromStore(project, dsp));
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

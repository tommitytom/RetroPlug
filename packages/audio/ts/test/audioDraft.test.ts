// The "More drivers..." step (docs/modular-graph-phase-f5b-design.md section 6.5): a sibling backend's drivers are its
// library's first use, so only an explicit step on the Driver row asks for them, and only once per visit. A sibling that
// answers NOTHING (a pinned run, where the hook refuses; a driver list with nothing usable in it) must not be asked
// again on the next tap, and a run that pins a backend offers no step at all.
import { test, expect } from "@retroplug/testing";
import type { AudioBackendDescription } from "@retroplug/core/host";
import { FakeAudioDevice, DEFAULT_BACKENDS } from "../testSupport/fakeDevice";
import { audioDescription, fetchMoreDrivers, moreDriversAvailable, resetAudioDraft } from "../ui/audioDraft";

/** A host over the fake's globals: `audioDescription` is the one read the rows make, and a sibling's drivers come from
 *  the hook the native host binds (the fake counts those calls, which is what "asked once" is proved by). */
const hostOver = (fake: FakeAudioDevice) =>
  ({
    audioDevice: {
      describe: () => fake.description,
      backendDrivers: (id: string) =>
        (globalThis as unknown as { __rp_audioBackendDrivers: (b: string) => unknown }).__rp_audioBackendDrivers(id),
    },
  }) as never;

/** SDL with no driver it can list, which is what a refused (pinned) sibling answers. */
const SILENT_SIBLING: AudioBackendDescription[] = [
  DEFAULT_BACKENDS[0],
  { id: "sdl", label: "SDL", autoDriver: "", drivers: [] },
  DEFAULT_BACKENDS[2],
];

test("a sibling that answers nothing is asked once, and the step goes away", () => {
  const fake = new FakeAudioDevice({ backends: SILENT_SIBLING }).install();
  resetAudioDraft();
  try {
    const described = audioDescription(hostOver(fake))!;
    expect(moreDriversAvailable(described)).toBe(true);
    // The answer is `[]` and stays that way: the step must not fetch the same backend on every tap.
    expect(fetchMoreDrivers(hostOver(fake), described)).toBe(true);
    expect(fake.backendDriversCalls).toEqual(["sdl"]);
    expect(moreDriversAvailable(audioDescription(hostOver(fake))!)).toBe(false);
    expect(fetchMoreDrivers(hostOver(fake), audioDescription(hostOver(fake))!)).toBe(false);
    expect(fake.backendDriversCalls).toEqual(["sdl"]);
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

test("a run that pins a backend offers no step: the hook refuses, and the This-run row says why", () => {
  const fake = new FakeAudioDevice({ overrides: { backend: "portaudio", blockSize: 0 } }).install();
  resetAudioDraft();
  try {
    const described = audioDescription(hostOver(fake))!;
    expect(moreDriversAvailable(described)).toBe(false);
    expect(fetchMoreDrivers(hostOver(fake), described)).toBe(false);
    expect(fake.backendDriversCalls).toEqual([]);
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

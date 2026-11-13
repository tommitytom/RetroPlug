// The "More drivers..." step (docs/modular-graph-phase-f5b-design.md section 6.5): a sibling backend's drivers are its
// library's first use, so only an explicit step on the Driver row asks for them, and only once per visit. A sibling that
// answers NOTHING (a pinned run, where the hook refuses; a driver list with nothing usable in it) must not be asked
// again on the next tap, and a run that pins a backend offers no step at all.
import { test, expect } from "@retroplug/testing";
import type { AudioBackendDescription } from "@retroplug/core/host";
import { FakeAudioDevice, DEFAULT_BACKENDS } from "../testSupport/fakeDevice";
import { DEFAULT_AUDIO_SECTION } from "../configSection";
import audioUi from "../ui/index";
import {
  applyAudioDraft, audioDescription, audioDraftDirty, audioDriverChoices, audioNotSaved, fetchMoreDrivers,
  getAudioDraft, moreDriversAvailable, resetAudioDraft, setAudioDraft,
} from "../ui/audioDraft";

/** A host over the fake's globals: `audioDescription` is the one read the rows make, and a sibling's drivers come from
 *  the hook the native host binds (the fake counts those calls, which is what "asked once" is proved by). The config
 *  stub is the `audio` section the draft seeds from, by default what a file that took the defaults holds. */
const SAVED = { ...DEFAULT_AUDIO_SECTION };
const hostOver = (fake: FakeAudioDevice, section: typeof SAVED = SAVED, setSection: () => boolean = () => true) =>
  ({
    audioDevice: {
      describe: () => fake.description,
      backendDrivers: (id: string) =>
        (globalThis as unknown as { __rp_audioBackendDrivers: (b: string) => unknown }).__rp_audioBackendDrivers(id),
      apply: (choice: unknown) =>
        (globalThis as unknown as { __rp_audioApply: (c: unknown) => unknown }).__rp_audioApply(choice),
    },
    config: { section: () => section, setSection },
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

/** The audio package's own menu contribution: the builder the shell's Audio container renders, whose `onOpen` is the
 *  moment a visit begins. */
const audioMenu = audioUi.menus!.find((menu) => menu.placement === "settings.audio")!;

/** The rows the shell would render, built by the real builder over the fake. */
const rowsOf = (host: unknown) =>
  audioMenu.build(host as never, null, { systems: [], assetRom: () => null, romPatterns: [] });

test("a visit asks a sibling that answered nothing again, and 'More drivers...' comes back", () => {
  const fake = new FakeAudioDevice({ backends: [...SILENT_SIBLING] }).install();
  resetAudioDraft();
  try {
    const host = hostOver(fake);
    expect(moreDriversAvailable(audioDescription(host)!)).toBe(true);
    // The first visit asks, gets nothing, and the step goes away for the rest of the visit.
    expect(fetchMoreDrivers(host, audioDescription(host)!)).toBe(true);
    expect(fake.backendDriversCalls).toEqual(["sdl"]);
    expect(moreDriversAvailable(audioDescription(host)!)).toBe(false);
    // Collapsing and reopening the container is a new visit: the question is asked afresh, and a library that had
    // nothing to list last time can now be seen to have something.
    audioMenu.onOpen!(host as never);
    expect(moreDriversAvailable(audioDescription(host)!)).toBe(true);
    // The element is REPLACED rather than edited in place: the fixture is the module's, and a later case must not see
    // this visit's answer.
    fake.description.backends[1] = {
      ...SILENT_SIBLING[1],
      drivers: [{ id: "pipewire", label: "pipewire", devices: { names: ["Speakers"], defaultName: "Speakers" } }],
    };
    expect(fetchMoreDrivers(host, audioDescription(host)!)).toBe(true);
    expect(fake.backendDriversCalls).toEqual(["sdl", "sdl"]);
    const labels = audioDriverChoices(host, audioDescription(host)!).map((choice) => choice.label);
    expect(labels.includes("SDL: pipewire")).toBe(true);
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

test("a visit starts a fresh draft: a staged block size is gone, and Apply with it", () => {
  const fake = new FakeAudioDevice().install();
  resetAudioDraft();
  try {
    const host = hostOver(fake);
    expect(getAudioDraft(host).blockSize).toBe(512);
    setAudioDraft({ blockSize: 256 });
    expect(audioDraftDirty(host)).toBe(true);
    expect(rowsOf(host).find((row) => row.id === "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
    expect(rowsOf(host).find((row) => row.id === "audio-apply")!.disabled).toBeFalsy();
    // The container is collapsed and reopened: the unapplied draft is discarded, so the rows show the saved section and
    // Apply has nothing to commit.
    audioMenu.onOpen!(host as never);
    expect(getAudioDraft(host).blockSize).toBe(512);
    expect(audioDraftDirty(host)).toBe(false);
    expect(rowsOf(host).find((row) => row.id === "audio-block")!.label).toBe("Block Size: 512 (10.7 ms)");
    expect(rowsOf(host).find((row) => row.id === "audio-apply")!.disabled).toBeTruthy();
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

test("a visit starts from the saved section, not from what the device is running", () => {
  // The two differ whenever the device did not take the saved choice (a clamped rate, a muted fallback, a run's flag):
  // the draft, and the rows, are the SECTION's, because that is what a later Apply writes and a later launch opens.
  const fake = new FakeAudioDevice().install(); // running 512, 48 kHz
  resetAudioDraft();
  try {
    const host = hostOver(fake, { ...SAVED, blockSize: 256 });
    audioMenu.onOpen!(host);
    expect(rowsOf(host).find((row) => row.id === "audio-block")!.label).toBe("Block Size: 256 (5.3 ms)");
    expect(audioDraftDirty(host)).toBe(false);
    expect(fake.applied.length).toBe(0); // a visit opens nothing
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

test("an Apply the file refuses leaves the device alone and the draft staged", () => {
  // The gate the shell's Apply row and this package share: the FILE is the record, so the device must not run a choice
  // the next launch will not open.
  const fake = new FakeAudioDevice().install();
  resetAudioDraft();
  try {
    const refused = hostOver(fake, SAVED, () => false);
    expect(getAudioDraft(refused).blockSize).toBe(512); // the builder that shows the row seeds the draft
    setAudioDraft({ blockSize: 256 });
    applyAudioDraft(refused);
    expect(fake.applied.length).toBe(0);
    expect(audioNotSaved().length > 0).toBe(true);
    expect(getAudioDraft(refused).blockSize).toBe(256);

    // The file takes the same draft: the device runs it, and the row clears.
    applyAudioDraft(hostOver(fake, SAVED, () => true));
    expect(fake.applied.at(-1)!.blockSize).toBe(256);
    expect(audioNotSaved()).toBe("");
  } finally {
    fake.uninstall();
    resetAudioDraft();
  }
});

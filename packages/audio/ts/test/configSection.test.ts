// The `audio` config section's parse (docs/modular-graph-phase-f5b-design.md section 6.2): a junk value snaps to its
// default per field, so one bad number cannot mute a run at launch, and the closed set / clamps mirror what
// AudioHost::apply accepts. The section is additive: nothing is stamped.
import { test, expect } from "@retroplug/testing";
import {
  AUDIO_OUT_CHANNELS, AUDIO_SECTION_KEY, audioSection, DEFAULT_AUDIO_SECTION,
} from "../configSection";

const parse = (raw: unknown) => audioSection.schema.parse(raw) as unknown as Record<string, unknown>;

test("a missing or junk block reads the defaults, field by field", () => {
  expect(audioSection.key).toBe(AUDIO_SECTION_KEY);
  expect(audioSection.defaults()).toEqual(DEFAULT_AUDIO_SECTION);
  expect(parse(undefined)).toEqual(DEFAULT_AUDIO_SECTION);
  expect(parse({ backend: 7, driver: null, device: [], sampleRate: "fast", blockSize: -1, outChannels: 3 })).toEqual(
    DEFAULT_AUDIO_SECTION,
  );
  // One bad field does not take the others with it.
  expect(parse({ backend: "sdl", driver: "pipewire", blockSize: 0 })).toEqual({
    ...DEFAULT_AUDIO_SECTION, backend: "sdl", driver: "pipewire",
  });
});

test("a saved backend is any id the product lists, and a blank one is Auto", () => {
  for (const id of ["auto", "portaudio", "sdl", "null"]) expect(parse({ backend: id }).backend).toBe(id);
  // A third-party backend's id (a package with an `audioBackend` manifest item, or a fork's own) must be SAVABLE: the
  // set native knows is a build's, and snapping an unknown id to Auto would make a pick that --audio-backend accepts
  // impossible to keep. Native answers an id no backend has by falling back to Auto, with its status saying what ran.
  expect(parse({ backend: "my-dsp-card" }).backend).toBe("my-dsp-card");
  // Junk is still junk: a non-string is not an id, and a blank one is the absence of a choice.
  expect(parse({ backend: "18" }).backend).toBe("18");
  expect(parse({ backend: "  " }).backend).toBe(DEFAULT_AUDIO_SECTION.backend);
  expect(parse({ backend: 18 }).backend).toBe(DEFAULT_AUDIO_SECTION.backend);
});

test("rate and block are positive integers; the width is 2, 4, 6 or 8", () => {
  expect(parse({ sampleRate: 44100, blockSize: 256 }).sampleRate).toBe(44100);
  expect(parse({ sampleRate: 44100, blockSize: 256 }).blockSize).toBe(256);
  expect(parse({ sampleRate: 0 }).sampleRate).toBe(DEFAULT_AUDIO_SECTION.sampleRate);
  expect(parse({ blockSize: -256 }).blockSize).toBe(DEFAULT_AUDIO_SECTION.blockSize);
  expect(parse({ sampleRate: 44100.7 }).sampleRate).toBe(44101); // rounded, as the device takes an integer
  for (const channels of AUDIO_OUT_CHANNELS) expect(parse({ outChannels: channels }).outChannels).toBe(channels);
  expect(parse({ outChannels: 3 }).outChannels).toBe(2);
  expect(parse({ outChannels: 10 }).outChannels).toBe(2);
});

test("a field this build does not know is dropped by the parse (the file keeps it verbatim)", () => {
  // The config-section rule: a block this session does not set is written back as the file carried it, so the additive
  // field survives a write here even though the section's own reader has no use for it.
  expect(parse({ ...DEFAULT_AUDIO_SECTION, bufferCount: 4 })).toEqual(DEFAULT_AUDIO_SECTION);
});

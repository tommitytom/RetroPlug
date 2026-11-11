// The `audio` config section (docs/modular-graph-phase-f5b-design.md section 6.2): the standalone's audio device, as
// config.json saves it. A root key of the user config, owned here rather than by the shell, so the shell names no
// backend, driver, device name or default.
//
// The shape is the device's CHOICE, never a library's: the backend and driver by the ids the product's AUDIO_BACKENDS
// and the backend's own driver list spell ("portaudio" / "pipewire"), the device by the name the driver knows ("" = its
// default), and the frames and width asked for. Render-ahead is NOT here: it is a run's flag, not a setting (decision
// 13), so it cannot be saved into a file a launcher's override then has to fight.
//
// The parse is written out by hand, as the packs' sections are: junk snaps to the default per field, so one bad value
// cannot mute a run at launch. The clamp mirrors what AudioHost::apply accepts (a positive rate and block, channels of
// 2, 4, 6 or 8).
import type { ProjectSection } from "@retroplug/core/definePackage";

export interface AudioSection {
  /** "auto" takes the first backend the product names whose auto driver opens; otherwise that backend by id. */
  backend: string;
  /** The driver's id in the backend's vocabulary ("" = the backend's auto driver). */
  driver: string;
  /** The device's name ("" = the driver's default). */
  device: string;
  sampleRate: number;
  blockSize: number;
  outChannels: number;
}

/** The section's root key in config.json. */
export const AUDIO_SECTION_KEY = "audio";

/** What an install that never set the section opens with: the compiled-in defaults, which are the ones the SDL app
 *  opened with before the choice was saved at all. */
export const DEFAULT_AUDIO_SECTION: AudioSection = Object.freeze({
  backend: "auto",
  driver: "",
  device: "",
  sampleRate: 48000,
  blockSize: 512,
  outChannels: 2,
});

/** The backends a saved choice may name. "null" is the fallback the host takes by itself; it is in the closed set
 *  because a harness pins it and a file may record it, but no row offers it. */
export const AUDIO_BACKENDS = ["auto", "portaudio", "sdl", "null"] as const;

/** The channel counts a device can be opened at (2 = stereo mix; 4/6/8 spread the project's routing across pairs). */
export const AUDIO_OUT_CHANNELS = [2, 4, 6, 8] as const;

const isObject = (v: unknown): v is Record<string, unknown> =>
  v !== null && typeof v === "object" && !Array.isArray(v);

const stringOf = (v: unknown): string => (typeof v === "string" ? v : "");
const positiveInt = (v: unknown, fallback: number): number =>
  typeof v === "number" && Number.isFinite(v) && v > 0 ? Math.round(v) : fallback;

function parseAudioSection(raw: unknown): AudioSection {
  const block = isObject(raw) ? raw : {};
  const backend = stringOf(block.backend);
  const channels = block.outChannels;
  return {
    backend: (AUDIO_BACKENDS as readonly string[]).includes(backend) ? backend : DEFAULT_AUDIO_SECTION.backend,
    driver: stringOf(block.driver),
    device: stringOf(block.device),
    sampleRate: positiveInt(block.sampleRate, DEFAULT_AUDIO_SECTION.sampleRate),
    blockSize: positiveInt(block.blockSize, DEFAULT_AUDIO_SECTION.blockSize),
    outChannels: (AUDIO_OUT_CHANNELS as readonly number[]).includes(channels as number)
      ? (channels as number)
      : DEFAULT_AUDIO_SECTION.outChannels,
  };
}

/** The section the generated registry hands the shell: additive only (nothing is stamped), so a file written before a
 *  field existed keeps its bytes and the parse fills the field's default. */
export const audioSection: ProjectSection<AudioSection> = {
  key: AUDIO_SECTION_KEY,
  schema: { parse: parseAudioSection },
  defaults: () => ({ ...DEFAULT_AUDIO_SECTION }),
};

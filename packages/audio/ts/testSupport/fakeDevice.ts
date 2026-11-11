// A fake audio device for the menu and UI tests: the three globals the SDL host binds, over a description the test
// owns. The Settings > Audio rows are proved without a real backend - no library is initialised, so a test machine's
// default card is never touched (the Zoom L6 rule) - and the row's Apply reaches `applied`, which is what the host's
// reconfigure would have taken.
import type {
  AudioBackendDescription, AudioDescription, AudioDeviceChoice, AudioDeviceStatus, AudioDriverDescription,
} from "@retroplug/core/host";

interface AudioHooks {
  __rp_audioDescribe?: () => AudioDescription | null;
  __rp_audioBackendDrivers?: (backend: string) => AudioDriverDescription[];
  __rp_audioApply?: (choice: AudioDeviceChoice) => AudioDeviceStatus | null;
  __rp_audioStatus?: () => AudioDeviceStatus | null;
}

const globals = globalThis as unknown as AudioHooks;

export interface FakeAudioDeviceOptions {
  backends?: AudioBackendDescription[];
  status?: Partial<AudioDeviceStatus>;
  /** What a run's flags pin over the saved section (empty/0 for no flag): the rows name it. */
  overrides?: { backend: string; blockSize: number };
}

/** Two backends with a same-named driver ("PortAudio: PipeWire" against "SDL: pipewire"), which is the case the rows'
 *  labels exist for. */
export const DEFAULT_BACKENDS: AudioBackendDescription[] = [
  {
    id: "portaudio",
    label: "PortAudio",
    autoDriver: "pipewire",
    drivers: [
      { id: "pipewire", label: "PipeWire", devices: { names: ["Speakers", "HDMI"], defaultName: "Speakers" } },
      { id: "alsa", label: "ALSA", devices: { names: ["default", "sysdefault"], defaultName: "default" } },
    ],
  },
  {
    id: "sdl",
    label: "SDL",
    autoDriver: "",
    drivers: [{ id: "pipewire", label: "pipewire", devices: { names: ["Speakers"], defaultName: "Speakers" } }],
  },
  { id: "null", label: "Null", autoDriver: "", drivers: [] },
];

export class FakeAudioDevice {
  readonly description: AudioDescription;
  /** Every choice the rows committed, in order. */
  readonly applied: AudioDeviceChoice[] = [];
  /** Every `backendDrivers` call the UI made, in order: a case holds this EMPTY while a menu is built (enumerating a
   *  sibling is its library's first use, so a builder that does it initialises every backend at launch). */
  readonly backendDriversCalls: string[] = [];
  private open: boolean;

  constructor(options: FakeAudioDeviceOptions = {}) {
    this.description = {
      backends: options.backends ?? DEFAULT_BACKENDS,
      overrides: options.overrides ?? { backend: "", blockSize: 0 },
      status: {
        backend: "portaudio",
        driver: "pipewire",
        device: "",
        sampleRate: 48000,
        blockFrames: 512,
        channels: 2,
        error: "",
        live: true,
        ...options.status,
      },
    };
    this.open = true;
  }

  /** Install the globals; returns this, so a test can write `const fake = new FakeAudioDevice().install()`. */
  install(): this {
    // The description carries the BACKEND IN FORCE's drivers only, exactly as the native describe does: a sibling's
    // drivers are asked for explicitly (an explicit step on the Driver row), and asking is what the call counter
    // watches.
    globals.__rp_audioDescribe = () => {
      if (!this.open) return null;
      const inForce = this.description.status.backend;
      return {
        status: this.description.status,
        overrides: this.description.overrides,
        backends: this.description.backends.map((backend) => ({
          ...backend,
          drivers: backend.id === inForce ? backend.drivers : [],
        })),
      };
    };
    globals.__rp_audioStatus = () => (this.open ? this.description.status : null);
    globals.__rp_audioBackendDrivers = (id) => {
      this.backendDriversCalls.push(id);
      return this.description.backends.find((b) => b.id === id)?.drivers ?? [];
    };
    globals.__rp_audioApply = (choice) => {
      this.applied.push({ ...choice });
      this.description.status = {
        backend: choice.backend === "auto" ? this.description.status.backend : choice.backend,
        driver: choice.driver || this.description.status.driver,
        device: choice.device,
        sampleRate: choice.sampleRate,
        blockFrames: choice.blockSize,
        channels: choice.outChannels,
        error: "",
        live: true,
      };
      return this.description.status;
    };
    return this;
  }

  /** A host with no audio device at all (a DAW-hosted editor, the plugin). */
  uninstall(): void {
    delete globals.__rp_audioDescribe;
    delete globals.__rp_audioStatus;
    delete globals.__rp_audioBackendDrivers;
    delete globals.__rp_audioApply;
  }

  /** Mute the device (the null fallback with a reason), as a failed open does. */
  mute(reason: string): void {
    this.description.status = { ...this.description.status, error: reason, live: false };
  }
}

// The audio package's UI contribution (docs/modular-graph-phase-f5b-design.md section 6.5): the Settings > Audio rows,
// at `settings.audio` inside the shell's Audio container. The value they edit is the `audio` config section, reached
// through the host's `config` group, so the shell names no backend, driver, device or default.
//
// The rows: one Driver row naming the backend ("Auto (PortAudio: PipeWire)", "PortAudio: ALSA", "SDL: pipewire"),
// Output Device, Sample Rate, Block Size (with the latency it buys), Out Channels, Apply (the draft model: the rows
// stage, Apply commits), a "More drivers..." step for a backend this visit has not enumerated, and - only while the
// device is muted - the reason it is.
import { button, cycler, infoRow, separator, type MenuItem } from "@retroplug/ui/menu";
import { defineUi, type MenuBuilder, type UiHost, type UiService } from "@retroplug/ui/contribution";
import { AUDIO_OUT_CHANNELS } from "../configSection";
import {
  applyAudioDraft, applySavedAudio, audioDescription, audioDevices, audioDraftDirty, audioDriverChoices,
  fetchMoreDrivers, getAudioDraft, moreDriversAvailable, setAudioDraft, subscribeAudioDraft,
} from "./audioDraft";

export * from "./audioDraft";

/** Both families, up to what a modern interface actually offers: the 44.1 line (44100/88200/176400) and the 48 line
 *  (32000/48000/96000/192000). Nothing in the path caps at 48k - the cores take whatever rate they are handed - this
 *  is just the starter set the SDL host shipped with. Higher rates cost proportionally more emulation per second, so a
 *  low-power device should stay at 48k or below. */
const AUDIO_RATES = [22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000];
const AUDIO_BLOCKS = [128, 256, 512, 1024, 2048, 4096];
/** Labelled by pair count since the project's audio routing works in stereo pairs. */
const AUDIO_CHANNEL_NAMES = ["Stereo", "4 (2 pairs)", "6 (3 pairs)", "8 (4 pairs)"];

/** How long one block of `frames` lasts at `rate`, for the Block Size row: the number that setting is really about (a
 *  block is a buffer's worth of audio, so it is the delay the size buys you). It is the device's buffer period, not
 *  end-to-end output latency - the driver and the device add their own buffering on top - and it is computed against
 *  the DRAFT rate, so cycling Sample Rate re-labels every block size. */
function blockLatencyMs(frames: number, sampleRate: number): string {
  if (sampleRate <= 0) return "?";
  return `${((frames / sampleRate) * 1000).toFixed(1)} ms`;
}

/** The row's device labels: "Default" at index 0 (naming the device it resolves to when the host can say), then the
 *  driver's devices. A saved device the driver no longer lists still shows, marked, so the row reflects the choice. */
function deviceNames(
  devices: { names: string[]; defaultName: string },
  selected: string,
): { names: string[]; index: number } {
  const names = [devices.defaultName ? `Default (${devices.defaultName})` : "Default", ...devices.names];
  if (selected === "") return { names, index: 0 };
  const i = devices.names.indexOf(selected);
  if (i >= 0) return { names, index: i + 1 };
  names.push(`${selected} (not present)`);
  return { names, index: names.length - 1 };
}

/** What this run's flags pin over the saved choice, or "" when none: the row that says so. */
function runOverride(described: { overrides?: { backend: string; blockSize: number } }): string {
  const parts: string[] = [];
  if (described.overrides?.backend) parts.push(`backend ${described.overrides.backend}`);
  if (described.overrides && described.overrides.blockSize > 0) parts.push(`block ${described.overrides.blockSize}`);
  return parts.join(", ");
}

function audioSettingsChildren(host: UiHost): MenuItem[] {
  const described = audioDescription(host);
  if (!described) return []; // no audio device on this host (a DAW's editor, the UI harness): the container is empty
  const cfg = getAudioDraft(host);
  const drivers = audioDriverChoices(host, described);
  const driverIdx = Math.max(
    0,
    drivers.findIndex((d) => d.backend === cfg.backend && (cfg.backend === "auto" || d.driver === cfg.driver)),
  );
  const rateIdx = Math.max(0, AUDIO_RATES.indexOf(cfg.sampleRate));
  const blockIdx = Math.max(0, AUDIO_BLOCKS.indexOf(cfg.blockSize));
  const chIdx = Math.max(0, (AUDIO_OUT_CHANNELS as readonly number[]).indexOf(cfg.outChannels));
  const devices = audioDevices(host, described, cfg);
  const dev = deviceNames(devices, cfg.device);
  const devName = (n: number) => (n === 0 ? "" : devices.names[n - 1] ?? cfg.device);
  const dirty = audioDraftDirty(host);
  const muted = described.status.error;
  return [
    // The cyclers stage a pending value only: the label tracks the draft, the device is unchanged until Apply. Picking
    // a driver resets the device, because a device name is not valid across backends.
    cycler("audio-driver", "Driver", drivers.map((d) => d.label), driverIdx, (n) =>
      setAudioDraft({ backend: drivers[n].backend, driver: drivers[n].driver, device: "" }),
    ),
    cycler("audio-device", "Output Device", dev.names, dev.index, (n) => setAudioDraft({ device: devName(n) })),
    cycler("audio-rate", "Sample Rate", AUDIO_RATES.map((r) => `${r} Hz`), rateIdx, (n) =>
      setAudioDraft({ sampleRate: AUDIO_RATES[n] }),
    ),
    cycler("audio-block", "Block Size", AUDIO_BLOCKS.map((b) => `${b} (${blockLatencyMs(b, cfg.sampleRate)})`),
      blockIdx, (n) => setAudioDraft({ blockSize: AUDIO_BLOCKS[n] }),
    ),
    cycler("audio-channels", "Out Channels", AUDIO_CHANNEL_NAMES, chIdx, (n) =>
      setAudioDraft({ outChannels: AUDIO_OUT_CHANNELS[n] }),
    ),
    separator("audio-sep-apply"),
    // Commit the staged choice (the file's section, then the device). keepOpen: you often tweak several settings in a
    // row, and the row greys out afterwards rather than closing the menu. While the device is MUTED the row stays
    // enabled even with a clean draft: re-applying the saved section is the one retry a failed open, a lost device or a
    // watchdog trip has (the status row says why; this row is how you try again).
    button("audio-apply", "Apply", () => applyAudioDraft(host), !dirty && !muted),
    // A sibling backend's drivers are its library's first use, so they are asked for here - an explicit step on this
    // menu - and only for a sibling no step has asked about yet. Never from the builder, which would open every backend
    // a product names on the render that shows this row.
    ...(moreDriversAvailable(described)
      ? [{
        ...button("audio-drivers-more", "More drivers...", () => fetchMoreDrivers(host, described)),
        keepOpen: true,
      }]
      : []),
    ...(muted
      ? [separator("audio-sep-status"), infoRow("audio-status", `Muted: ${muted}`)]
      : []),
    // A run's flags win over the saved section (E12), so the rows say which choice is NOT the one running rather than
    // showing a saved value the device is not using.
    ...(runOverride(described)
      ? [infoRow("audio-run-override", `This run: ${runOverride(described)} (a flag overrides the saved choice)`)]
      : []),
  ];
}

/** The Settings > Audio rows, in the shell's Audio container. */
const audioMenu: MenuBuilder = {
  placement: "settings.audio",
  build: (host) => audioSettingsChildren(host),
};

/** What the service has done this mount: the first apply (the startup rendezvous's word), and the status it last
 *  showed, so a mute, a lost device or a recovery repaints the Muted row. */
let applied = false;
let lastStatus = "";

/** The status as one comparable line: what the Muted row and the Apply row read, without a repaint reading as a
 *  move. */
function statusKey(host: UiHost): string {
  const status = host.audioDevice?.status();
  if (!status) return "";
  return [status.backend, status.driver, status.device, status.sampleRate, status.blockFrames, status.channels,
    status.error, status.live ? 1 : 0].join("|");
}

/** The mount's half (decision 23): hand the device request the `audio` section saved (or the defaults for a file that
 *  never set it) to native, which is the word the startup rendezvous waits for. It lands on the FIRST POLL, not in
 *  `start`: services start in product order and the Launchpad's restore (a claim) is a later package's, so a claimed
 *  port is reserved before anything of ours opens - the order the app always had (claim, then MIDI, then audio). The
 *  device itself OPENs at native's own point, after the MIDI stream and the providers' restore, so an apply here only
 *  records what it should open with.
 *
 *  The poll also watches `status()`: it is a cheap read, and a status that moves (a failed open at launch, a lost
 *  device, the watchdog) must repaint the Muted row without waiting for an unrelated render. */
const audioService: UiService = {
  id: "audio-device",
  start: () => {
    applied = false;
    lastStatus = "";
  },
  poll: {
    frames: 1,
    run: (host) => {
      let moved = false;
      if (!applied) {
        applied = true;
        applySavedAudio(host);
        moved = true;
      }
      const key = statusKey(host);
      if (key !== lastStatus) {
        lastStatus = key;
        moved = true;
      }
      return moved;
    },
  },
  subscribe: subscribeAudioDraft,
};

export default defineUi({
  menus: [audioMenu],
  services: [audioService],
});

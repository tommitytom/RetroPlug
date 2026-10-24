// The two output shapes renderBlocks does not reach: renderAudioPerSystem (each system on its own pair; the
// CLI analyze session and the SDK use it) and renderAudioPerChannel (one system's streams, a pair each; the
// render library's exports use it). Each case boots through renderBlocks, then renders one shape segment
// with held notes staged at frame 0.
import { runGroup, type ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { BOOT_MS, frames, mgb, mgbFile, ROMS } from "./scenes";
import type { AudioRouting, ChannelExportMode } from "../../src/settingsEnums";

const SR = 48000;
const boot = { blocks: even(frames(SR, BOOT_MS), 512) };

/** Notes held on `channels` (no note-off: a shape segment stages everything at frame 0). Key 48 on channel 3
 *  is in range for BlipToaster's noise voice (36..67) as well as mGB's. */
const held = (channels: number[]) => channels.map((ch, i) => ({ at: 0, bytes: [0x90 | ch, ch === 3 ? 48 : 60 + 4 * i, 100] }));

function perChannel(name: string, audible: number[], setup: (rig: Rig) => void): ParityCase {
  return { name, sampleRate: SR, outputs: 2, audible, setup, segments: [boot, { blocks: [], shape: { perChannel: 0, ms: 600 }, midi: held([0, 1, 2, 3]) }] };
}

const nesUnder = (mode: AudioRouting) => (rig: Rig) => {
  rig.add(ROMS.bliptoaster);
  rig.project.setAudioRouting(mode);
};

const roleMode = (mode: ChannelExportMode) => (rig: Rig) => {
  const id = rig.add(ROMS.bliptoaster);
  if (!rig.project.systems.setRoleConfig(id, "mesen", { channelExportMode: mode })) throw new Error(`setRoleConfig refused ${mode}`);
  if (rig.project.systems.reloadSystem(id) == null) throw new Error("the reload did not build");
};

const cases: ParityCase[] = [
  {
    // Slots 0 and 2 are both mGB: distinct gains tell them apart.
    name: "perSystem-mixed",
    sampleRate: SR,
    outputs: 2,
    audible: [0, 1, 2, 3, 4, 5],
    setup: (rig) => {
      mgb(rig);
      rig.add(ROMS.bliptoaster);
      mgbFile(rig);
      for (let i = 0; i < 3; i++) rig.project.systems.setGain(rig.id(i), -2 * i);
    },
    segments: [boot, { blocks: [], shape: { perSystem: true, ms: 600 }, midi: held([0, 1, 2, 3]) }],
  },
  {
    // One NES under ChannelSplit, rendered per system: the tap is armed but the shape asks for the mix, the
    // narrow-host defect's path (split.ts), recorded as it is.
    name: "perSystem-nes-channelSplit",
    sampleRate: SR,
    outputs: 2,
    audible: [0, 1],
    setup: nesUnder("channelSplit"),
    segments: [boot, { blocks: [], shape: { perSystem: true, ms: 600 }, midi: held([0, 1, 2, 3]) }],
  },
  // A Game Boy's four stems, a stereo pair each.
  perChannel("perChannel-mgb", [0, 1, 2, 3, 4, 5, 6, 7], (rig) => mgb(rig)),
  // A NES with no tap armed reports one stereo Mix stream.
  perChannel("perChannel-nes-mix", [0, 1], (rig) => rig.add(ROMS.bliptoaster)),
  // PinSplit arms mode 1: Pulse, TND, Expansion, mono, each on the L lane of its pair (Expansion silent on
  // the 2A03-only build).
  perChannel("perChannel-nes-pins", [0, 2], nesUnder("pinSplit")),
  // ChannelSplit arms mode 3: the five core channels, mono (DMC silent: nothing plays a sample).
  perChannel("perChannel-nes-channels", [0, 2, 4, 6], nesUnder("channelSplit")),
  // Mode 2 (pins plus the reference mix) is the one routing cannot reach; the mesen role builds any mode, as
  // the render library and the CLI export sessions do: arm at build time, default Stereo routing.
  perChannel("perChannel-nes-pinsPlusRef", [0, 2, 6], roleMode("pinsPlusRef")),
  perChannel("perChannel-nes-stereoModPins-role", [0, 2], roleMode("stereoModPins")),
  perChannel("perChannel-nes-individualMono-role", [0, 2, 4, 6], roleMode("individualMono")),
];

runGroup("render-shapes", cases);

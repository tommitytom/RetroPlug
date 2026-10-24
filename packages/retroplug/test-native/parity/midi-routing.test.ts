// The project MIDI-routing modes, over three systems whose audio lands on separate pairs (TwoPerInstance)
// so each system's share of the MIDI shows on its own lanes. Notes go out on all 16 channels, with ties,
// plus a SysEx (which every mode broadcasts unchanged). The last case switches mode mid-run, which is
// the path the setMidiRouting fix made reach the kernel.
import { runGroup, type ParityCase } from "./group";
import { even } from "./rig";
import { frames, mgb, mgbFile, BOOT_MS, phrase, ROMS } from "./scenes";
import { MIDI_ROUTING_VALUES, type MidiRouting } from "../../src/settingsEnums";

const SR = 48000;
const blocks = (n: number) => even(n, 512);
const ALL_CHANNELS = Array.from({ length: 16 }, (_, i) => i);
// phrase() over 16 channels spans just over a second; each MIDI segment is 1100 ms.
const SYSEX = { at: frames(SR, 500), bytes: [0xf0, 0x7d, 0x01, 0x02, 0x03, 0xf7] };

function setup3(mode: MidiRouting) {
  return (rig: import("./rig").Rig) => {
    mgb(rig);
    mgbFile(rig);
    rig.add(ROMS.bliptoaster);
    rig.project.setAudioRouting("twoPerInstance");
    rig.project.setMidiRouting(mode);
  };
}

const cases: ParityCase[] = MIDI_ROUTING_VALUES.map((mode) => ({
  name: mode,
  sampleRate: SR,
  outputs: 8 as const,
  // Four channels per instance hands the NES channels 8..11, and it listens on 0..3: it stays silent.
  audible: mode === "fourChannelsPerInstance" ? [0, 1, 2, 3] : [0, 1, 2, 3, 4, 5],
  setup: setup3(mode),
  segments: [
    { blocks: blocks(frames(SR, BOOT_MS)) },
    { blocks: blocks(frames(SR, 1100)), midi: [...phrase(SR, ALL_CHANNELS), SYSEX] },
  ],
}));

cases.push({
  name: "sendToAll-then-oneChannelPerInstance",
  sampleRate: SR,
  outputs: 8,
  audible: [0, 1, 2, 3, 4, 5],
  setup: setup3("sendToAll"),
  segments: [
    { blocks: blocks(frames(SR, BOOT_MS)) },
    { blocks: blocks(frames(SR, 1100)), midi: phrase(SR, ALL_CHANNELS), after: (rig) => void rig.project.setMidiRouting("oneChannelPerInstance") },
    { blocks: blocks(frames(SR, 1100)), midi: phrase(SR, ALL_CHANNELS) },
  ],
});

runGroup("midi-routing", cases);

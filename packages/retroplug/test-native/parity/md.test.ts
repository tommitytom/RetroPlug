// The Mega Drive group: the synthetic images the native cases boot, played through the store's own detection and
// construct path, plus the port-2 device's timing proved natively (BlastemPort.test.cpp) and end to end on the real
// cart (genmddj-sync.test.ts, the QuickJS host's). Cases are compared byte for byte against baseline/md.json on
// linux-x64-gnu13 (spec/06's parity rule), so one case per image: a change to the PSG, the YM2612, the machine's
// own timing or the one-sample stop grid moves a digest here even where the native invariance cases still hold.
//
// Each case renders a boot segment (the recorder asks only whether a lane sounds AFTER it) and then the partition the
// baseline is held at. Every image plays from power-on and every digest is a function of absolute time, so the group
// holds the machine's output at a partition the sizes straddle, not at one size only.
import { runGroup, type ParityCase } from "./group";
import { mdSynthetic } from "./scenes";
import {
  fmPsgRom, psgOnlyRom, fmOnlyRom, hvPsgRom, vintHintRom, z80TimingRom, vintDmaRom,
} from "@retroplug/blastem/testFixtures";

/** The power-on, in the machine's own block size: short, because these images write their chip registers in the first
 *  instructions after reset. */
const BOOT = [512, 512, 512, 512];

/** The partition the baseline is held at: sizes that straddle the one-sample stop grid (1 frame) and the sizes a DAW
 *  hands the engine (4096, 1024, 64), an odd one between them, then the machine's own 512-frame blocks. */
const MIXED = [1, 64, 1024, 4096, 7, 512, 512, 512, 512, 512];

/** The images and what each one holds: `fmPsgRom` is PSG-only in effect (its YM writes never land without a Z80 bus
 *  request), and its case earns its place by pinning the path those writes take: the thirty refused YM writes delay
 *  the PSG tone by a sub-sample, so the digest moves where the recorded rms stays the PSG's, and `fm` in the name is
 *  not FM coverage. `fmOnlyRom` is the one whose YM2612 channel actually sounds, and the four timing images watch
 *  their own VDP counters, take interrupts, run a Z80 program behind a bus request, and start a 68k-to-VDP DMA from
 *  the VINT. */
const IMAGES: [string, () => Uint8Array][] = [
  ["fm-psg", fmPsgRom],
  ["psg", psgOnlyRom],
  ["fm", fmOnlyRom],
  ["hv-psg", hvPsgRom],
  ["vint-hint", vintHintRom],
  ["z80-timing", z80TimingRom],
  ["vint-dma", vintDmaRom],
];

const cases: ParityCase[] = IMAGES.map(([name, image]) => ({
  name,
  outputs: 2 as const,
  audible: [0, 1], // every image drives both lanes: the PSG and the YM2612 both answer stereo
  setup: (rig) => {
    mdSynthetic(rig, image(), name);
  },
  segments: [{ blocks: BOOT }, { blocks: MIXED }],
}));

runGroup("md", cases);

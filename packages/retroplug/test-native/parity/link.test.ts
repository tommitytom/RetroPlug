// Linked Game Boys: a link group steps its members as one unit (round-robin, so the cable stays in
// lockstep), which is why B1 models a group as one coupled node rather than several. Two linked mGBs,
// with a third unlinked; then a link made, and broken, mid-run (linkGroupId is a live setting).
//
// mGB takes its MIDI over the serial port, which a link cable occupies, so a linked mGB hears no host
// MIDI at all: these pin how linked systems render, and the MIDI shows only on unlinked ones. Link SYNC,
// where the cable carries a clock, is the LSDj group's.
import { runGroup, type ParityCase } from "./group";
import { even, type Rig } from "./rig";
import { BOOT_MS, frames, mgb, mgbFile, phrase } from "./scenes";

const SR = 48000;
const blocks = (n: number) => even(n, 512);
const link = (rig: Rig, slot: number, group: number) => void rig.project.systems.setRoleConfig(rig.id(slot), "sameboy", { linkGroupId: group });
const boot = () => ({ blocks: blocks(frames(SR, BOOT_MS)) });
const play = (ms = 1000) => ({ blocks: blocks(frames(SR, ms)), midi: phrase(SR, [0, 1, 2]) });

const cases: ParityCase[] = [
  {
    name: "two-linked",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1, 2, 3],
    setup: (rig) => {
      mgb(rig);
      mgbFile(rig);
      link(rig, 0, 1);
      link(rig, 1, 1);
      rig.project.setAudioRouting("twoPerInstance");
    },
    segments: [boot(), { blocks: blocks(frames(SR, 1000)) }],
  },
  {
    name: "two-linked-one-not",
    sampleRate: SR,
    outputs: 8,
    audible: [0, 1, 2, 3, 4, 5],
    setup: (rig) => {
      mgb(rig);
      mgbFile(rig);
      mgbFile(rig);
      link(rig, 0, 1);
      link(rig, 2, 1);
      rig.project.setAudioRouting("twoPerInstance");
    },
    segments: [boot(), play()],
  },
  {
    name: "link-then-unlink",
    sampleRate: SR,
    outputs: 4,
    audible: [0, 1, 2, 3],
    setup: (rig) => {
      mgb(rig);
      mgbFile(rig);
      rig.project.setAudioRouting("twoPerInstance");
    },
    segments: [
      boot(),
      { ...play(600), after: (rig) => (link(rig, 0, 1), link(rig, 1, 1)) },
      { ...play(600), after: (rig) => link(rig, 1, 0) },
      play(600),
    ],
  },
];

runGroup("link", cases);

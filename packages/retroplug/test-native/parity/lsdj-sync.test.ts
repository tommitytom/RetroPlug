// LSDj following the host clock: SYNC=MIDI armed by START (or by autoStart on the transport rise), at
// every tempo divisor, on the stock ROM and the Arduinoboy build. lsdj.ts has the scenes.
import { runGroup, type ParityCase } from "./group";
import { blocks, denseSongSav, lsdj, lsdjCase, SR, tap } from "./lsdj";
import { frames, ROMS } from "./scenes";
import { even } from "./rig";

const transport = [
  { blocks: blocks(frames(SR, 2000)), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }] },
  { blocks: blocks(frames(SR, 500)), transport: [{ at: 0, playing: false }] },
];

const cases: ParityCase[] = [];
for (const d of [1, 2, 4, 8])
  cases.push(
    lsdjCase(`midiSync-div${d}`, ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, denseSongSav("Midi"), { mode: "midiSync", tempoDivisor: d }), [
      ...tap(),
      ...transport,
    ]),
  );
cases.push(
  lsdjCase("midiSync-autoStart", ROMS.lsdj, (rig) => lsdj(rig, ROMS.lsdj, denseSongSav("Midi"), { mode: "midiSync", autoStart: true }), transport),
);
// The Arduinoboy protocol: the cart sits in SYNC=LSDJ as a slave, the transport sends start / stop, and
// note 24 enables the clock (note 25 disables it).
//
// KNOWN DEFECT, recorded as it is: LSDj plays only when host blocks carry clock ticks. At 120 bpm and
// divisor 1 (24 PPQN) a tick is ~1000 frames long, and the threshold is that tick's length (1000-frame
// blocks play), so at 128 to 768 (ordinary DAW buffers) LSDj never starts, a divisor of 4 never starts at
// 1024 either, and 2048 plays louder than 1024. "Every block carries a tick" is neither necessary (the
// ragged `fl` split of blocks-tiny-lsdj plays with six of its seven blocks carrying none) nor
// sufficient: divisor 4 at 4096 frames is silent while divisors 4 and 8 at 8192 play, so a fix built only on
// that model may be incomplete. The silent cases pin all this; fixing it is its own change, with a
// deliberate re-record of these cases.
const arduinoboy = (bs: number): ParityCase["segments"] => {
  const b = (ms: number) => even(frames(SR, ms), bs);
  return [
    { blocks: b(1000), transport: [{ at: 0, bpm: 120, ppq: 0, playing: true }] },
    { blocks: b(2000), midi: [{ at: 0, bytes: [0x90, 24, 100] }] },
    { blocks: b(500), midi: [{ at: 0, bytes: [0x90, 25, 100] }], transport: [{ at: 0, playing: false }] },
  ];
};
for (const d of [1, 4])
  cases.push(
    lsdjCase(
      `midiSyncArduinoboy-div${d}-1024`,
      ROMS.arduinoboy,
      (rig) => lsdj(rig, ROMS.arduinoboy, denseSongSav("Lsdj"), { mode: "midiSyncArduinoboy", tempoDivisor: d }),
      arduinoboy(1024),
      2,
      d === 1 ? [0, 1] : [], // divisor 4: silent, the defect above
    ),
  );
cases.push(
  lsdjCase(
    "midiSyncArduinoboy-div1-512",
    ROMS.arduinoboy,
    (rig) => lsdj(rig, ROMS.arduinoboy, denseSongSav("Lsdj"), { mode: "midiSyncArduinoboy" }),
    arduinoboy(512),
    2,
    [], // silent: the defect above
  ),
);

runGroup("lsdj-sync", cases);

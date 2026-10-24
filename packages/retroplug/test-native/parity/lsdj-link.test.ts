// Two LSDj on one link cable, both SYNC=LSDJ: START on the first makes it the leader, and the follower
// plays only once the cable's clock reaches it. Each on its own pair, so the follower's sync shows.
import { runGroup, type ParityCase } from "./group";
import { blocks, lsdj, lsdjCase, songSav, SR, tap } from "./lsdj";
import { frames, ROMS } from "./scenes";

const cases: ParityCase[] = [
  lsdjCase(
    "leader-follower",
    ROMS.lsdj,
    (rig) => {
      lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "off" });
      lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "off" });
      for (const slot of [0, 1]) rig.project.systems.setRoleConfig(rig.id(slot), "sameboy", { linkGroupId: 1 });
      rig.project.setAudioRouting("twoPerInstance");
    },
    [...tap(0), { blocks: blocks(frames(SR, 3000)) }],
    4,
  ),
];

runGroup("lsdj-link", cases);

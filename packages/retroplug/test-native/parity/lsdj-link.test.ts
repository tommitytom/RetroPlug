// Two LSDj on one link cable, both SYNC=LSDJ: START on the first makes it the leader, and the follower
// plays only once the cable's clock reaches it. Each on its own pair, so the follower's sync shows. Linked
// cores step in lockstep (their coupled group round-robins them until both reach the block boundary), so
// the boundary is part of what the follower hears: the scene runs in every host block size blocks.ts
// renders, and the follower's lanes differ in each. Recorded before B1 moved that stepping onto the graph
// executor, which holds it to the bit.
import { runGroup, type ParityCase } from "./group";
import { SPLITS } from "./blocks";
import { blocks, lsdj, lsdjCase, songSav, SR, tap } from "./lsdj";
import { frames, ROMS, type Blocks } from "./scenes";

function leaderFollower(name: string, split: Blocks): ParityCase {
  return lsdjCase(
    name,
    ROMS.lsdj,
    (rig) => {
      lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "off" });
      lsdj(rig, ROMS.lsdj, songSav("Lsdj"), { mode: "off" });
      for (const slot of [0, 1]) rig.project.systems.setRoleConfig(rig.id(slot), "sameboy", { linkGroupId: 1 });
      rig.project.setAudioRouting("twoPerInstance");
    },
    [...tap(0, SR, split), { blocks: split(frames(SR, 3000)) }],
    4,
    [0, 1, 2, 3],
    SR,
    split,
  );
}

// 512 frames keeps the case's original name.
const cases: ParityCase[] = [
  leaderFollower("leader-follower", blocks),
  ...SPLITS.filter(([n]) => n !== "512").map(([n, split]) => leaderFollower(`leader-follower-${n}`, split)),
];

runGroup("lsdj-link", cases);

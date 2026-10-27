// smsggdj's host sync (the `sms-sync` role's DSP behaviour, dspRoles.ts, as a script node): while the DAW plays, a
// 2-bit counter advanced one step a 24-PPQN tick, each step a LEVEL WORD at the tick's own frame, which the system
// holds on the sync lines until the next (smsSync.ts). `machine` picks which lines: controller port 2's TR + TH on a
// Master System, the EXT port's PC4/PC5 + PC6 on a Game Gear. No arm, no locate, no start or stop: the ROM reads
// deltas, and latches the lines itself when it arms.
//
// The role's glue, line for line, with its counter and its system's tick counter on the instance. A block allocates
// nothing.
import { bytes, defineNode, type Block } from "../../graph/defineNode";
import { s, type Infer } from "../../graph/settingsSchema";
import { SMS_SYNC_COUNTER_MOD, SMS_SYNC_PPQN, syncLevelsFor } from "../../smsSync";
import { TickClock } from "./clock";
import { CORE_BYTES } from "./ports";

export const SMS_SYNC_NODE_TYPE = "rp.sms.sync";

const settings = s.object({ machine: s.enum(["sms", "gg"], "sms") });

export type SmsSyncSettings = Infer<typeof settings>;

export const smsSyncNode = defineNode<SmsSyncSettings>({
  id: SMS_SYNC_NODE_TYPE,
  name: "smsggdj Host Sync",
  category: "protocol",
  settings,
  reshapes: true,
  ports: { bytes: bytes.out(CORE_BYTES) },
  create: ({ settings: initial }) => {
    let machine = initial.machine;
    const clock = new TickClock();
    // The counter is free-running and its absolute value is irrelevant (the ROM diffs it), so it is never reset, only
    // advanced. A walk advances `walking`, which becomes the counter once the walk is done, as the role kept it.
    let counter = 0;
    let walking = 0;
    let levels = syncLevelsFor(machine);
    let block: Block | null = null;
    const level = new Uint8Array(1);
    const tick = (_t: number, off: number) => {
      walking = (walking + 1) % SMS_SYNC_COUNTER_MOD;
      level[0] = levels(walking);
      block!.send("bytes", off, level);
    };
    return {
      process(b) {
        block = b;
        clock.begin(b);
        if (!clock.block.transport) return;
        levels = syncLevelsFor(machine);
        walking = counter;
        clock.walk(SMS_SYNC_PPQN, tick);
        counter = walking;
      },
      onSettings(next) {
        machine = next.machine;
      },
    };
  },
});

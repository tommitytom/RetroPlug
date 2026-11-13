// smsggdj's host sync (the `sms-sync` role's audio-thread work; this package's app.ts registers the role, and buildGraph
// places this node for it): while the DAW plays, a 2-bit counter advanced one step a 24-PPQN tick, each step a LEVEL WORD at
// the tick's own frame, which the system holds on the sync lines until the next (smsSync.ts). `machine` picks which
// lines: controller port 2's TR + TH on a Master System, the EXT port's PC4/PC5 + PC6 on a Game Gear. No arm, no
// locate, no start or stop: the ROM reads deltas, and latches the lines itself when it arms.
//
// The glue the role ran under the DSP kernel, ported line for line, with its counter and its system's tick counter on
// the instance. A block allocates nothing.
import { bytes, defineNode, type Block } from "@retroplug/core/defineNode";
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { SMS_SYNC_PPQN, counterSyncFor, syncLevelsFor } from "./smsSync";
import { TickClock } from "@retroplug/core/clock";
import { CORE_BYTES } from "@retroplug/core/ports";

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
    // advanced. The CounterSync lives in the closure and is NEVER rebuilt on a settings change: its counter is the state
    // the ROM reads deltas of, and a rebuilt machine would restart it at 0 and hand the cart a spurious delta. A walk
    // advances it, which becomes the counter once the walk is done, as the role kept it.
    const sync = counterSyncFor(machine);
    let block: Block | null = null;
    const level = new Uint8Array(1);
    const tick = (_t: number, off: number) => {
      level[0] = sync.tick();
      block!.send("bytes", off, level);
    };
    return {
      process(b) {
        block = b;
        clock.begin(b);
        if (!clock.block.transport) return;   // a stopped transport sends nothing and keeps the counter
        sync.use(syncLevelsFor(machine));     // a machine change encodes the next step on the new pins
        sync.begin();
        clock.walk(SMS_SYNC_PPQN, tick);
        sync.end();
      },
      onSettings(next) {
        machine = next.machine;
      },
    };
  },
});

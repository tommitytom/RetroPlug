// genmddj's host sync (`genmddj-sync`'s audio-thread work; this package's app.ts registers the role, and buildGraph
// places this node for it): while the DAW plays, a 2-bit counter advanced one step a 24-PPQN tick, each step a LEVEL
// WORD at the tick's own frame, which the system holds on controller port 2's TR and TH until the next (mdSync.ts).
// The state machine is smsggdj's, with genmddj's encoder: no arm, no locate, no start or stop message, because the ROM
// reads deltas and latches the lines itself when it arms.
//
// Port 2 is every Mega Drive game's player 2, so the system opens it only for a cart whose role placed this node
// (buildGraph writes it as the node's own claim, and BlastemMdSystem answers a staged or routed message with a pad's
// idle lines until then).
//
// A block allocates nothing (the engine's [alloc] cases hold it).
import { bytes, defineNode, type Block } from "@retroplug/core/defineNode";
import { s, type Infer } from "@retroplug/core/settingsSchema";
import { TickClock } from "@retroplug/core/clock";
import { CORE_BYTES } from "@retroplug/core/ports";
import { CounterSync } from "@retroplug/smsggdj/smsSync";
import { GENMDDJ_SYNC_COUNTER_MOD, GENMDDJ_SYNC_PPQN, mdSyncLevels } from "./mdSync";

export const GENMDDJ_SYNC_NODE_TYPE = "rp.genmddj.sync";

const settings = s.object({});

export type GenmddjSyncSettings = Infer<typeof settings>;

export const genmddjSyncNode = defineNode<GenmddjSyncSettings>({
  id: GENMDDJ_SYNC_NODE_TYPE,
  name: "genmddj Host Sync",
  category: "protocol",
  settings,
  reshapes: true,
  ports: { bytes: bytes.out(CORE_BYTES) },
  create: () => {
    const clock = new TickClock();
    // The counter is free-running and its absolute value is irrelevant (the ROM diffs it), so it is never reset, only
    // advanced. A walk advances it, which becomes the counter once the walk is done: a stopped transport walks nothing,
    // so the counter carries across one rather than restarting.
    const sync = new CounterSync(mdSyncLevels, GENMDDJ_SYNC_COUNTER_MOD);
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
        sync.begin();
        clock.walk(GENMDDJ_SYNC_PPQN, tick);
        sync.end();
      },
      onSettings() {},
    };
  },
});

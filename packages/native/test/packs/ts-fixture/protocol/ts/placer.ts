// The protocol fixture pack's placer (docs/downloadable-packs-m2e-plan.md): places nothing, and says on the console
// where buildGraph placed the pack's role, so a render's output shows the protocol data and the placer both committed
// in its worker.
import { definePlacer } from "@retroplug/core/placer";

export default definePlacer<unknown>({
  key: "tsfixProto",
  place(_model, ctx) {
    for (const s of ctx.systems)
      for (const r of s.pipeline) if (r.kind === "tsfix-proto") console.log(`tsfix-proto: placed ${r.node} on system ${s.id}`);
    return {};
  },
});

// A device pack whose process() calls startsWith and Array.from, which MicroQuickJS does not have, while the node SDK
// it bundles declares functions of those names (defineNode's startsWith, settingsSchema's `from`): a declaration
// elsewhere must not make a method call the pack's own. The device-pack toolchain must refuse it, naming each call and
// its line (test/graph/device-pack.test.ts).
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

export const sysexOnly = defineNode({
  id: "rp.test.device.sysex-only",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => ({
    process(b) {
      const ms = b.messages("in");
      for (let i = 0; i < ms.length; i++) {
        const hex = ms[i].data[0].toString(16);
        if (hex.startsWith("f0")) b.send("out", ms[i].frame, Array.from(ms[i].data));
      }
    },
  }),
});

registerScriptNodes([sysexOnly]);

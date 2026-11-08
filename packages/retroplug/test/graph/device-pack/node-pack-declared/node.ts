// A node pack whose manifest DECLARES the package it imports: a node pack's allowlist is still its own directory and
// the portable node SDK, so the toolchain must refuse the import, naming the module and its importer
// (test/graph/device-pack.test.ts). Only a scenario may reach its declared dependencies.
import { decodeMessage } from "@retroplug/launchpad/decode";
import { PRO_MK3 } from "@retroplug/launchpad/profile";
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

export const decode = defineNode({
  id: "rp.test.device.decode",
  ports: { in: events.in() },
  create: () => ({
    process(b) {
      const messages = b.messages("in");
      for (let i = 0; i < messages.length; i++) decodeMessage(PRO_MK3, messages[i].data);
    },
  }),
});

registerScriptNodes([decode]);

// The hog pack: a node that keeps what it makes until its pack's heap runs out, beside one that makes little, for
// ScriptPack.test.cpp's case that a heap running out silences its own pack's nodes and no other's; and a node that
// empties the array messages() hands it every block, which the host must then grow again on a rooted slot, run in a
// heap that collects at every allocation. A test fixture of the device-pack toolchain
// (packages/native/cmake/tests.cmake), never built by a product.
import { defineNode, events } from "../../../../../retroplug/src/graph/defineNode";
import { s } from "../../../../../retroplug/src/graph/settingsSchema";
import { registerScriptNodes } from "../../../../../retroplug/src/script/register";

// It keeps what it makes only when its settings say `grow`, so the toolchain's heap check, which runs every node on
// its default settings, passes the pack.
export const hogNode = defineNode({
  id: "rp.hog.hog",
  settings: s.object({ grow: s.bool(false) }),
  ports: { out: events.out() },
  create: ({ settings }) => {
    const kept: number[][] = [];
    return {
      process() {
        if (!settings.grow) return;
        for (let i = 0; i < 4; i++) kept.push([i, i, i, i, i, i, i, i]);
      },
    };
  },
});

export const quietNode = defineNode({
  id: "rp.hog.quiet",
  ports: { out: events.out() },
  create: () => {
    let blocks = 0;
    return {
      process(b) {
        blocks++;
        b.send("out", 0, [blocks & 0x7f]);
      },
    };
  },
});

// Each message on, then the array emptied: its storage freed, so the next block's array is grown again by the host.
export const emptierNode = defineNode({
  id: "rp.hog.emptier",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => ({
    process(b) {
      const messages = b.messages("in");
      for (let i = 0; i < messages.length; i++) b.send("out", messages[i].frame, messages[i].data, messages[i].flags);
      (messages as unknown as unknown[]).length = 0;  // what the contract allows a node to do with its own array
    },
  }),
});

registerScriptNodes([hogNode, quietNode, emptierNode]);

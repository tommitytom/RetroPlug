// The devtest script pack (packages/devtest/package.json's retroplug.script): a device pack of test node types, which
// the device-pack toolchain (packages/retroplug/scripts/build-device-pack.mjs) compiles to MicroQuickJS bytecode
// through cmake/packages.cmake's retroplug_script_pack. Like the package's C++ types, no product that ships carries it;
// the tests build it, at both widths (packages/retroplug/test/graph/device-pack.test.ts).
import { defineNode, events } from "../../retroplug/src/graph/defineNode";
import { s } from "../../retroplug/src/graph/settingsSchema";
import { registerScriptNodes } from "../../retroplug/src/script/register";

const clamp7 = (v: number): number => (v < 0 ? 0 : v > 127 ? 127 : v);

// A note on or off moved by `semitones` and held to 0..127; every other message passed on as it came. Each keeps its
// frame and flags, and the output's capacity is its input's. A moved note goes out from a three-byte array of the
// node's own, refilled for each, so a warmed block allocates nothing.
export const scriptTransposeNode = defineNode({
  id: "rp.devtest.script-transpose",
  name: "Script transpose (devtest)",
  category: "test",
  settings: s.object({ semitones: s.int({ min: -127, max: 127, default: 0 }) }),
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: ({ settings }) => {
    let semitones = settings.semitones;
    const note = new Uint8Array(3);
    return {
      onSettings(next) {
        semitones = next.semitones;
      },
      process(b) {
        const messages = b.messages("in");
        for (let i = 0; i < messages.length; i++) {
          const m = messages[i];
          const kind = m.data[0] & 0xf0;
          if ((kind === 0x80 || kind === 0x90) && m.data.length === 3) {
            note[0] = m.data[0];
            note[1] = clamp7(m.data[1] + semitones);
            note[2] = m.data[2];
            b.send("out", m.frame, note, m.flags);
          } else {
            b.send("out", m.frame, m.data, m.flags);
          }
        }
      },
    };
  },
});

registerScriptNodes([scriptTransposeNode]);

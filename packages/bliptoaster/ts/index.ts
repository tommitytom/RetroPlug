// The bliptoaster script pack (packages/bliptoaster/package.json's retroplug.script,
// docs/modular-graph-phase-e4-design.md section 6): the BlipToaster cart's MIDI policy, a device pack the box's
// built-in plan runs between its MIDI input and the cart (pico/n8-host/plan.json), and the desktop products list so it
// can be auditioned there. The device-pack toolchain (packages/retroplug/scripts/build-device-pack.mjs) compiles it to
// MicroQuickJS bytecode, at 64 bits for the desktop's host and at 32 for the box's.
import { defineNode, events } from "@retroplug/core/defineNode";
import { registerScriptNodes } from "@retroplug/core/register";

// What BlipToaster's FIFO is not fed: polyphonic and channel aftertouch (high-rate streams a controller floods and no
// ROM here reads), the system common messages but a SysEx (F1 to F6) and the real-time ones (F8 to FF), which the
// bridge never forwarded. Every other message passes as it came, its frame, flags and bytes, in order: a SysEx (F0,
// or a lone F7), and a message of no kind (one whose first byte is a data byte, or an empty one), as
// rp.core.midi-filter passes them. A status is read from the message's first byte alone, with no running status
// carried across messages, which is rp.core.midi-filter's reading too: this node drops exactly what that filter drops
// with `drop: ["polyAftertouch", "channelAftertouch", "systemCommon", "realtime"]`, message for message. The test is
// written out in the loop rather than called, as a call is a frame of the interpreter's for every message.

// The policy has no settings. Its output is sized as its input's queue (capacity.match), so a stream it passes whole
// is never one it drops. Each message's first byte is read once, and what passes goes on through forward, which the
// host makes (its frame, flags and bytes as they came, copied at the call), so a warmed block allocates nothing for a
// message of up to three bytes, at either width. It keeps nothing between blocks and sends only what a block brought
// it, so the host skips it on a block with no message (idle: "skip"), the box's every pass between two.
export const midiPolicyNode = defineNode({
  id: "rp.bliptoaster.midi-policy",
  name: "BlipToaster MIDI policy",
  category: "bliptoaster",
  idle: "skip",
  ports: { in: events.in(), out: events.out({ match: "in" }) },
  create: () => ({
    process(b) {
      const messages = b.messages("in");
      const count = messages.length;
      for (let i = 0; i < count; i++) {
        const m = messages[i];
        // The first byte; undefined for an empty message, which then matches no kind below and passes.
        const status = m.data[0];
        const high = status & 0xf0;
        if (high === 0xa0 || high === 0xd0 || (status > 0xf0 && status < 0xf7) || status >= 0xf8) continue;
        b.forward("out", m);
      }
    },
  }),
});

registerScriptNodes([midiPolicyNode]);

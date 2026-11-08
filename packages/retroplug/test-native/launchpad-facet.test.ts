// The Launchpad facet through the real native host: the pack's provider and its RPC facet over a SCRIPTED device
// (packages/native/src/main.cpp starts it with packages/launchpad/native/test/FakePorts.hpp's rig - a Pro MK3 on
// "LPProMK3 MIDI" both ways, which answers the Universal Device Inquiry and its own layout query, and an interface's port
// that answers nothing). No hardware, no rtmidi, and the same facet code the standalone runs.
//
// What only this file can hold: that the facet's answers reach TS over the real RPC seam, and that what TS decides reaches
// the PRODUCT (rp_device_host's reserve_midi, suspend_hardware_inputs and restored, recorded by the headless host's
// ProviderHost and drained through the engine facet). The claim's cross-process half is the pack's own case in C++
// (Controller.test.cpp, over a real config dir), and the standalone's `pnpm sdl:controller` leg 5 is the two-PROCESS one.
//
// Run: pnpm test:native launchpad-facet
import { test, expect } from "@retroplug/testing";
import { createAudioDriver } from "../src/audioDriver";
import { createLaunchpadClient, type LaunchpadClient } from "@retroplug/launchpad/client";
import {
  PRO_MK3, deviceInquiry, exitToLiveMode, isModelReply, modelQuery, parseInquiryReply, profilesForFamily,
} from "@retroplug/launchpad";
import { PortScan } from "@retroplug/launchpad/portScan";
import { createRealBackend } from "../src/realBackend";
import { makeCall } from "../src/rpcClient";
import { toGraphJson } from "@retroplug/core/graphDocument";

const PRO = "LPProMK3 MIDI";
const IFACE = "MIDISPORT 2x2 Port A";

function facet(): LaunchpadClient {
  return createLaunchpadClient(makeCall());
}

/** A scan over the facet, driven to its end by a clock the test advances (the standalone's frame poll does this). */
function scanToEnd(
  client: LaunchpadClient,
): { found: { input: string; output: string; name: string } | null; wrote: number } {
  const ports = client.ports();
  const api = {
    openInputs: (names: readonly string[]) => client.openInputs(names.slice()),
    openOutput: (name: string) => client.openOutput(name),
    send: (h: number, bytes: readonly number[]) => client.sendOn(h, Uint8Array.from(bytes)),
    read: (h: number) => client.read(h).messages.map((m) => Array.from(m)),
    close: (h: number) => void client.close(h),
  };
  const scan = new PortScan(api, ports.inputs, ports.outputs, deviceInquiry(), 300, {
    followUp: (reply) => {
      const parsed = parseInquiryReply(reply.bytes);
      return parsed ? profilesForFamily(parsed.family).map((p) => modelQuery(p)) : [];
    },
  });
  let now = 1000;
  scan.step(now); // the first probe goes out now
  for (let i = 0; i < 1000 && scan.status().busy; i++) {
    now += 16;
    scan.step(now);
  }
  const st = scan.status();
  let found: { input: string; output: string; name: string } | null = null;
  for (const r of st.replies) {
    const parsed = parseInquiryReply(r.bytes);
    if (!parsed) continue;
    for (const profile of profilesForFamily(parsed.family))
      if (st.replies.some((x) => x.output === r.output && x.input === r.input && isModelReply(profile, x.bytes)))
        found = { input: r.input, output: r.output, name: profile.name };
  }
  return { found, wrote: now - 1000 };
}

test("the facet lists the scripted device's ports only, so a scan never targets one that is not a device", () => {
  // The host's rig also lists RetroPlug's own virtual ports, a second instance's `RetroPlug Launchpad` link ports and
  // ALSA's Midi Through (packages/native/src/main.cpp); the facet drops all three, which is what a scan's sweep is handed.
  // A scan writes a Device Inquiry out of every output it is given, so this list is the guard.
  const client = facet();
  const ports = client.ports();
  expect(ports.inputs).toEqual([PRO, IFACE]);
  expect(ports.outputs).toEqual([PRO, IFACE]);
  expect(client.status().connected).toBe(false);
});

test("a scan through the facet finds and confirms the model on the pair that answered", () => {
  const client = facet();
  const { found, wrote } = scanToEnd(client);
  expect(found).toEqual({ input: PRO, output: PRO, name: PRO_MK3.name });
  // Every port was probed, and each window ran its course: milliseconds, not a wall-clock stall.
  expect(wrote >= 300 * 2).toBe(true);
});

test("a claim keeps the pair out of the product's shared MIDI stream, and a release gives it back", () => {
  const audio = createAudioDriver();
  audio.drainProductCalls(); // whatever the host's own startup did, this case reads from here
  const client = facet();
  const held = client.claim(PRO, PRO, Uint8Array.from(exitToLiveMode(PRO_MK3)));
  expect(held.connected).toBe(true);
  // A relative count: the provider is the process's, so an earlier case's claims are behind it.
  expect(held.generation > 0).toBe(true);
  expect(held.input).toBe(PRO);
  // The facet lets the OLD pair go first (nothing, here), then reserves the pick BEFORE it opens it, so the stream never
  // holds a port the surface is about to take. ONE reservation, not two: there is no empty pair in between, which is what
  // makes a swap a single audio restart. The record is what the facet SAID.
  expect(audio.drainProductCalls()).toEqual([`reserve_midi:${PRO}|${PRO}`]);

  const given = client.release();
  expect(given.connected).toBe(false);
  expect(given.generation).toBe(0);
  // Release reserves empty: the pair rejoins the shared stream.
  expect(audio.drainProductCalls()).toEqual(["reserve_midi:|"]);
});

test("a claim the lister cannot see is refused, and reserves nothing for it", () => {
  const audio = createAudioDriver();
  audio.drainProductCalls();
  const client = facet();
  const held = client.claim("Some Synth", "Some Synth", Uint8Array.from(exitToLiveMode(PRO_MK3)));
  expect(held.connected).toBe(false);
  expect(held.error).toBe("not connected: Some Synth");
  // Presence is checked before the pick is reserved: the refusal hands the reservation back and names no pair, so the
  // stream is never reopened for a stale pick, and a port another instance holds stays out of this one's stream until it
  // is released.
  expect(audio.drainProductCalls()).toEqual(["reserve_midi:|"]);
});

test("the facet's restored reaches the product: the rendezvous's end", () => {
  const audio = createAudioDriver();
  audio.drainProductCalls();
  const client = facet();
  expect(client.restored()).toBe(true);
  expect(audio.drainProductCalls()).toEqual(["restored"]);
});

test("a scan holds the stream's inputs, then gives them back", () => {
  const audio = createAudioDriver();
  audio.drainProductCalls();
  const client = facet();
  const input = client.openInputs([PRO])[0] ?? 0;
  expect(input > 0).toBe(true);
  expect(audio.drainProductCalls()).toEqual(["suspend_hardware_inputs:1"]);
  expect(client.close(input)).toBe(true);
  expect(audio.drainProductCalls()).toEqual(["suspend_hardware_inputs:0"]);
});

test("a claim that replaces a claim lets the old pair go before the new one is reserved", () => {
  const audio = createAudioDriver();
  const client = facet();
  const first = client.claim(PRO, PRO, Uint8Array.from(exitToLiveMode(PRO_MK3)));
  expect(first.connected).toBe(true);
  audio.drainProductCalls();
  const second = client.claim(IFACE, IFACE, Uint8Array.from(exitToLiveMode(PRO_MK3)));
  expect(second.connected).toBe(true);
  // The session's `connected` value, so a pair swapped inside one block reads as a NEW device. A relative count: the
  // provider is the process's, so an earlier case's claims are behind it.
  expect(second.generation).toBe(first.generation + 1);
  // One reservation for the swap: the old pair's gave way to the new one directly, with no empty pair between them (which
  // would be two reopens of the shared stream, and two audio restarts, for one swap).
  expect(audio.drainProductCalls()).toEqual([`reserve_midi:${IFACE}|${IFACE}`]);
  client.release();
  audio.drainProductCalls();
});

test("the fake device is not a claim on the graph: the harness surface still drives the document", () => {
  // The headless host's graph control surface is its harness link (stageControllerIn), and the facet's scripted device is
  // separate: a plan's surface nodes are not silently fed by a device a test claimed. Held to what the PLAN received, not
  // to stageControllerIn's return value: a document carries the press from the surface node to the host's MIDI out.
  const audio = createAudioDriver();
  const be = createRealBackend();
  const client = facet();
  expect(client.claim(PRO, PRO, Uint8Array.from(exitToLiveMode(PRO_MK3))).connected).toBe(true);
  const document = toGraphJson({
    nodes: [
      { id: "out", type: "rp.host.audio-out", settings: { buses: [1, 1] } },
      { id: "surface-in", type: "rp.launchpad.surface-in" },
      { id: "midi-out", type: "rp.host.midi-out", settings: { systems: [0] } },
    ],
    wires: [{ from: "surface-in.in", to: "midi-out.in1" }],
  });
  const placed = be.setGraph({ document, slots: [] });
  expect(placed.ok, JSON.stringify(placed.errors)).toBe(true);
  audio.renderBlocks(2, [{ frames: 64 }]); // the surface node binds the link and discards what it holds
  expect(audio.stageControllerIn([0x90, 0x51, 0x7f])).toBe(true);
  const delivered = audio.renderBlocks(2, [{ frames: 64 }]);
  expect(delivered.midiOut.length).toBe(1);
  expect(Array.from(delivered.midiOut[0].data)).toEqual([0x90, 0x51, 0x7f]);
  client.release();
});

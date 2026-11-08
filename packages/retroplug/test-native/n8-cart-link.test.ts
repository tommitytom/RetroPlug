// The headless host installs the N8 provider's cart link in the engine's registry, as a product's provider does
// (packages/native/src/main.cpp's ProviderHost, docs/modular-graph-phase-e2-design.md section 6). A plan that places
// rp.n8.nes acquires the link BY NAME from that registry (rp.host.devices/2), so a host that gives the provider a seat
// and installs nothing leaves the node on its "no cart" stub: it plans, it renders, and every byte and every capability
// job it takes goes nowhere. That is a wiring mistake no other test:native case saw, because nothing else here places a
// cart node.
//
// What distinguishes the two from TS: rp.n8.nes's capability jobs. The stub (rp::n8::NoCart) refuses every one of them
// "not connected"; the real link's own answer comes from N8Host, which refuses a battery job that names no ROM (and no
// ROM has been booted here) BEFORE it looks at whether the cart is streaming. So the words of that refusal are the
// proof that the registry held the link.
//
// Run: pnpm test:native n8-cart-link
import { test, expect } from "@retroplug/testing";
import { createAudioDriver } from "../src/audioDriver";
import { createRealBackend } from "../src/realBackend";
import { toGraphJson } from "@retroplug/core/graphDocument";

test("the headless host's N8 provider installs the cart link, so a plan's rp.n8.nes is the real link", async () => {
  const be = createRealBackend();
  const audio = createAudioDriver();
  const document = toGraphJson({
    nodes: [
      { id: "out", type: "rp.host.audio-out", settings: { buses: [1, 1] } },
      { id: "cart", type: "rp.n8.nes" },
    ],
    wires: [],
  });
  const placed = be.setGraph({ document, slots: [] });
  expect(placed.ok, JSON.stringify(placed.errors)).toBe(true);
  audio.renderBlocks(2, [{ frames: 64 }]); // the node activates here and acquires the link

  // The link's answer, not the stub's ("not connected"): a battery job that names no ROM and follows no boot.
  let refusal = "";
  await be.capability("cart", "sram.dump").catch((e: Error) => (refusal = e.message));
  expect(refusal).toBe(
    "a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first",
  );

  // The same door for the ROM capability, which also answers before it looks at the cart: an image that is not iNES.
  let uploadRefusal = "";
  await be.capability("cart", "rom.upload", { bytes: Uint8Array.from([0, 1, 2, 3]) })
    .catch((e: Error) => (uploadRefusal = e.message));
  expect(uploadRefusal).toBe("the upload is not an iNES image");
});

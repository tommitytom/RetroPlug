// Replacement for examples/scripts/lsdj_sync_smoke.json.
//
// The lightweight two-instance plumbing smoke: boot two LSDj instances on the
// same link_group, press START on instance 0, confirm both are alive and the mix
// produces audio. The JSON proved the multi-instance + LinkGroup plumbing with
// screenshots; we author a minimal SYNC=LSDJ song so there is something to play,
// then assert both framebuffers publish and the mixed output is audible. (Deeper
// sync semantics live in sync_pattern / sync_negative.)
import { test, expect, emu, Button } from "harness";

const LSDJ = "../resources/roms/lsdj/lsdj9_4_2.gb";
function songSav(): ArrayBuffer {
  // A minimal SYNC=LSDJ song so there is something to play. The codec pads every
  // fixed array to full length, so we author just the cells we set.
  return emu.savFromJson(JSON.stringify({
    workingSong: {
      formatVersion: 22,
      settings: { syncMode: "Lsdj" },
      rows:    [{ chains: [0] }],
      chains:  [{ phrases: [0] }],
      phrases: [{ notes: [1], instruments: [0] }],
      instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
    },
  }));
}

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("two linked LSDj instances boot and produce audio on START", () => {
  const a = emu.loadRom(LSDJ, songSav(), undefined, 1);
  const b = emu.loadRom(LSDJ, songSav(), undefined, 1);
  emu.runMs(6000);

  expect(emu.getFrame(a).published).toBeTruthy();
  expect(emu.getFrame(b).published).toBeTruthy();

  emu.tap(a, Button.Start, 100);
  expect(rms(emu.getAudio(2000))).toBeGreaterThan(0.001); // mixed output is audible
});

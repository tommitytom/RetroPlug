// The TS `risa-sync` role, running IN the real DSP kernel, is the SOLE clock for a real risa 2.3.0 core.
// The counterpart of dsp-lsdj-midisync for the NES tracker, and the end-to-end proof that the 2.3.0
// protocol lands: nothing here fakes the transport shape, so the ROM's own dormant receive path has to
// accept the 5-byte arm, the start, and the 24-PPQN clock stream exactly as risa's doc specifies.
//
// risa's receive path is a pure slave: with no bytes it never advances, so the negative case (transport
// running, role absent) is SILENT while the positive case sings. A 4-byte arm would land here as silence
// too - 2.3.0 rejects it - which is what makes this a real protocol check rather than a smoke test.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { isRisaSyncRom, risaMarkerVersion } from "../src/risa";

declare const __DSP_KERNEL_BUNDLE__: string;

const ROM_230 = "/workspaces/resources/roms/risa/risa-v2.3.0/risa-2.3.0-pal.nes";
const LETGO = "/workspaces/resources/roms/risa/let_go.srm";

// This system's pipeline: the sync role, or nothing at all (the negative control).
const withSync = (id: number) => ({ systems: [{ id, pipeline: [{ kind: "risa-sync", config: {} }] }] });
const noRoles = (id: number) => ({ systems: [{ id, pipeline: [] }] });

const rms = (a: Float32Array): number => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i] * a[i];
  return a.length ? Math.sqrt(s / a.length) : 0;
};

test("the risa-sync role in the DSP kernel is the sole clock that makes risa 2.3.0 play", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230) || !be.fileExists(LETGO)) {
    console.log(`# SKIP dsp-risa-sync: missing ${ROM_230} or ${LETGO}`);
    return;
  }

  // The released ROM really does advertise the receive path, so the provider would attach the role.
  const rom = be.readFile(ROM_230)!;
  expect(risaMarkerVersion(rom.subarray(0, 0x150))).toBe("2.3.0");
  expect(isRisaSyncRom(rom.subarray(0, 0x150))).toBe(true);

  const dsp = createDspRuntime();
  const audio = createAudioDriver();

  const id = 1; // TS owns the id counter; this direct-backend test picks its own (fresh host per file)
  expect(be.constructSystem({
    romPath: ROM_230,
    platform: "nes",
    core: "mesen",
    embeddedRom: "",
    savPath: LETGO,
    statePath: null,
  }, id)).toBeTruthy();

  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  audio.renderAudio(2500); // boot (transport off) — risa materializes the song into working RAM

  audio.setBpm(120);
  audio.setTransport(true);

  // Negative: no role -> no arm, no start, no clocks -> the receive path stays dormant and risa is silent,
  // even though the DAW transport is running.
  expect(dsp.setSystems(noRoles(id))).toBeTruthy();
  const neg = rms(audio.renderAudio(3000));

  // Positive: risa-sync arms (F9 52 ss cc tt), starts (FA), then clocks at 24 PPQN -> risa plays.
  expect(dsp.setSystems(withSync(id))).toBeTruthy();
  const pos = rms(audio.renderAudio(3000));

  console.log(`[dsp-risa-sync] neg=${neg.toFixed(5)} pos=${pos.toFixed(5)}`);
  expect(neg < 0.001).toBeTruthy(); // transport running but unclocked -> silent
  expect(pos > 0.001).toBeTruthy(); // the kernel's byte stream advances risa -> audible
  expect(pos > neg).toBeTruthy();
});

test("stopping the transport stops risa, and restarting it plays again", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM_230) || !be.fileExists(LETGO)) {
    console.log(`# SKIP dsp-risa-sync: missing ${ROM_230} or ${LETGO}`);
    return;
  }

  const dsp = createDspRuntime();
  const audio = createAudioDriver();
  const id = 1;
  expect(be.constructSystem({
    romPath: ROM_230, platform: "nes", core: "mesen", embeddedRom: "", savPath: LETGO, statePath: null,
  }, id)).toBeTruthy();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems(withSync(id))).toBeTruthy();
  audio.renderAudio(2500);

  audio.setBpm(120);
  audio.setTransport(true);
  const play = rms(audio.renderAudio(3000));

  // FC gates the clocks and stops playback; the tail of the last note decays, so measure past it.
  audio.setTransport(false);
  audio.renderAudio(1500);
  const stopped = rms(audio.renderAudio(2000));

  // A fresh arm + start after the stop, which is the path a DAW re-trigger takes.
  audio.setTransport(true);
  const again = rms(audio.renderAudio(3000));

  console.log(`[dsp-risa-sync] play=${play.toFixed(5)} stopped=${stopped.toFixed(5)} again=${again.toFixed(5)}`);
  expect(play > 0.001).toBeTruthy();
  expect(stopped < 0.001).toBeTruthy(); // FC really stopped it, rather than just muting the host
  expect(again > 0.001).toBeTruthy(); // and the re-arm brought it back
});

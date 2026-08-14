// A stale project - one saved before a role shipped - loads through the REAL ProjectStore and both
// gains the role and actually works.
//
// The pure counterpart (test/systems/store-role-union) proves the store logic over a mock backend.
// This proves the thing the user actually met: `/workspaces/resources/roms/smsggdj/smsggdj_v0_45.rplg`
// carried `roles: [mesen]` and no `sms-sync` because it predated that role, so the cart loaded, booted,
// armed, showed WAIT and ignored the DAW transport forever. Reconstructed here against the in-repo ROM
// rather than that file, which has since been repaired by hand.
//
// The second half is what makes this more than a duplicate: it does not stop at "the role is attached",
// it runs the DSP kernel and the transport and asserts the sequencer MOVES. A union that produced a
// correctly-named role with a wrong or empty config would pass the pure test and fail here.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createDspRuntime } from "../src/dspRuntime";
import { createAudioDriver } from "../src/audioDriver";
import { ProjectStore } from "../src/projectStore";
import { RecentStore } from "../src/recentStore";
import { buildAppRegistry } from "../src/appHost";
import { buildSmsMetronomeSav, pokeMetronomeIntoWram, SMS_SYNC_IN24 } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;
declare const __DSP_KERNEL_BUNDLE__: string;

const ROM = __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms";
const PAUSE = 7;
const PHRASE_STEP = 0x1b02;
const PHRASE_STEPS = 16;

/** A project stamped current but written before `sms-sync` existed: the mesen core role and nothing
 *  else, which is byte-for-byte the shape of the `.rplg` that prompted this. */
function staleProjectJson(savPath: string): string {
  return JSON.stringify({
    schemaVersion: "3",
    settings: { layout: "auto", midiRouting: "sendToAll", audioRouting: "stereo", zoom: 0 },
    systems: [
      {
        platform: "sms",
        core: "mesen",
        romPath: ROM,
        savPath,
        roles: [{ kind: "mesen", config: { region: "auto", removeSpriteLimit: false, enableFm: false, apuLatencyMs: 1.4, channelExportMode: "mix" } }],
      },
    ],
  });
}

test("a project saved before sms-sync existed loads WITH it, and the DAW can clock it", () => {
  const be = createRealBackend();
  if (!be.fileExists(ROM)) {
    console.log(`# SKIP project-role-union: missing ${ROM}`);
    return;
  }

  // The battery the project points at: the metronome song, ROM configured for IN24.
  const savPath = __CONFIG_DIR__ + "/stale.sav";
  expect(be.writeFile(savPath, buildSmsMetronomeSav(SMS_SYNC_IN24))).toBeTruthy();
  const projPath = __CONFIG_DIR__ + "/stale.rplg";
  expect(be.writeFile(projPath, new TextEncoder().encode(staleProjectJson(savPath)))).toBeTruthy();

  const project = new ProjectStore(be, new RecentStore(be), buildAppRegistry());
  const outcome = project.load(projPath);
  expect(outcome.kind).toBe("loaded");

  const view = project.systems.view();
  expect(view.length).toBe(1);
  const kinds = view[0].roles.map((r) => r.kind);
  console.log(`[project-role-union] roles after load: ${kinds.join(", ")}`);
  expect(kinds.includes("mesen")).toBeTruthy(); // the stored one survived...
  expect(kinds.includes("sms-sync")).toBeTruthy(); // ...and the missing one was attached
  // Tagged with the wire format, not left to a default that happens to be right.
  expect(view[0].roles.find((r) => r.kind === "sms-sync")!.config.machine).toBe("sms");
  // The stored core config is untouched - the union must not have re-derived over it.
  expect(view[0].roles.find((r) => r.kind === "mesen")!.config.enableFm).toBe(false);

  // --- and it actually syncs ---
  const id = view[0].id;
  const audio = createAudioDriver();
  audio.renderAudio(3000); // boot: splash, config_load (takes IN24), song_new
  expect(pokeMetronomeIntoWram(be, id) > 0).toBeTruthy();

  const dsp = createDspRuntime();
  expect(dsp.loadKernel(dsp.compileScript(__DSP_KERNEL_BUNDLE__)!)).toBeTruthy();
  expect(dsp.setSystems({ systems: [{ id, pipeline: view[0].roles }] })).toBeTruthy();

  audio.pressButton(id, PAUSE, true);
  audio.renderAudio(100);
  audio.pressButton(id, PAUSE, false);
  audio.renderAudio(200);

  audio.setBpm(120);
  audio.setTransport(true);
  const before = be.readRam(id)![PHRASE_STEP];
  audio.renderAudio(3000);
  const after = be.readRam(id)![PHRASE_STEP];
  const advanced = (after - before + PHRASE_STEPS) % PHRASE_STEPS;
  console.log(`[project-role-union] rows advanced under transport: ${advanced}`);
  // 3 s at 120 bpm is 24 rows, which is 8 mod 16. Anything nonzero means the transport reached the
  // cart; before this fix it was exactly 0, forever.
  expect(advanced > 0).toBeTruthy();
});

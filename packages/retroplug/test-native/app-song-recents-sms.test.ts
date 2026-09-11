// Song rows in the Recent list for an smsggdj project, proven against a REAL Mesen core on the shipped
// v0.45 ROMs - the twin of app-song-recents (LSDj), for the console where the timing is the whole story.
//
// smsggdj's working song is work RAM, and the cart boots for a few seconds before that RAM is its own.
// Everything the mock-tier tests model with a "booted" flag is the real latch here: the project row is
// OWED at save time (nothing recorded), paid by the watcher's tick once the cart is up, and a Recent row's
// song request waits on the same latch and then lands - the flow that used to write into the boot and
// lose the song. The stores are the real ones; only the frame tick is driven by hand.
import { test, expect } from "../testing/harness";
import { createRealBackend } from "../src/realBackend";
import { createAudioDriver } from "../src/audioDriver";
import { buildAppRegistry } from "../src/appHost";
import { RecentStore } from "../src/recentStore";
import { ProjectStore, type SongSettle } from "../src/projectStore";
import type { Platform } from "../src/platform";
import { buildSav, SMDJ4_BLOCK_LEN } from "../src/smsggdj/codec/sav";
import { resolveSmsggdjLayout } from "../src/smsggdj/runtime/layout";
import { smsggdjSongCatalog } from "../src/tracker/smsggdjSongCatalog";
import { buildMetronomeBlock, buildConfigBlock, SMS_SYNC_OFF, P_PHRASES, STEPS_PER_PHRASE, SMS_ROWS_PER_BEAT } from "./smsSyncSong";

declare const __REPO_RESOURCES_DIR__: string;
declare const __CONFIG_DIR__: string;

const ROMS: { path: string; platform: Platform }[] = [
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.sms", platform: "sms" },
  { path: __REPO_RESOURCES_DIR__ + "/roms/smsggdj_v0_45.gg", platform: "gg" },
];
const layout = resolveSmsggdjLayout("0.45")!;
const TICK_MS = 50;
const SETTLE_LIMIT_MS = 8000;

/** The metronome with its note transposed - two songs a test can tell apart in RAM. */
function metronomeAtNote(note: number): Uint8Array {
  const b = buildMetronomeBlock();
  for (let step = 0; step < STEPS_PER_PHRASE; step += SMS_ROWS_PER_BEAT) b[P_PHRASES + step * 4] = note;
  return b;
}

for (const rom of ROMS) {
  const tag = rom.platform;

  test(`[${tag}] the project row waits for the boot, a Recent row's song lands after it, and rows stay one per song`, () => {
    const be = createRealBackend();
    if (!be.fileExists(rom.path)) {
      console.log(`# SKIP app-song-recents-sms: missing ${rom.path}`);
      return;
    }
    // A private copy of the cart with its battery beside it, so the project is self-contained and the
    // core boots from the sibling `.sav` exactly as a user's project does.
    const romCopy = `${__CONFIG_DIR__}/sms-recents.${tag}`;
    const savPath = `${__CONFIG_DIR__}/sms-recents.sav`;
    const rplg = `${__CONFIG_DIR__}/sms-recents.rplg`;
    expect(be.writeFile(romCopy, be.readFile(rom.path)!)).toBeTruthy();
    const low = metronomeAtNote(0x0d);
    const high = metronomeAtNote(0x19);
    const sav = buildSav([{ block: low, name: "ALPHA" }, { block: high, name: "BETA" }], 32 * 1024, buildConfigBlock(SMS_SYNC_OFF))!;
    expect(be.writeFile(savPath, sav)).toBeTruthy();

    const audio = createAudioDriver();
    const recent = new RecentStore(be);
    const project = new ProjectStore(be, recent, buildAppRegistry());
    // FM off so the render is deterministic; the sms-sync role is what carries the song catalog.
    const id = project.systems.adopt(
      { romPath: romCopy, roles: [{ kind: "mesen", config: { enableFm: false } }, { kind: "sms-sync", config: { machine: tag } }] },
    );
    expect(typeof id).toBe("number");
    const songs = () => recent.view().map((v) => v.song);
    const ramOf = () => be.readRam(project.systems.primary()!.id)!;
    const working = () => ramOf().subarray(layout.song, layout.song + SMDJ4_BLOCK_LEN);
    const holds = (block: Uint8Array) => working().subarray(P_PHRASES, P_PHRASES + 64).every((b, i) => b === block[P_PHRASES + i]);
    const settleUntilDone = (): SongSettle => {
      let r: SongSettle = "idle";
      for (let t = 0; t < SETTLE_LIMIT_MS; t += TICK_MS) {
        r = project.settleSong();
        if (r !== "waiting") return r;
        audio.renderAudio(TICK_MS);
      }
      return r;
    };

    // Saved the instant the core exists: the cart cannot say what it holds, so the row is OWED.
    expect(project.save(rplg)).toBeTruthy();
    expect(songs()).toEqual([]);
    expect(project.syncRecent()).toBeFalsy();

    // The watcher's tick during the boot: still nothing, and no garbage row either.
    for (let t = 0; t < 1500; t += 500) {
      audio.renderAudio(500);
      expect(project.syncRecent()).toBeFalsy();
    }
    expect(songs()).toEqual([]);

    // Up (v0.45 boots blank): the honest songless row, once.
    let up = false;
    for (let t = 0; t < 6000 && !up; t += TICK_MS) {
      audio.renderAudio(TICK_MS);
      up = smsggdjSongCatalog.workingSongReady!(ramOf());
    }
    expect(up).toBeTruthy();
    expect(project.syncRecent()).toBeTruthy();
    expect(songs()).toEqual([undefined]);
    expect(recent.view()[0].label).toBe(`sms-recents.sav [sms-recents]`);
    expect(project.syncRecent()).toBeFalsy();

    // A song request on the running cart settles at once and its row supersedes the songless one.
    project.requestSong({ name: "ALPHA", confirmed: false });
    expect(project.settleSong()).toBe("loaded");
    audio.renderAudio(200);
    expect(holds(low)).toBeTruthy();
    expect(smsggdjSongCatalog.workingName(be.readSram(project.systems.primary()!.id)!, ramOf())).toBe("ALPHA");
    expect(songs()).toEqual(["ALPHA"]);
    expect(project.syncRecent()).toBeFalsy(); // the watcher agrees with what the settle recorded

    // The Recent-row flow: reopen the project, ask for BETA. The rebuilt core is booting, the request
    // waits on it, and lands - and STAYS - once it is up. No songless row reappears on the way.
    project.newProject();
    expect(project.load(rplg).kind).toBe("loaded");
    expect(songs()).toEqual(["ALPHA"]);
    project.requestSong({ name: "BETA", confirmed: false });
    expect(project.settleSong()).toBe("waiting");
    expect(settleUntilDone()).toBe("loaded");
    audio.renderAudio(2500); // well past anything the boot could still do to it
    expect(holds(high)).toBeTruthy();
    expect(smsggdjSongCatalog.workingName(be.readSram(project.systems.primary()!.id)!, ramOf())).toBe("BETA");
    expect(songs()).toEqual(["BETA", "ALPHA"]);
    expect(project.syncRecent()).toBeFalsy(); // the owed row resolves to (path, BETA): already there
    expect(recent.view().every((v) => v.path === be.canonicalize(rplg))).toBeTruthy(); // one project, two rows
    expect(recent.view().every((v) => /^[\x20-\x7e]*$/.test(v.song ?? ""))).toBeTruthy(); // and nothing a font cannot draw

    // Back to ALPHA by its row: moved up, not duplicated; the cart follows.
    project.requestSong({ name: "ALPHA", confirmed: false });
    expect(project.settleSong()).toBe("loaded");
    audio.renderAudio(200);
    expect(holds(low)).toBeTruthy();
    expect(songs()).toEqual(["ALPHA", "BETA"]);
  });
}

// Render two link-synced LSDj instances to per-system WAVs for the reaper-MCP audio-quality workflow
// (the counterpart of legacy `reaper:analyze-lsdj-sync`). Two LSDj cores in one link group,
// both authored SYNC=LSDJ, START on the leader; renderAudioPerSystem isolates each core's audio so the
// follower's WAV shows it actually synced. Staged by `pnpm reaper:analyze-lsdj-sync`.
//
//   retroplug-cli build/cli/analyze-lsdj-sync.js <lsdjRom> [outPrefix]
import { runSession, hostArgs } from "../session";
import { encodeWav } from "../wav";
import { savFrom, type SongSettings } from "../../src/lsdjSav";

const START = 7; // GameboyButton::Start

// row 0 → chain 00 → phrase 00, one C note on a hard-panned pulse; SYNC=LSDJ on both.
const songSav = (sync: SongSettings["syncMode"]) => savFrom({
  workingSong: {
    settings: { syncMode: sync },
    rows: [{ chains: [0] }],
    chains: [{ phrases: [0] }],
    phrases: [{ notes: [1], instruments: [0] }],
    instruments: [{ type: "pulse", panning: "LeftRight", adsr: { initialLevel: 8, attackSpeed: 8 }, vibrato: { direction: "Up" }, sweep: 127 }],
  },
});

runSession((s) => {
  const [romPath, outPrefix] = hostArgs();
  if (!romPath) throw new Error("usage: analyze-lsdj-sync.js <lsdjRom> [outPrefix]");
  const prefix = outPrefix || "/tmp/lsdj-sync-pattern";

  // Link-cable sync is pure GB serial ferrying in the block runner — construct both cores directly,
  // link them, and drive; no DSP kernel needed.
  const construct = (id: number, sync: SongSettings["syncMode"]) => s.backend.constructSystem({
    romPath, platform: "gb", core: "sameboy", embeddedRom: "",
    savPath: null, statePath: null, sramBytes: songSav(sync),
  }, id);
  if (!construct(1, "Lsdj") || !construct(2, "Lsdj")) throw new Error("construct failed");
  s.backend.applyRoleConfig(1, "sameboy", { linkGroupId: 1 });
  s.backend.applyRoleConfig(2, "sameboy", { linkGroupId: 1 });

  s.audio.renderAudio(6000); // reach the song screen from the savs
  s.audio.pressButton(1, START, true); // START on the leader only
  s.audio.renderAudio(120);
  s.audio.pressButton(1, START, false);

  const bufs = s.audio.renderAudioPerSystem(4000); // slot order = [leader, follower]
  bufs.forEach((pcm, i) => {
    const out = `${prefix}_sys${i}.wav`;
    if (!s.backend.writeFile(out, encodeWav(pcm))) throw new Error(`write failed: ${out}`);
    console.log(`cli: LSDj sync system ${i} → ${out}`);
  });
});

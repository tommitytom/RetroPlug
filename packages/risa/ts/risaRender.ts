// What rendering a risa song needs of the cart (docs/modular-graph-phase-f4a-design.md section 4.1): its saved songs,
// how it spells them, the battery a chosen one is seeded from, the button gesture that starts playback, and the
// sequencer flag that says the song is still playing. The shell's render library keeps its pipeline and asks the
// integration for these.
//
// risa begins song playback on SELECT+START (a plain START first nudges it off an empty phrase context, the sequence
// the runtime-reader test proved reliable). The two prep renders are pre-song and discarded; the SELECT+START render,
// where the song begins, is captured through the sink and returned as the recording's lead-in, so the opening frames
// are not lost.
// The end of the song is SEQ_MODE_STOPPED, read through the pure runtime reader from a work-RAM snapshot.
import type { SongInfo } from "@retroplug/tracker/songCatalog";
import { PLAY_HOLD_MS, type TrackerRender } from "@retroplug/tracker/trackerRender";
import { listSongs as risaListSongs } from "./risaSav";
import { loadSongToWorkingInSav } from "./risaSongOps";
import { decodeRisaState, identifyRisaVersion, resolveRisaLayout, supportedRisaVersions } from "./runtime";

const NES_START = 7; // NesButton::Start
const NES_SELECT = 6; // NesButton::Select

/** The catalog's saved slots as one line, as a selection error prints them. A catalog read from a battery that holds no
 *  song entries is still a catalog. */
const risaListing = (songs: readonly SongInfo[]): string =>
  songs.length ? songs.map((s) => `${s.index}: ${s.name || "(unnamed)"}`).join(", ") : "(no saved songs)";

export const RISA_RENDER: TrackerRender = {
  listSongs: (sav) => risaListSongs(sav),
  songList: { listing: risaListing, empty: "(no saved songs in the catalog)" },

  // Promote the catalog song into the working banks (0-3), keeping the catalog (banks 4-7) intact. A current-layout
  // catalog is required: the firmware migrates legacy images on boot, so a live-read battery is always current. Null
  // when the slot names no song or the image is not one this build can promote.
  seedSong: (sav, index) => loadSongToWorkingInSav(sav, index),
  // The list offers a legacy-layout catalog's slots too, and those are exactly the ones this build cannot promote: the
  // render says so and refuses, rather than booting the battery's own working song and naming the file after another.
  seedRefusal: (index) => `render: could not load risa song ${index} (needs a current-layout catalog)`,

  pressPlay: (host, id, _rom, sink) => {
    host.pressButton(id, NES_START, true);
    sink.renderChunk(PLAY_HOLD_MS); // prep: nudge off an empty phrase context (pre-song, discarded)
    host.pressButton(id, NES_START, false);
    sink.renderChunk(PLAY_HOLD_MS); // prep (discarded)
    host.pressButton(id, NES_SELECT, true);
    host.pressButton(id, NES_START, true);
    const head = sink.renderChunk(PLAY_HOLD_MS); // the song starts here: capture it as the lead-in
    host.pressButton(id, NES_START, false);
    host.pressButton(id, NES_SELECT, false);
    return [head];
  },

  playingProbe: (host, id, rom, facts, warn) => {
    const layout = resolveRisaLayout(identifyRisaVersion(rom));
    if (!layout) {
      // A risa cart whose version has no internal-RAM layout: the reader cannot find seq_mode, so the render falls
      // back to a fixed length, and says why - or it just looks like the song never stops (it cannot be seen to stop).
      // Only these versions expose the addresses song-length auto-detect needs.
      warn(
        `render: risa ${identifyRisaVersion(rom) ?? "(unrecognized version)"} has no runtime layout \u2014 song-length ` +
          `auto-detect unavailable (supported: ${supportedRisaVersions().join(", ")}); ` +
          `rendering a fixed ${facts.durationMs ?? facts.maxDurationMs}ms`,
      );
      return null;
    }
    // A work-RAM read is the per-block snapshot (safe while the core plays). If it is momentarily unavailable, report
    // "playing" so a read gap never trims the song early; the render's maxDurationMs cap still bounds it.
    return () => {
      const ram = host.readRam(id);
      return ram ? decodeRisaState(ram, layout).playing : true;
    };
  },
};

// What rendering an LSDj song needs of the cart (docs/modular-graph-phase-f4a-design.md section 4.1): its saved songs,
// how it spells them, the SRAM a chosen one is seeded from, and the register that says the song is still playing. The
// shell's render library (packages/retroplug/src/render/render.ts) keeps its pipeline and asks the integration for these.
//
// The end-of-song probe is the hardware's, not the codec's: LSDj's HFF command stops the song by powering the APU off, a
// 0 write to NR52 ($FF26), the sound-master-enable register (bit 7). Polling it each render chunk and stopping at the
// high-to-low edge is the technique lsdpack uses, and it is version-independent, so it holds for any LSDj build. It is
// chosen for a ROM whose roles name this cart (the integration's markerRole), not for a battery that decodes: a cart
// booted from a blank battery answers it too.
import type { SongInfo } from "@retroplug/tracker/songCatalog";
import type { TrackerRender } from "@retroplug/tracker/trackerRender";
import { decodeSav, encodeSav, kSavSize } from "./codec/sav";

const NR52_ADDR = 0xff26;
const NR52_ON = 0x80; // bit 7 = all-sound-on

/** The populated project slots as one line ("0: HAPPYBD, 3: DEMO"), as a selection error prints them: LSDj's projects
 *  are NAMED slots, and a save holding none still has a working song worth saying so about. */
const lsdjListing = (songs: readonly SongInfo[]): string =>
  songs.length ? songs.map((s) => `${s.index}: ${s.name || "(unnamed)"}`).join(", ") : "(no named projects)";

/** The populated project slots, DECODED: a render has always read a battery through the codec, so a malformed 128 KiB
 *  image is refused in the codec's own words (where the header-only catalogue scan would answer "no songs") and an
 *  early 32 KiB image, which has no archive, lists none. */
function lsdjRenderSongs(sav: Uint8Array): SongInfo[] {
  const out: SongInfo[] = [];
  decodeSav(sav).projects.forEach((project, index) => {
    if (project) out.push({ index, name: project.name });
  });
  return out;
}

export const LSDJ_RENDER: TrackerRender = {
  listSongs: lsdjRenderSongs,
  songNameLength: 8, // LSDj stores a project name in 8 characters, and uppercases it
  songList: { listing: lsdjListing, empty: "(no named projects \u2014 only the working song)" },

  // LSDj plays its WORKING song at boot, whatever slot you would rather hear, so a chosen project is promoted into it
  // in the SRAM the fresh system boots from. The unmodelled regions come from the original battery when it is a full
  // image (a narrower one is authored fresh), which is what the codec's optional tail is for.
  seedSong: (sav, index) => {
    const decoded = decodeSav(sav);
    const project = decoded.projects[index];
    if (!project) return null;
    decoded.workingSong = project.song; // working song and project songs share the decoded Song shape
    decoded.activeProjectIndex = index;
    return encodeSav(decoded, sav.length >= kSavSize ? sav : undefined);
  },

  playingProbe: (host, id) => () => ((host.emulator.readCpu(id, NR52_ADDR) ?? 0) & NR52_ON) !== 0,
};

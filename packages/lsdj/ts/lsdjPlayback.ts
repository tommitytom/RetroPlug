// LSDj's playback contribution to its tracker integration (docs/modular-graph-phase-f4a-design.md section 4.2): the SYNC
// setting it is running, the change signature over its working song, the row-timing table the controller's predictor
// runs on, and the WRAM reader that says where its playhead is. The shell's project store polls these through the
// integration its roles name; the shapes are @retroplug/tracker's, so nothing here is a type the shell has to know.
//
// The reader is built from the cart's ROM header once per system (the version decides the memory layout) and read each
// frame. Reading is pure: no backend, no emulator.
import type { TrackerPlayback } from "@retroplug/tracker/trackerPlayback";
import { songRowTicksFromSav, savSyncMode, workingSongSignature } from "./playback/fromSav";
import { anyChannelPlaying, anchorRowsFromState } from "./playback/anchor";
import { LsdjReader } from "./runtime";
import type { LsdjState } from "./runtime/types";

export const LSDJ_PLAYBACK: TrackerPlayback = {
  syncMode: (sram) => savSyncMode(sram),
  songSignature: (sram) => workingSongSignature(sram),
  rowTicks: (sram) => songRowTicksFromSav(sram),
  anchor: {
    // A version with no bundled layout has no memory map to read, so there is no reader rather than one answering
    // nonsense: the same answer the controller poll's own gate gave when it keyed on the lsdj-sync role.
    reader: (header) => {
      const reader = LsdjReader.fromHeader(header);
      return reader.supported ? reader : null;
    },
    playing: (state) => anyChannelPlaying(state as LsdjState),
    rows: (state) => anchorRowsFromState(state as LsdjState),
  },
};

// Mesen's views module: the NES SD card's file list, a side view (a grid cell of its own beside the game). The System
// submenu's SD Card Files row opens it (ts/ui/index.ts), through the open list both modules share (sdCard.ts).
import { defineViews, type SideView } from "@retroplug/ui/contribution";
import { reactView } from "@retroplug/ui-react";

import { SdCardFiles } from "./SdCardFiles";
import { sdFilesOpen } from "./sdCard";

export const sdFilesView = {
  id: "nes-sd-files",
  label: "SD Card Files",
  placement: "side",
  when: (sys) => sys.platform === "nes",
  openFor: sdFilesOpen,
  body: reactView(SdCardFiles),
} satisfies SideView;

export default defineViews({ views: [sdFilesView] });

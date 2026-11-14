// The SD Card Files panel: what is on the NES's emulated SD card, drawn in a grid cell beside the game (a side view,
// views.ts). A folder's entries sit indented under it, one level deep, which is where a ROM keeps its data. It reads
// the folder again every half second, so a clip copied onto the card shows without reopening anything.
import { useCallback, useMemo, useRef, type ReactNode } from "react";

import type { UiHost } from "@retroplug/ui/contribution";
import { shortenMiddle } from "@retroplug/core/path";
import { Box, TextEx, useFrameSampled, type ViewComponentProps } from "@retroplug/ui-react";

import { sdCardEntries, sdRootOf } from "./sdCard";

/** Frames between two reads of the folder: half a second at 60 fps. A listing is a handful of readdirs, but a frame
 *  tick is no place for one. */
const REFRESH_FRAMES = 30;

/** What the panel draws: its title and its lines (already indented and marked). */
interface Listing {
  readonly title: string;
  readonly lines: readonly string[];
}

function listingKey(listing: Listing): string {
  return `${listing.title}\n${listing.lines.join("\n")}`;
}

function basename(path: string): string {
  const trimmed = path.replace(/[\\/]+$/, "");
  return trimmed.slice(Math.max(trimmed.lastIndexOf("/"), trimmed.lastIndexOf("\\")) + 1) || trimmed;
}

function readListing(host: UiHost, systemId: number): Listing | undefined {
  const sys = host.systems.view().find((s) => s.id === systemId);
  if (!sys) return undefined; // a system mid-rebuild: keep what shows
  const root = sdRootOf(sys);
  if (!root) return { title: "SD Card: Scratch", lines: ["No folder picked.", "", "System > SD Card", "picks one."] };
  const entries = sdCardEntries(host.files, root);
  const lines = entries.map((e) => `${"  ".repeat(e.depth)}${e.name}${e.dir ? "/" : ""}`);
  return { title: `SD Card: ${basename(root)}`, lines: lines.length > 0 ? lines : ["(empty)"] };
}

export function SdCardFiles({ host, systemId, width, height }: ViewComponentProps): ReactNode {
  // A new sampler identity is a new source, which useFrameSampled clears the panel for, so it changes with the system
  // alone: the shell hands a new host whenever the menu opens or closes, and that is read through a ref instead.
  const hostRef = useRef(host);
  hostRef.current = host;
  const sample = useMemo(() => {
    let frame = 0;
    return (): Listing | undefined =>
      frame++ % REFRESH_FRAMES === 0 ? readListing(hostRef.current, systemId) : undefined;
  }, [systemId]);
  const listing = useFrameSampled(sample, useCallback(listingKey, []));
  if (!listing) return null;

  // Sized to the tile: a 432-pixel tile (zoom 3) draws 16-pixel text. The face is drawn monospaced, so a character is
  // close to 0.6 of the size wide, which is what a long name is elided to.
  const font = Math.max(10, Math.round(height / 27));
  const lineH = Math.round(font * 1.25);
  const pad = Math.round(font / 2);
  const cols = Math.max(8, Math.floor((width - 2 * pad) / (font * 0.6)));
  const rows = Math.max(1, Math.floor((height - 2 * pad) / lineH) - 1); // less the title's line
  const fit = (line: string): string => shortenMiddle(line, cols);
  let body = listing.lines.map(fit);
  if (body.length > rows) body = [...body.slice(0, rows - 1), `... ${body.length - rows + 1} more`];

  return (
    <Box
      style={{
        width,
        height,
        "background-color": "#101418",
        display: "flex",
        "flex-direction": "column",
        "padding-left": pad,
        "padding-right": pad,
        "padding-top": pad,
        "padding-bottom": pad,
      }}
    >
      <TextEx style={{ "text-color": "#4a86e8", "font-size": font, width: width - 2 * pad, height: lineH }}>
        {fit(listing.title)}
      </TextEx>
      <TextEx style={{ "text-color": "#d0d0d0", "font-size": font, width: width - 2 * pad, height: height - 2 * pad - lineH }}>
        {body.join("\n")}
      </TextEx>
    </Box>
  );
}

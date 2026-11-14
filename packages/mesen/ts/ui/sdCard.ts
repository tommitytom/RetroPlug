// The NES's emulated SD card as the UI shows it: the folder the card maps to, the systems whose file list is open
// beside them, and the listing that panel draws. Plain TypeScript, so the menu module (ts/ui/index.ts) and the views
// module (ts/ui/views.ts) share one state without the menu reaching React, and the listing is tested without a UI.
import type { FilesHost, SystemInfo, SystemsHost } from "@retroplug/core/host";
import { findRole } from "@retroplug/core/systemRoles";
import { createSignal } from "@retroplug/ui/menu";

/** The folder the emulated N8's SD card maps to, as the mesen role holds it ("" = the per-process scratch card). */
export function sdRootOf(sys: SystemInfo): string {
  const root = findRole(sys.roles, "mesen")?.config.sdRoot;
  return typeof root === "string" ? root : "";
}

/** The systems the SD Card Files panel is open beside, by the id each had when it was opened: a pick of a new card
 *  rebuilds the system under a new id, and the shell follows the old one to it (`currentId`). Session state, as a
 *  debug overlay's flag is: a project does not reopen it. */
export const sdFilesOpen = createSignal<readonly number[]>([]);

/** The open list with every id followed to the system it names now, and the gone ones dropped. */
function liveOpen(systems: Pick<SystemsHost, "currentId">): number[] {
  const live: number[] = [];
  for (const id of sdFilesOpen.get()) {
    const now = systems.currentId(id);
    if (now !== null && !live.includes(now)) live.push(now);
  }
  return live;
}

export function sdFilesShown(systems: Pick<SystemsHost, "currentId">, id: number): boolean {
  return liveOpen(systems).includes(id);
}

export function setSdFilesShown(systems: Pick<SystemsHost, "currentId">, id: number, shown: boolean): void {
  const others = liveOpen(systems).filter((open) => open !== id);
  sdFilesOpen.set(shown ? [...others, id] : others);
}

/** One line of the listing: a name, how deep under the card's root it is, and whether it is a folder. */
export interface SdEntry {
  readonly name: string;
  readonly depth: number;
  readonly dir: boolean;
}

/** How deep the listing goes: the root and one level of folders, which is where a ROM keeps its data (nesvj's clips are
 *  /nesvj/*.raw). */
const MAX_DEPTH = 2;
/** The most entries one listing reads, so a card holding a photo library costs a bounded amount per refresh. */
const MAX_ENTRIES = 256;

/** The card's contents in the order the panel draws them: folders first, then files, each by name ignoring case, a
 *  folder's own entries right under it. Hidden names (a leading dot: .DS_Store, .Trashes) are left out. Empty for a
 *  folder that cannot be read. */
export function sdCardEntries(files: Pick<FilesHost, "listDir">, root: string): SdEntry[] {
  const out: SdEntry[] = [];
  const walk = (dir: string, depth: number): void => {
    const names = files
      .listDir(dir)
      .filter((name) => !name.startsWith("."))
      .map((name) => ({ name: name.endsWith("/") ? name.slice(0, -1) : name, dir: name.endsWith("/") }))
      .sort((a, b) => (a.dir !== b.dir ? (a.dir ? -1 : 1) : compareNames(a.name, b.name)));
    for (const entry of names) {
      if (out.length >= MAX_ENTRIES) return;
      out.push({ name: entry.name, depth, dir: entry.dir });
      if (entry.dir && depth + 1 < MAX_DEPTH) walk(`${dir}/${entry.name}`, depth + 1);
    }
  };
  // The root without its trailing separator, so a folder under it is one join away (a bare "/" stays itself).
  walk(root.replace(/[\\/]+$/, "") || root, 0);
  return out;
}

function compareNames(a: string, b: string): number {
  const la = a.toLowerCase();
  const lb = b.toLowerCase();
  return la < lb ? -1 : la > lb ? 1 : a < b ? -1 : a > b ? 1 : 0;
}

// useProjectModals — the app-level overlays around New Project / Load Project.
//
// Two gaps this closes: (1) New/Load used to discard unsaved work with no prompt; (2) project.load()'s
// LoadOutcome (incompatible / missing / error) was voided at every call site, so a project saved by a
// newer build, or one whose ROM/sav moved, failed silently. This owns a single modal at a time and the
// guarded operations the menu drives (ctx.newProject / ctx.loadProject).
//
// Load outcomes arrive AFTER the file dialog closes (the menu is gone by then), so they surface as a
// full-window overlay - App renders its tree above everything, exactly like the close-guard prompt. The
// overlay is a plain MenuTree (data), rebuilt each render from the pending state, so its handlers never
// go stale. Mirrors useCloseGuard's Save/Discard/Cancel flow via the shared saveProjectInteractive.
//
// It also owns the settling of a SONG REQUEST (ProjectStore.requestSong): the song a Recent row asked
// for, or one the Songs menu picked. The store parks the request and applies it on the first tick the
// cart can take it, which for a console whose working song is work RAM (smsggdj) is after its boot, not
// the frame the project loaded; this hook is what ticks it, and what raises the discard prompt when the
// store reports that loading would destroy unsaved work.

import { useEffect, useRef, useState } from "react";

import type { AppStores } from "../../src/appStores";
import type { LoadOutcome, ProjectStore } from "../../src/projectStore";
import { hasUnsavedChanges } from "../../src/unsavedChanges";
import { savEditWouldDiscard, saveWorkingSong } from "../../src/tracker/liveSav";
import { flushDirtySram } from "../../src/sramAutoSave";
import { useNativeEvent } from "@retroplug/ui-react";
import type { Overlay } from "./overlay";
import { buildProjectModal, NoticeQueue, SONG_SAVE_REFUSED, type PendingModal } from "./projectModals";
import { saveProjectInteractive } from "./saveProjectInteractive";

export interface ProjectModals extends Overlay {
  /** Start a new project, guarding unsaved changes first. */
  newProject: () => void;
  /** Load `path`, guarding unsaved changes first and surfacing the outcome. `song` (a recent row's) is
   *  loaded into the cart once the project lands, so picking a song row reopens that song. */
  loadProject: (path: string, song?: string) => void;
  /** Open `romPath` as a fresh project (with an optional paired sav), guarding unsaved changes first. */
  loadRomAsProject: (romPath: string, explicitSav?: string) => void;
  /** Run `proceed` (an op that replaces what system `id` holds), first offering to save its working song
   *  when that song lives outside the battery and is unsaved - the drop-target twin of the menu's guarded
   *  rows. Runs it straight away otherwise. */
  guardSongReplace: (id: number, proceed: () => void) => void;
}

/** The project revision the missing-packs notice last showed for, per store: once per project, whether the load came
 *  through a menu here or (in the plugin) through the control plane before the editor opened. Spent only when the
 *  notice is actually displayed. */
const noticeRevision = new WeakMap<ProjectStore, number>();

export function useProjectModals(stores: AppStores): ProjectModals {
  const [pending, setPending] = useState<PendingModal | null>(null);
  const project = stores.project;
  // The song a recent row asked for, held until its load actually lands. A ref (not state) because it must
  // survive the relink round-trip - locate the missing ROM, and the song still gets loaded - without
  // re-rendering anything itself.
  const pendingSong = useRef<string>("");
  // Bumped whenever an overlay is dismissed. A save started from an overlay is async (it waits for the
  // battery write to land), and what runs after it must not outlive the overlay: Cancel pressed while it
  // was in flight used to be followed by the save landing and running the very op the user cancelled.
  const modalEpoch = useRef(0);
  // The missing-packs notice waiting for the modal in front of it (a song-discard prompt) to clear, and the one on
  // screen now (a prompt raised over it sends it back to the queue).
  const notices = useRef(new NoticeQueue());

  // Show the queued notice, spending the project's revision only now that it displays.
  const flushNotice = (): void => {
    const packs = notices.current.take();
    if (!packs) return;
    noticeRevision.set(project, project.revision());
    setPending({ kind: "missingPacks", packs });
  };

  // A project the control plane loaded before this editor mounted (a DAW session's state chunk) raises the notice here,
  // once per project.
  useEffect(() => {
    const packs = project.missingPacks();
    if (packs.length && noticeRevision.get(project) !== project.revision()) {
      notices.current.queue(packs);
      flushNotice();
    }
  }, []);

  // Give the parked song request a chance to apply. "waiting" means the cart is not up yet (or the discard
  // prompt is showing) and the frame tick will ask again; "discard" raises that prompt; anything else is
  // finished, one way or the other, and there is nothing to show for it.
  const settleSong = (): boolean => {
    const r = project.settleSong();
    if (r === "waiting" || typeof r !== "object") return false;
    // The prompt replaces whatever shows: a notice under it waits for the prompt to clear.
    notices.current.covered();
    setPending({ kind: "songDiscard", song: r.song, working: r.working });
    return true;
  };
  useNativeEvent("frame", () => {
    if (project.hasSongRequest()) settleSong();
    // A working song a reset is carrying across the reboot goes back in on the first frame its cart is up.
    if (project.systems.hasCarries()) project.systems.settleCarries();
    // Which saved slot each work-RAM working song is, kept true to loads, saves and NEWs made on the
    // cart's own screens - every frame, so an in-cart NEW cannot be edited before it is noticed.
    project.systems.observeWorkingSongs();
  });

  // A load result → the next overlay: loaded clears; incompatible/error notify; missing offers relink.
  const handleOutcome = (outcome: LoadOutcome): void => {
    // Every completed load funnels through here (including one finished by a relink), so this is the one
    // place the requested song can be handed over. A load that didn't land drops it - except "missing",
    // which is still in flight awaiting the relink.
    const song = pendingSong.current;
    if (outcome.kind !== "missing") pendingSong.current = "";
    switch (outcome.kind) {
      case "loaded": {
        setPending(null);
        notices.current.queue(outcome.missingPacks?.length ? outcome.missingPacks : null);
        // The row's song is a REQUEST, not a load: the project has only just been rebuilt, and on a
        // console whose working song is work RAM the cart is still booting - a write now is a write the
        // boot erases. The store applies it on the first tick the cart is ready (LSDj / risa: this very
        // call), and guards it THEN, when the cart's working song is real; the discard prompt, if any,
        // comes back through settleSong.
        let prompted = false;
        if (song) {
          project.requestSong({ name: song, confirmed: false });
          prompted = settleSong();
        }
        if (!prompted) flushNotice();
        break;
      }
      case "incompatible":
        setPending({ kind: "notice", title: "Incompatible Project", body: "Saved by a newer version of RetroPlug." });
        break;
      case "error":
        setPending({ kind: "notice", title: "Load Failed", body: "The project file couldn't be read." });
        break;
      case "missing":
        setPending({ kind: "relink", missing: outcome.missing });
        break;
    }
  };

  // Run `proceed` now if the project is clean, else raise the discard confirm first.
  const guard = (proceed: () => void): void => {
    if (hasUnsavedChanges(stores.backend, project)) setPending({ kind: "confirm", proceed });
    else proceed();
  };

  const newProject = (): void =>
    guard(() => {
      setPending(null);
      project.newProject();
    });

  const loadProject = (path: string, song?: string): void =>
    guard(() => {
      pendingSong.current = song ?? "";
      handleOutcome(project.load(path));
    });

  const loadRomAsProject = (romPath: string, explicitSav?: string): void =>
    guard(() => {
      setPending(null);
      project.openRom(romPath, explicitSav ? { explicitSav } : undefined);
    });

  const onClose = (): void => {
    modalEpoch.current++;
    setPending((p) => {
      if (p?.kind === "relink") {
        project.cancelLoad(); // abandoning the relink drops the held load
        pendingSong.current = ""; // …and the song it was going to open
      }
      if (p?.kind === "songDiscard") project.cancelSong(); // "Keep current song": the request is withdrawn
      return null;
    });
    notices.current.dismissed();
    flushNotice();
  };

  // "Discard & load": the store has the request; agreeing is what lets the next settle apply it.
  const proceedSong = (): void => {
    project.confirmSong();
    setPending(null);
    if (!settleSong()) flushNotice();
  };

  const guardSongReplace = (id: number, proceed: () => void): void => {
    const sys = project.systems.systems().find((x) => x.id === id);
    if (sys && savEditWouldDiscard(project.systems, sys)) setPending({ kind: "songReplace", id, proceed });
    else proceed();
  };

  // "Save song first": into the cart's battery, then that battery to its .sav, then the replacing op.
  const saveSongThen = (id: number, proceed: () => void): void => {
    const epoch = modalEpoch.current;
    void (async () => {
      const sys = project.systems.systems().find((x) => x.id === id);
      if (!sys) return setPending(null);
      setPending({ kind: "songReplace", id, proceed, saving: true });
      const r = await saveWorkingSong(stores.backend, project.systems, sys).catch(
        (): { kind: "refused"; reason: "unwritable" } => ({ kind: "refused", reason: "unwritable" }),
      );
      if (modalEpoch.current !== epoch) return; // dismissed while saving: run nothing on its behalf
      if (r.kind === "refused") return setPending({ kind: "songReplace", id, proceed, error: SONG_SAVE_REFUSED[r.reason] });
      const now = project.systems.systems().find((x) => x.id === id);
      if (now) flushDirtySram(stores.backend, [now]);
      setPending(null);
      proceed();
    })();
  };

  // The New / Load confirm's Save: the same rule - a dismissal while it saves cancels what follows it.
  // While it runs, Don't Save is inert: pressed mid-save it used to proceed (start the new project) and
  // the save then resumed and wrote the NEW, empty project over the user's .rplg.
  const saveThen = (proceed: () => void): void => {
    const epoch = modalEpoch.current;
    setPending({ kind: "confirm", proceed, saving: true });
    void saveProjectInteractive(stores)
      .catch((err) => {
        console.warn(`[project] save failed: ${String(err)}`);
        return false;
      })
      .then((saved) => {
        if (modalEpoch.current !== epoch) return;
        if (saved) proceed();
        else setPending({ kind: "confirm", proceed }); // cancelled Save-As, a refused song, a failure: choose again
      });
  };

  const tree = pending
    ? buildProjectModal(pending, stores, { handleOutcome, onClose, proceedSong, saveSongThen, saveThen })
    : null;
  return { tree, onClose, newProject, loadProject, loadRomAsProject, guardSongReplace };
}

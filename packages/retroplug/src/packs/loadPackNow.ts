// The live load (docs/downloadable-packs-m3-design.md section 7): one development pack folder loaded now, a development
// and test hook that M4's `pack install` and Settings > Packs call. Native loads the folder into the process's table
// (__rp_packLoadNow: PackTable::load, every Engine registering it at its next setGraph, a kinds half waiting for the
// next launch as M2f decided); on a load this module set's loader runs again with the builders it has loaded with,
// which loads only the new pack's halves, and the registry commit re-resolves the open project
// (ProjectStore.reresolve). The status listeners hear of a refusal, or of a load that committed nothing, too, so a
// placeholder waiting for the pack shows it refused. `{ loaded }` is the answer once TS accepted the pack as well: a
// pack native loaded that this module set then refuses (its app half throws, its manifest does not hold, it ran past
// the load budget) answers `{ refused }` with the TS reason.
//
// A pack refused in TS is tried again: its refusal is forgotten before the loader runs. Limits that stay, all of the
// host's: QuickJS-ng caches a module by its import name (`pack:<id>/<half>`) in the context's `loaded_modules`. A half
// that compiled and resolved stays cached for the context's life; a half whose body threw is cached with its
// rejection, so a later import rejects with the old error. A pack.json fix is never re-read, since the same folder
// answers the `LoadedPack` the table already holds (PackTable::loadLocked's same-folder branch). A pack refused for
// running past the load budget stays out of `__rp_packList` for the process. Only a half that failed to compile, or
// whose import the contract guard refused, picks up new code on a retry; the rest take a restart.
import { APP_BUILDERS, forgetRefused, loadedBuilders, loadPacks, packStatus, type HalfBuilder } from "./loadPacks";
import { nativePackHost, type HalfKind } from "./packHost";
import { packStatusChanged } from "./refusals";

export type LoadPackNowResult = { loaded: string } | { refused: string };

interface LiveLoadHook {
  __rp_packLoadNow?: (dir: string) => string;
}

/** Load the pack whose version folder is `dir` now. Never rejects: a refusal is the answer. `builders` defaults to the
 *  ones this module set has loaded with (the control plane's, the UI's once its editor loaded them, a CLI session's),
 *  or the control plane's before any load. */
export async function loadPackNow(
  dir: string,
  builders?: Partial<Record<HalfKind, HalfBuilder>>,
): Promise<LoadPackNowResult> {
  const hook = (globalThis as LiveLoadHook).__rp_packLoadNow;
  if (!hook) return { refused: "this host loads no pack live" };
  let answer: LoadPackNowResult;
  try {
    answer = JSON.parse(hook(dir)) as LoadPackNowResult;
  } catch (e) {
    return { refused: `the host's live load failed: ${e instanceof Error ? e.message : String(e)}` };
  }
  if ("refused" in answer) {
    packStatusChanged();
    return answer;
  }
  const halvesBefore = packStatus().loaded[answer.loaded]?.length ?? 0;
  forgetRefused(answer.loaded);
  await loadPacks(nativePackHost(), builders ?? loadedBuilders() ?? APP_BUILDERS);
  // A load that committed a half has scheduled the re-resolve through the registry's commit; one that committed
  // nothing (the folder was already loaded, or TS refused the pack again) moves only the status.
  const status = packStatus();
  // Forgotten just above, so a refusal now is this attempt's.
  const refusedNow = status.refused[answer.loaded];
  if (refusedNow !== undefined || (status.loaded[answer.loaded]?.length ?? 0) === halvesBefore) packStatusChanged();
  return refusedNow !== undefined ? { refused: refusedNow } : answer;
}

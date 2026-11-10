// One effect behind the three renderers (docs/modular-graph-phase-f4c-design.md sections 5.3 and 5.4): the session is
// opened once per (view, system, ROM, roles), reopened when that key or the view's visibility moves, and closed on
// unmount. The lifecycle itself is plain TypeScript (viewSession.ts), so it is tested without React; this file is only
// the effect that calls it, and the handle a renderer draws and reads through.
import { useCallback, useEffect, useRef, useState } from "react";

import type { SystemInfo } from "@retroplug/core/host";
import type { UiHost, ViewDef } from "@retroplug/ui/contribution";
import { shownSession, ViewSessionController, type OpenedSession } from "./viewSession";

/** What a renderer gets: the session to draw (`shownSession` has its three states) and the per-frame reader. */
export interface ViewSessionHandle {
  readonly session: OpenedSession | null | undefined;
  read<R>(reader: (session: OpenedSession) => R): R | null;
}

/** `shown` is the renderer's own reading of the view's visibility (`viewShown`, subscribed by the caller): a window
 *  view passes true, and each tile slot subscribes for itself, so a hidden view of any body kind does no work. */
export function useViewSession(
  view: ViewDef,
  host: UiHost,
  system: SystemInfo | null,
  shown: boolean,
): ViewSessionHandle {
  const controller = useRef<ViewSessionController | null>(null);
  if (controller.current === null) controller.current = new ViewSessionController();
  const [session, setSession] = useState<OpenedSession | null | undefined>(undefined);

  // The key is the sync's to compare, so this re-runs on any candidate change and the controller is what decides
  // whether it is a real one. `setSession` with the same object does not re-render.
  useEffect(() => {
    setSession(controller.current!.sync(view, host, system, shown));
  }, [view, host, system, shown]);

  useEffect(() => () => controller.current?.close(), []);

  // The reader is the controller's, not this render's closure: a frame that lands after a hide or a reopen must not
  // read the session that change closed. It is stable, so a source that memoises it does not reset every render.
  const read = useCallback(
    <R,>(reader: (session: OpenedSession) => R): R | null => controller.current!.read(reader),
    [],
  );

  return { session: shownSession(session, shown, system), read };
}

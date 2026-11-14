// View sessions (docs/modular-graph-phase-f4c-design.md section 5.4): the shell opens one per (view, system, ROM,
// roles), holds it for as long as that key holds, and closes it when the key changes, the view hides or the component
// unmounts.
//
// Plain TypeScript on purpose, so the lifecycle is tested without React: the renderers call this from one effect
// (`useViewSession`), and a package owns no session state of its own. The key is `(view.id, systemId, romPath, roles)`
// by identity: a role edit replaces the roles array (SystemsStore.setRoleConfig slices it on every write), so any role
// write reopens the system's views.
//
// Every call into a package's code goes through a guard here: a `when`, a `visible.get()`, a session's per-frame
// reader, its `signature` and its `close` all run inside the shell's render or its frame loop, and a throw in one of
// them must cost that view only. A guard reports and then answers what "no" answers, so the rest of the UI carries on.
import type { SystemInfo } from "@retroplug/core/host";
import type {
  PixelSession, ReadoutFrame, ReadoutSampler, SideView, UiHost, ViewDef,
} from "@retroplug/ui/contribution";

/** What a session can do on close; both contracts make it optional. */
export interface ViewSession {
  close?(): void;
}

/** What opening a body produces: its kind selects which contract's session it is. */
export type OpenedSession = PixelSession | ReadoutSampler;

/** The two readers are disjoint, so a session's kind reads off the object itself: a renderer only ever opens a view of
 *  its own kind, and these make that a check rather than a claim about what the registry holds. */
export function isPixelSession(session: OpenedSession): session is PixelSession {
  return "frame" in session;
}

export function isReadoutSampler(session: OpenedSession): session is ReadoutSampler {
  return "sample" in session;
}

/** What a session is reused over. `roles` is compared by identity: a role write replaces the array even when the
 *  values are equal, so the comparison is what makes a role edit reopen the view. */
export interface ReopenKey {
  readonly id: string;
  readonly systemId: number;
  readonly romPath: string;
  readonly roles: SystemInfo["roles"];
}

export function reopenKey(view: ViewDef, system: SystemInfo): ReopenKey {
  return { id: view.id, systemId: system.id, romPath: system.romPath, roles: system.roles };
}

export function sameReopenKey(a: ReopenKey, b: ReopenKey): boolean {
  return a.id === b.id && a.systemId === b.systemId && a.romPath === b.romPath && a.roles === b.roles;
}

function message(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** The reported faults, per owner object and per thing that threw: a `when` read during a render or a `visible.get`
 *  on every repaint would otherwise report once per frame, and one bad predicate would bury every other message. */
const reported = new WeakMap<object, Set<string>>();

function reportOnce(owner: object, tag: string, text: string): void {
  let tags = reported.get(owner);
  if (tags === undefined) {
    tags = new Set();
    reported.set(owner, tags);
  }
  if (tags.has(tag)) return;
  tags.add(tag);
  console.warn(text);
}

/** Whether a view's `when` accepts a system: absent = every system, and a throw reads as "no" and is reported (the
 *  predicate runs inside a render, App's open and every tile slot). */
export function viewAppliesTo(view: ViewDef, system: SystemInfo): boolean {
  if (!view.when) return true;
  try {
    return view.when(system);
  } catch (error) {
    reportOnce(view, "when", `[views] ${view.id}: its when threw: ${message(error)}`);
    return false;
  }
}

/** A tile view's visibility as a render reads it: a window view has none (always shown), and a throw in a tile view's
 *  `get` reads as hidden and is reported. */
export function viewShown(view: ViewDef): boolean {
  if (view.placement !== "tile" || view.visible === undefined) return true;
  try {
    return view.visible.get();
  } catch (error) {
    reportOnce(view, "visible", `[views] ${view.id}: its visible threw: ${message(error)}`);
    return false;
  }
}

/** Subscribe to a tile view's visibility, a throwing subscribe reported and treated as "nothing to watch": a flag the
 *  shell cannot watch leaves the view at its last reading rather than taking the renderer down. */
export function subscribeViewShown(view: ViewDef, onChange: () => void): () => void {
  if (view.placement !== "tile" || view.visible === undefined) return () => {};
  try {
    return view.visible.subscribe(onChange);
  } catch (error) {
    reportOnce(view, "visible", `[views] ${view.id}: its visible threw: ${message(error)}`);
    return () => {};
  }
}

/** The systems a side view is open beside, as the grid reads them: a throw in its `get` reads as open beside none and
 *  is reported, so a broken panel costs its own cells only. */
export function sideOpenFor(view: SideView): readonly number[] {
  try {
    return view.openFor.get();
  } catch (error) {
    reportOnce(view, "openFor", `[views] ${view.id}: its openFor threw: ${message(error)}`);
    return [];
  }
}

/** Subscribe to a side view's `openFor`, a throwing subscribe reported and treated as nothing to watch. */
export function subscribeSideOpenFor(view: SideView, onChange: () => void): () => void {
  try {
    return view.openFor.subscribe(onChange);
  } catch (error) {
    reportOnce(view, "openFor", `[views] ${view.id}: its openFor threw: ${message(error)}`);
    return () => {};
  }
}

/** Open a view body's session, reporting a throw and answering null for it: the system is left with no session rather
 *  than with a half-opened one, and the shell renders what an unreadable cart renders. A component view opens nothing
 *  (it is mounted, not sampled), and the body's kind selects which contract's `open` runs. */
export function openSession(view: ViewDef, host: UiHost, systemId: number): OpenedSession | null {
  try {
    const body = view.body;
    if (body.kind === "pixels") return body.open(host, systemId);
    if (body.kind === "readout") return body.open(host, systemId);
    return null;
  } catch (error) {
    console.warn(`[views] ${view.id}: opening the session failed: ${message(error)}`);
    return null;
  }
}

/** A `component` body's fault, as the shell's error boundary records it: the state the boundary renders nothing for,
 *  and the report, once per body (the same rule the guards above follow, since a component that throws on every mount
 *  must not report on every mount). Plain TypeScript, so the pure tier holds it: the boundary itself imports `react`,
 *  which that tier's tsconfig does not resolve. */
export function componentFault(body: object, id: string, error: unknown): { readonly failed: boolean } {
  reportOnce(body, "component", `[views] ${id}: its component threw: ${message(error)}`);
  return { failed: true };
}

/** Close a session, reporting a throw: one view's teardown must not stop the shell's. */
export function closeSession(session: ViewSession | null): void {
  try {
    session?.close?.();
  } catch (error) {
    console.warn(`[views] closing a session failed: ${message(error)}`);
  }
}

/** The default readout dedupe: exactly the fields a readout frame shows. */
export function readoutSignature(frame: ReadoutFrame): string {
  return `${frame.playing ? 1 : 0}|${frame.lines.join("\n")}`;
}

/** One frame's dedupe key: the sampler's own signature, else the default over the lines and the flag. A signature that
 *  throws is reported once and the default taken, so a bad dedupe costs a repaint rather than the frame bus. */
export function readoutKey(view: ViewDef, sampler: ReadoutSampler, frame: ReadoutFrame): string {
  const own = sampler.signature;
  if (!own) return readoutSignature(frame);
  try {
    return own(frame);
  } catch (error) {
    reportOnce(sampler, "signature", `[views] ${view.id}: its signature threw: ${message(error)}`);
    return readoutSignature(frame);
  }
}

/** What a renderer draws, from the controller's state: null while the view is hidden or its system is gone (so a hide
 *  drops the session in the render that sees it, not one effect later), `undefined` before the first open produced
 *  anything (nothing to draw yet, which is NOT "unsupported"), and otherwise the held session, null included (an open
 *  that was refused). */
export function shownSession(
  session: OpenedSession | null | undefined,
  shown: boolean,
  system: SystemInfo | null,
): OpenedSession | null | undefined {
  return shown && system !== null ? session : null;
}

/** Holds one session per reopen key. `sync` is the whole lifecycle (open once, close on a change, a hide or a null
 *  system), `read` is the per-frame access, and `close` is the unmount. */
export class ViewSessionController {
  private held: { key: ReopenKey; session: OpenedSession | null } | null = null;

  /** The session for `system`, opening one when the key moved and closing the previous session either way. A system
   *  that is gone, or a view that is hidden, closes what was held and starts nothing: a hidden tile view does no work.
   *  `shown` is the renderer's reading of the view's visibility (`viewShown`), so a throwing flag costs nothing. */
  sync(view: ViewDef, host: UiHost, system: SystemInfo | null, shown: boolean): OpenedSession | null {
    if (system === null || !shown) {
      this.close();
      return null;
    }
    const key = reopenKey(view, system);
    if (this.held !== null && sameReopenKey(this.held.key, key)) return this.held.session;
    this.close();
    const session = openSession(view, host, system.id);
    this.held = { key, session };
    return session;
  }

  /** Read the session held NOW, for a per-frame handler that must not use a render's closure: a session a hide or a
   *  reopen closed since that render answers null and is not called, so a readout that was showing a dropped session
   *  clears rather than keeping its last lines (a `null` sample is "draw nothing"; the sampler's own `undefined` is
   *  "unreadable this frame, keep what shows"). A reader that throws is reported once, naming the view, and drops the
   *  session, the key staying held so the shell retries at the next key change (as it does for an open that threw). */
  read<R>(reader: (session: OpenedSession) => R): R | null {
    const held = this.held;
    if (held === null || held.session === null) return null;
    try {
      return reader(held.session);
    } catch (error) {
      console.warn(`[views] ${held.key.id}: reading a session failed: ${message(error)}`);
      closeSession(held.session);
      held.session = null;
      return null;
    }
  }

  close(): void {
    if (this.held === null) return;
    closeSession(this.held.session);
    this.held = null;
  }
}

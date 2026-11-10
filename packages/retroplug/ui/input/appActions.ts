// One press's app actions (docs/modular-graph-phase-f4c-design.md section 5.5): the shell's three, then whatever the
// profile bound to that key or controller button, which may be a package's contributed action as well as the shell's.
// Kept out of App so the order and each action's isolation are testable with no display.
//
// The contributed list is a parameter rather than a registry read: App passes the generated registry's list, and a test
// passes its own action, which is the only way an action no product ships can drive this.
import { isAppAction, type AppAction } from "../../src/keyCodes";
import type { ContributedAction, UiHost } from "@retroplug/ui/contribution";

/** What the shell's own three do, by id: App's bodies, built at the press. */
export type ShellActions = Readonly<Record<AppAction, () => void>>;

/** Run one press's resolved action ids in order. A shell id runs its shell body; a contributed id runs its action with
 *  the focused system (null on the start screen); an id neither knows is skipped (a profile can name an action no
 *  product registers any more, as it can name an unknown one today). A contributed action that throws is reported and
 *  the rest of the press still runs: one package's bug must not swallow the shell's menu key on the same press. */
export function runActions(
  ids: readonly string[],
  shell: ShellActions,
  contributed: readonly ContributedAction[],
  host: UiHost,
  systemId: number | null,
): void {
  for (const id of ids) {
    if (isAppAction(id)) {
      shell[id]();
      continue;
    }
    const found = contributed.find((entry) => entry.action.id === id);
    if (!found) continue;
    try {
      found.action.run(host, { systemId });
    } catch (error) {
      const why = error instanceof Error ? error.message : String(error);
      console.warn(`[actions] ${id}: its run threw: ${why}`);
    }
  }
}

// MenuItem builders - pure data, no LVGL/React, so every tree builder (menuDefs, the full-window prompts,
// the song-import picker, the file browser) shares one spelling of each row kind. They used to live
// privately in menuDefs while the overlay builders hand-rolled their own literals, and the copies drifted:
// a read-only line was `disabled` in one prompt and a focusable no-op in the next, so the cursor landed on
// it and Enter did nothing.

import type { MenuAction, MenuItem, MenuTree } from "./menuTree";

/** Wrap `current` within [min, max]: +1 past max → min, -1 below min → max. */
export function cycleInt(current: number, min: number, max: number, dir: 1 | -1): number {
  if (dir > 0) return current >= max ? min : current + 1;
  return current <= min ? max : current - 1;
}

export function action(id: string, label: string, onSelect: () => void, disabled = false): MenuItem {
  // Only attach `disabled` when set, so every enabled action stays byte-identical (toEqual-stable in tests).
  return disabled ? { id, label, kind: "action", onSelect, disabled } : { id, label, kind: "action", onSelect };
}

/** An action that leaves the menu open - the button of a prompt / overlay, which owns its own dismissal. */
export function button(id: string, label: string, onSelect: () => void, disabled = false): MenuItem {
  return { ...action(id, label, onSelect, disabled), keepOpen: true };
}

/** A read-only line (a status, a warning, a notice body): greyed, skipped by nav, inert on click - a
 *  statement, not an option, so Enter always lands on a real choice. */
export function infoRow(id: string, label: string): MenuItem {
  return action(id, label, () => {}, true);
}

export function submenu(id: string, label: string, children: MenuItem[]): MenuItem {
  return { id, label, kind: "submenu", children };
}

export function separator(id: string): MenuItem {
  return { id, label: "", kind: "separator" };
}

/** An inline action-cycler row: one line carrying its own verb list (`[0] TR-606   <  Export...  >`) instead
 *  of a submenu holding one leaf per verb. Left/Right pick the verb, Enter runs it - so a row's actions cost
 *  no extra level of menu. The showing verb is Menu.tsx's state; this only supplies the list. */
export function actionCycler(id: string, label: string, actions: MenuAction[]): MenuItem {
  return { id, label, kind: "actionCycler", actions };
}

/** A value-cycler row: label shows `prefix: names[current]`, Enter/Right step forward, Left back. */
export function cycler(id: string, prefix: string, names: string[], current: number, apply: (next: number) => void): MenuItem {
  const step = (dir: 1 | -1) => apply(cycleInt(current, 0, names.length - 1, dir));
  return { id, label: `${prefix}: ${names[current] ?? "?"}`, kind: "cycler", keepOpen: true, onSelect: () => step(1), onCycle: step };
}

/** A dismissible notice: the body as a read-only line, then a single OK. Ids are `${idp}-body` / `${idp}-ok`. */
export function noticeTree(idp: string, title: string, body: string, onOk: () => void): MenuTree {
  return { title, items: [infoRow(`${idp}-body`, body), button(`${idp}-ok`, "OK", onOk)] };
}

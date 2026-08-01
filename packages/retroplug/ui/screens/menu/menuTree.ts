// The menu data model — pure data, no LVGL/React. Simplified from the legacy menuDefs types
// (packages/ui/src/menu/menuDefs.tsx). "capture" arms on Enter and binds the next key (key rebinding);
// "prompt" arms on Enter and opens an inline text-input / yes-no overlay (profile names, delete confirm).
//
// A leaf carries its own effect as a callback (no dispatch indirection). "cycler" items display their
// current value baked into the label and step it: Enter/onSelect goes forward, Left/Right → onCycle(±1);
// they set keepOpen so the menu stays put while stepping. Submenus nest inline via `children`.

export type MenuItemKind = "action" | "submenu" | "separator" | "cycler" | "capture" | "prompt";

/** A text-input (or yes/no) overlay armed by a "prompt" row. onConfirm returns an error string to keep
 *  the overlay open (shown red), or null to close it — the single success/failure channel. */
export interface PromptSpec {
  title: string;
  initial?: string; // seeds the field (e.g. Rename pre-fills the current name)
  hint?: string; // status-line default; a built-in is used when omitted
  confirm?: boolean; // yes/no dialog — no text field
  // Letter-case policy for typed characters: "mixed" (default) respects Shift (Shift+a → "A"); "upper"
  // forces every letter to uppercase regardless of Shift (LSDj / risa song names are uppercase-only).
  casing?: "mixed" | "upper";
  filter?: (ch: string) => boolean; // per-keystroke character filter (e.g. profile-name chars); sees the CASED char
  onConfirm: (value: string) => string | null;
}

export interface MenuItem {
  id: string;
  label: string;
  kind: MenuItemKind;
  children?: MenuItem[]; // present iff kind === "submenu"
  onSelect?: () => void; // action / cycler (Enter or click)
  onCycle?: (dir: 1 | -1) => void; // cycler (Left/Right — fine step)
  onCoarseStep?: (dir: 1 | -1) => void; // cycler (PageUp/PageDown — coarse step, e.g. the render duration jump)
  keepOpen?: boolean; // stay open after onSelect (cyclers)
  disabled?: boolean; // greyed + inert: skipped by nav, no-op on click (an unavailable-for-this-cart action)
  // present iff kind === "capture" — Enter arms the row, the next input binds (Backspace clears). `source`
  // picks the event bus: "keyboard" (default) captures the next key; "gamepad" captures the next controller
  // button or stick flick. `onCapture` receives the resolved token (key name / SDL button name / axis token).
  capture?: { source?: "keyboard" | "gamepad"; onCapture: (name: string) => void; onClear: () => void };
  prompt?: PromptSpec; // present iff kind === "prompt"
  warn?: boolean; // paint the label in a warning colour (yellow) — e.g. a recent entry whose file is missing
  onRename?: PromptSpec; // F2 on the focused row opens this text prompt (recent-entry rename)
  onDelete?: () => void; // Del on the focused row invokes this (recent-entry removal)
}

export interface MenuTree {
  title: string;
  items: MenuItem[];
}

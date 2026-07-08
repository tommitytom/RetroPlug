// The menu data model — pure data, no LVGL/React. Simplified from the legacy menuDefs types
// (packages/ui/src/menu/menuDefs.tsx). A "capture" kind (re-added for the keyboard bindings editor) arms
// on Enter and binds the next key; legacy's `prompt` kind stays deferred with named-profile CRUD.
//
// A leaf carries its own effect as a callback (no dispatch indirection). "cycler" items display their
// current value baked into the label and step it: Enter/onSelect goes forward, Left/Right → onCycle(±1);
// they set keepOpen so the menu stays put while stepping. Submenus nest inline via `children`.

export type MenuItemKind = "action" | "submenu" | "separator" | "cycler" | "capture";

export interface MenuItem {
  id: string;
  label: string;
  kind: MenuItemKind;
  children?: MenuItem[]; // present iff kind === "submenu"
  onSelect?: () => void; // action / cycler (Enter or click)
  onCycle?: (dir: 1 | -1) => void; // cycler (Left/Right)
  keepOpen?: boolean; // stay open after onSelect (cyclers)
  // present iff kind === "capture" — Enter arms the row, the next key press binds (Backspace clears).
  capture?: { onCapture: (keyName: string) => void; onClear: () => void };
}

export interface MenuTree {
  title: string;
  items: MenuItem[];
}

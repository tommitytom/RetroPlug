// The menu data model — pure data, no LVGL/React. Simplified from the legacy menuDefs types
// (packages/ui/src/menu/menuDefs.tsx): the browser-free port needs only these four kinds (legacy's
// `capture`/`prompt` kinds belong to the deferred bindings editor).
//
// A leaf carries its own effect as a callback (no dispatch indirection). "cycler" items display their
// current value baked into the label and step it: Enter/onSelect goes forward, Left/Right → onCycle(±1);
// they set keepOpen so the menu stays put while stepping. Submenus nest inline via `children`.

export type MenuItemKind = "action" | "submenu" | "separator" | "cycler";

export interface MenuItem {
  id: string;
  label: string;
  kind: MenuItemKind;
  children?: MenuItem[]; // present iff kind === "submenu"
  onSelect?: () => void; // action / cycler (Enter or click)
  onCycle?: (dir: 1 | -1) => void; // cycler (Left/Right)
  keepOpen?: boolean; // stay open after onSelect (cyclers)
}

export interface MenuTree {
  title: string;
  items: MenuItem[];
}

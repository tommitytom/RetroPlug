// The TS fixture pack's UI half: a start-menu row the UI test finds by its label.
import { defineUi } from "@retroplug/ui/contribution";

export default defineUi({
  menus: [{
    placement: "start",
    build: () => [{ id: "tsfix-row", label: "TS Fixture Row", kind: "action", onSelect: () => {} }],
  }],
});

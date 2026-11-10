// The shell's view of the controller apps (src/controller/registry.ts): the generated CONTROLLER_APPS list filled into
// launchpad's registry, and the parse the publishers hand the session node, so the audio side parses nothing and imports
// no schema. The mechanism is the launchpad package's (ts/test/registry.test.ts) and the app's own schema the bridge's
// (ts/test/app.test.ts); what is the shell's is that the product's list reaches it and that a saved config is parsed by
// the right app's schema.
import { test, expect } from "@retroplug/testing";
import { controllerApp, controllerApps, parseAppConfig } from "../../src/controller/registry";
import { CONTROLLER_APPS } from "../../src/generated/packages";
import { MAX_PAGE } from "@retroplug/lsdj-launchpad/lsdjMidiMap";

test("the registry holds the generated list, in product order", () => {
  // Nothing registers by hand: the list is the desktop product's packages' `controllerApps` contributions, so an app a
  // product gains appears here without the shell naming it.
  expect(controllerApps.list().map((app) => app.id)).toEqual(CONTROLLER_APPS.map((app) => app.id));
  expect(controllerApps.list().map((app) => app.id)).toEqual(["lsdj-midimap"]);
  expect(controllerApp("lsdj-midimap")?.label).toBe("LSDj MI.MAP");
});

test("a saved config is parsed by its app's schema, and an unknown id gets an empty one", () => {
  // An unknown id must not throw on the control plane: the session node looks the app up and runs nothing, and a project
  // that names an app the product lost stays loadable.
  expect(controllerApp("nope")).toBe(undefined);
  expect(parseAppConfig("nope", { quantise: "bar" })).toEqual({});
  // Defaults filled, a stale or garbage value snapped to something sane rather than locked out.
  expect(parseAppConfig("lsdj-midimap", undefined)).toEqual({ quantise: "bar", follow: true, page: 0 });
  expect(parseAppConfig("lsdj-midimap", { quantise: "sometime", follow: "yes", page: 99 }))
    .toEqual({ quantise: "bar", follow: true, page: MAX_PAGE });
  expect(parseAppConfig("lsdj-midimap", { quantise: "rowEnd", follow: false, page: 3 }))
    .toEqual({ quantise: "rowEnd", follow: false, page: 3 });
});

test("every row an app offers edits a key of its own schema, with values that schema accepts", () => {
  // The Launchpad submenu builds a row per field the app declares (menuDefs' controllerAppRows), so a typo in a key would
  // be a row that silently edits nothing - the menu would write an unknown key the schema strips, and the knob would
  // never move. EVERY choice value is held, not just the first: a value the schema snaps away from would make the cycler
  // step back to where it was, and only the values a project can reach are the row's.
  let rows = 0;
  for (const app of controllerApps.list()) {
    const schema = app.config.parse({}); // the defaults, which is what a row with no stored value shows
    for (const field of app.menu ?? []) {
      rows++;
      expect(Object.prototype.hasOwnProperty.call(schema, field.key)).toBe(true);
      // The row is one of the config row's shapes (the UI contract's ConfigRow): a choice list, an Off/On toggle or a
      // range of steps. Each step is held like a choice value: the schema must accept it unchanged. The shapes are
      // exclusive, so the defined member is the row's only one (the others are `never`).
      expect(field.toggle !== undefined || field.choices !== undefined || field.range !== undefined).toBe(true);
      const values = field.choices !== undefined
        ? field.choices.map((c) => c.value)
        : field.range !== undefined
          ? field.range.steps
          : [schema[field.key]];
      if (field.choices !== undefined) expect(field.choices.length > 0).toBe(true);
      for (const value of values) expect(app.config.parse({ [field.key]: value })[field.key]).toEqual(value);
    }
  }
  expect(rows > 0).toBe(true); // an app with no rows at all would pass the loop above
});

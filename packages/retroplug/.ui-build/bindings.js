// packages/retroplug/testing/harness.ts
var cases = [];
var scheduled = false;
function test(name, fn) {
  cases.push({ name, fn });
  if (!scheduled) {
    scheduled = true;
    Promise.resolve().then(runAll);
  }
}
function fmt(v) {
  if (v instanceof Uint8Array) {
    const head = Array.from(v.slice(0, 8)).join(",");
    return `Uint8Array(${v.length})[${head}${v.length > 8 ? ",\u2026" : ""}]`;
  }
  try {
    return typeof v === "string" ? JSON.stringify(v) : JSON.stringify(v) ?? String(v);
  } catch {
    return String(v);
  }
}
function expect(actual) {
  return {
    toBe(expected) {
      if (!Object.is(actual, expected)) {
        throw new Error(`expected ${fmt(expected)}, got ${fmt(actual)}`);
      }
    },
    toEqual(expected) {
      const a = JSON.stringify(actual);
      const b = JSON.stringify(expected);
      if (a !== b) throw new Error(`expected ${b}, got ${a}`);
    },
    toBeTruthy() {
      if (!actual) throw new Error(`expected truthy, got ${fmt(actual)}`);
    },
    toBeFalsy() {
      if (actual) throw new Error(`expected falsy, got ${fmt(actual)}`);
    },
    toThrow(match) {
      if (typeof actual !== "function") throw new Error("toThrow expects a function");
      let threw;
      let didThrow = false;
      try {
        actual();
      } catch (e) {
        didThrow = true;
        threw = e;
      }
      if (!didThrow) throw new Error("expected function to throw, but it did not");
      if (match !== void 0) {
        const msg = threw instanceof Error ? threw.message : String(threw);
        const ok = typeof match === "string" ? msg.includes(match) : match.test(msg);
        if (!ok) throw new Error(`expected throw matching ${fmt(match)}, got ${fmt(msg)}`);
      }
    }
  };
}
function exit(code) {
  const g = globalThis;
  g.tjs?.exit(code);
}
async function runAll() {
  const out = ["TAP version 13", `1..${cases.length}`];
  let failed = 0;
  for (let i = 0; i < cases.length; i++) {
    const { name, fn } = cases[i];
    try {
      await fn();
      out.push(`ok ${i + 1} - ${name}`);
    } catch (e) {
      failed++;
      const detail = e instanceof Error ? `${e.message}
${e.stack ?? ""}` : String(e);
      out.push(`not ok ${i + 1} - ${name}`, "  ---");
      for (const line of detail.split("\n")) out.push(`  ${line}`);
      out.push("  ...");
    }
  }
  console.log(out.join("\n"));
  exit(failed ? 1 : 0);
}

// packages/retroplug/test-ui/uiHarness.ts
var rp = globalThis[/* @__PURE__ */ Symbol.for("retroplug-ui")];
var Key = {
  Up: 17,
  Down: 18,
  Right: 19,
  Left: 20,
  Esc: 27,
  Enter: 10
};
var ui = {
  /** Boot the render scaffold + the greenfield UI bundle (idempotent — the runner boots it first). */
  boot() {
    return rp.boot();
  },
  /** Advance the render loop `iterations` blocks (settles the React mount + effects). */
  pump(iterations = 30) {
    rp.pump(iterations);
  },
  /** Detach + re-attach the display on the same runtime (unmount → re-mount). */
  reopen() {
    rp.reopen();
  },
  /** Render the active screen to an ARGB snapshot. */
  snapshot() {
    const s = rp.snapshot();
    return { width: s.width, height: s.height, pixels: new Uint8Array(s.pixels) };
  },
  /** Write the active screen to a PNG (eyeball parity with `pnpm screenshot`). */
  snapshotPng(path) {
    return rp.snapshotPng(path);
  },
  /** Total live lv_binding_js components in the tree. */
  widgetCount() {
    return rp.widgetCount();
  },
  /** Count components of an ECOMP_TYPE (see CompType). */
  countByType(compType) {
    return rp.countByType(compType);
  },
  /** Find a widget tagged via testId, or null. */
  findByTestId(name) {
    return rp.findByTestId(name);
  },
  /** Find the first Text widget whose label equals `text`, or null. */
  findByText(text) {
    return rp.findByText(text);
  },
  /** Find the first Text widget whose label contains `substr`, or null. */
  findByTextContaining(substr) {
    return rp.findByTextContaining(substr);
  },
  /** Find the first widget of a type, or null. */
  findFirstByType(compType) {
    return rp.findFirstByType(compType);
  },
  /** The widget currently focused in the keypad group, or null. */
  focused() {
    return rp.focused();
  },
  /** Tap an LVGL key (see Key) — drives focus-group nav + activation. */
  tapKey(lvKey) {
    rp.tapKey(lvKey);
  },
  /** Click (press+release) at absolute (x,y) → the widget's onClick. */
  clickAt(x, y) {
    rp.clickAt(x, y);
  },
  /** Move the (unpressed) pointer to absolute (x,y) → LVGL hover on the widget under it. */
  moveMouse(x, y) {
    rp.moveMouse(x, y);
  },
  /** Emit one SDL controller button transition on the "gamepad-button" bus (name = SDL canonical, e.g.
   *  "dpdown"/"a"/"leftshoulder"). Menu nav / open-button / game routing all read this. */
  gamepadButton(name, press, pad = 0) {
    rp.gamepadButton(name, press, pad);
  },
  /** Emit a continuous axis value on the "gamepad-axis" bus (axis = "leftx"/"lefty"/…, value in [-1,1]). */
  gamepadAxis(axis, value, pad = 0) {
    rp.gamepadAxis(axis, value, pad);
  },
  /** Press+release a controller button (the pad twin of tapKey): a single deliberate tap. */
  gamepadTap(name, pad = 0) {
    rp.gamepadButton(name, true, pad);
    rp.gamepadButton(name, false, pad);
  },
  /** Advance the emulator by `ms` so tiles receive live frames (pump() only ticks LVGL). */
  advance(ms) {
    rp.advance(ms);
  }
};

// packages/retroplug/test-ui/bindings.test.ts
var KEY_BACKSPACE = 8;
var keyQ = "Q".charCodeAt(0);
function navTo(substr, maxSteps = 24) {
  for (let i = 0; i < maxSteps; i++) {
    const f2 = ui.focused();
    if (f2 && f2.text.includes(substr)) return true;
    ui.tapKey(Key.Down);
    ui.pump(2);
  }
  const f = ui.focused();
  return !!f && f.text.includes(substr);
}
test("Settings \u2192 Keyboard Bindings: arm, capture a key, and clear a binding", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  expect(navTo("Settings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(navTo("Keyboard Bindings")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(8);
  expect(ui.findByTextContaining("A: Z, z") != null).toBeTruthy();
  expect(ui.findByTextContaining("Start: Enter") != null).toBeTruthy();
  expect(ui.findByTextContaining("Reset Keyboard to Defaults") != null).toBeTruthy();
  expect(navTo("A: Z, z")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.focused().text.includes("Press a key")).toBeTruthy();
  ui.tapKey(keyQ);
  ui.pump(10);
  expect(ui.findByText("A: Q") != null).toBeTruthy();
  expect(ui.findByTextContaining("Press a key") == null).toBeTruthy();
  expect(navTo("A: Q")).toBeTruthy();
  ui.tapKey(KEY_BACKSPACE);
  ui.pump(10);
  expect(ui.findByText("A: -") != null).toBeTruthy();
  expect(navTo("New Profile")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.findByTextContaining("New profile name:") != null).toBeTruthy();
  for (const ch of "wasd") ui.tapKey(ch.charCodeAt(0));
  ui.pump(4);
  expect(ui.findByTextContaining("wasd_") != null).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(10);
  expect(ui.findByTextContaining("New profile name:") == null).toBeTruthy();
  expect(ui.findByTextContaining("Profile: wasd") != null).toBeTruthy();
  expect(navTo("New Profile")).toBeTruthy();
  ui.tapKey(Key.Enter);
  ui.pump(6);
  for (const ch of "default") ui.tapKey(ch.charCodeAt(0));
  ui.pump(4);
  ui.tapKey(Key.Enter);
  ui.pump(6);
  expect(ui.findByTextContaining("already exists") != null).toBeTruthy();
  ui.tapKey(Key.Esc);
  ui.pump(6);
  expect(ui.findByTextContaining("New profile name:") == null).toBeTruthy();
  expect(ui.findByTextContaining("Profile: wasd") != null).toBeTruthy();
  ui.snapshotPng("/tmp/greenfield-ui-bindings.png");
});

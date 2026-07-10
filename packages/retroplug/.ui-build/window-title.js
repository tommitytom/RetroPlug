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

// packages/retroplug/test-ui/window-title.test.ts
test("the window title is set to RetroPlug + version on (re)mount, with no project segment when empty", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(30);
  const titles = [];
  globalThis.__rp_setWindowTitle = (t) => {
    titles.push(t);
  };
  ui.reopen();
  const last = titles[titles.length - 1];
  expect(last != null && /^RetroPlug v.+$/.test(last)).toBeTruthy();
  expect(!!last && last.includes(" - ")).toBeFalsy();
  delete globalThis.__rp_setWindowTitle;
});

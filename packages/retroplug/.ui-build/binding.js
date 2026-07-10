// packages/retroplug-greenfield/testing/harness.ts
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

// packages/retroplug-greenfield/test-ui/uiHarness.ts
var rp = globalThis[/* @__PURE__ */ Symbol.for("retroplug-ui")];
function isFlat(snap) {
  const p = snap.pixels;
  if (p.length < 8) return true;
  for (let i = 4; i + 4 <= p.length; i += 4) {
    if (p[i] !== p[0] || p[i + 1] !== p[1] || p[i + 2] !== p[2] || p[i + 3] !== p[3]) return false;
  }
  return true;
}
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
  }
};

// packages/retroplug-greenfield/test-ui/binding.test.ts
test("the binding layer re-renders store views on a mutation driven through the real reconciler", () => {
  expect(ui.boot()).toBeTruthy();
  ui.pump(40);
  const snap = ui.snapshot();
  expect(snap.width).toBe(480);
  expect(isFlat(snap)).toBeFalsy();
  const frames0 = ui.findByTextContaining("frames:");
  expect(frames0 != null).toBeTruthy();
  expect(frames0.text !== "frames:0").toBeTruthy();
  const zoom0 = ui.findByTextContaining("zoom:");
  expect(zoom0 != null).toBeTruthy();
  const dirty0 = ui.findByTextContaining("dirty:");
  expect(dirty0?.text).toBe("dirty:no");
  const btn = ui.findByTextContaining("zoom+");
  expect(btn != null).toBeTruthy();
  ui.clickAt(btn.x + Math.floor(btn.width / 2), btn.y + Math.floor(btn.height / 2));
  ui.pump(20);
  ui.snapshotPng("/tmp/greenfield-ui-binding.png");
  const zoom1 = ui.findByTextContaining("zoom:");
  expect(zoom1 != null).toBeTruthy();
  expect(zoom1.text !== zoom0.text).toBeTruthy();
  const dirty1 = ui.findByTextContaining("dirty:");
  expect(dirty1?.text).toBe("dirty:yes");
});

// The view session lifecycle (docs/modular-graph-phase-f4c-design.md section 5.4), over fake views and a fake host: no
// React, no display. The renderers call the same controller from one effect (ui/views/useViewSession.ts), so what is
// held, reopened, closed and read here is what an overlay or the HD screen gets.
import { test, expect } from "@retroplug/testing";
import type { SystemInfo } from "@retroplug/core/host";
import type {
  PixelSession, ReadoutFrame, Signal, TileView, UiHost, ViewDef, WindowView,
} from "@retroplug/ui/contribution";
import { createSignal } from "@retroplug/ui/menu";

import { warned } from "../_warnings";
import {
  componentFault,
  isPixelSession,
  isReadoutSampler,
  openSession,
  readoutKey,
  reopenKey,
  sameReopenKey,
  shownSession,
  subscribeViewShown,
  ViewSessionController,
  viewAppliesTo,
  viewShown,
} from "../../ui/views/viewSession";

/** The host a fake view's `open` receives: the fakes read nothing of it, and no group is reached. */
const host = {} as UiHost;

function system(over: Partial<SystemInfo> = {}): SystemInfo {
  return {
    id: 1,
    platform: "gb",
    core: "sameboy",
    romPath: "/roms/game.gb",
    savPath: "/roms/game.sav",
    savSuffix: 0,
    embedded: false,
    battery: true,
    missing: false,
    roles: [],
    ...over,
  };
}

/** A fake session that records its close and every frame it was asked for. */
function session(label: string, opened: string[], closed: string[], frames: string[] = []): PixelSession {
  opened.push(label);
  return {
    frame: () => {
      frames.push(label);
      return null;
    },
    close: () => closed.push(label),
  };
}

/** A window view whose body opens a recorded session, so the controller's decisions are all it tests. */
function fakeView(id: string, opened: string[], closed: string[], frames: string[] = []): WindowView {
  return {
    id,
    label: id,
    placement: "window",
    body: { kind: "pixels", open: () => session(id, opened, closed, frames) },
  };
}

test("a session opens once per key and closes on the next change", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  const sys = system();
  const first = held.sync(view, host, sys, true);
  // The same key, twice: the same session object, opened once.
  expect(held.sync(view, host, sys, true) === first).toBe(true);
  expect(opened).toEqual(["test-view"]);
  expect(closed).toEqual([]);
  // A ROM change is a new key: the old session closes, a new one opens.
  const second = held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true);
  expect(second === first).toBe(false);
  expect(opened).toEqual(["test-view", "test-view"]);
  expect(closed).toEqual(["test-view"]);
  held.close();
  expect(closed).toEqual(["test-view", "test-view"]);
});

test("a role edit reopens the views of that system", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  const sys = system({ roles: [{ kind: "sameboy", config: {} }] as SystemInfo["roles"] });
  held.sync(view, host, sys, true);
  // SystemsStore.setRoleConfig always hands back a NEW roles array, so identity is what the key sees.
  const edited = { ...sys, roles: [...sys.roles] };
  held.sync(view, host, edited, true);
  expect(opened.length).toBe(2);
  expect(closed.length).toBe(1);
  // A new system view with the SAME roles array is not a role edit: no reopen.
  held.sync(view, host, { ...edited }, true);
  expect(opened.length).toBe(2);
});

test("a hidden tile view opens nothing, and a visibility flip closes what was open", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const visible = createSignal(false);
  const view: TileView = {
    id: "test-overlay",
    label: "Test Overlay",
    placement: "tile",
    visible,
    body: { kind: "pixels", open: () => session("overlay", opened, closed) },
  };
  const held = new ViewSessionController();
  expect(held.sync(view, host, system(), viewShown(view))).toBe(null);
  expect(opened).toEqual([]); // hidden: no session and no work
  visible.set(true);
  expect(held.sync(view, host, system(), viewShown(view)) === null).toBe(false);
  expect(opened).toEqual(["overlay"]);
  visible.set(false);
  expect(held.sync(view, host, system(), viewShown(view))).toBe(null);
  expect(closed).toEqual(["overlay"]); // the flip closes it
});

test("a system that is gone closes the session", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const held = new ViewSessionController();
  held.sync(view, host, system(), true);
  expect(held.sync(view, host, null, true)).toBe(null);
  expect(closed).toEqual(["test-view"]);
});

test("a throwing open is reported and leaves the system without a session", () => {
  let opens = 0;
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => {
        opens += 1;
        throw new Error("the cart is unreadable");
      },
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  const { result, warnings } = warned(() => held.sync(view, host, sys, true));
  expect(result).toBe(null);
  expect(warnings).toEqual(["[views] test-view: opening the session failed: the cart is unreadable"]);
  // The key is held with no session: the shell retries only when the key moves, not on every frame's render.
  held.sync(view, host, sys, true);
  expect(opens).toBe(1);
  held.close(); // a null session closes without a warning or a second throw
});

test("a throwing close is reported and does not stop the next open", () => {
  const opened: string[] = [];
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => {
        opened.push("open");
        return { frame: () => null, close: () => { throw new Error("teardown failed"); } };
      },
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  held.sync(view, host, sys, true);
  const { warnings } = warned(() => held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true));
  expect(warnings).toEqual(["[views] closing a session failed: teardown failed"]);
  expect(opened.length).toBe(2);
});

test("a component body opens no session, and the key is its id, the system, the ROM and the roles", () => {
  const component: ViewDef = {
    id: "test-component",
    label: "Test Component",
    placement: "window",
    body: { kind: "component", component: () => null },
  };
  expect(openSession(component, host, 1)).toBe(null);

  const sys = system({ roles: [{ kind: "sameboy", config: {} }] as SystemInfo["roles"] });
  const view = fakeView("test-view", [], []);
  const key = reopenKey(view, sys);
  expect(sameReopenKey(key, reopenKey(view, { ...sys }))).toBe(true);
  expect(sameReopenKey(key, reopenKey(view, { ...sys, romPath: "/roms/other.gb" }))).toBe(false);
  expect(sameReopenKey(key, reopenKey(view, { ...sys, roles: [...sys.roles] }))).toBe(false);
  expect(sameReopenKey(reopenKey(view, sys), reopenKey(fakeView("other-view", [], []), sys))).toBe(false);
});

test("a frame reads the session held now: none before the first open, none after a hide or a reopen", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const frames: string[] = [];
  const view = fakeView("test-view", opened, closed, frames);
  const held = new ViewSessionController();
  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(null); // nothing opened: no call at all

  const sys = system();
  held.sync(view, host, sys, true);
  held.read((s) => (isPixelSession(s) ? s.frame() : null));
  expect(frames).toEqual(["test-view"]);

  // A hide closes the session, and a frame that lands before the next render must not reach it.
  held.sync(view, host, sys, false);
  expect(held.read((s) => (isPixelSession(s) ? s.frame() : null))).toBe(null);
  expect(closed).toEqual(["test-view"]);
  expect(frames).toEqual(["test-view"]);

  // A reopen swaps the session: a frame reads the NEW one, never the closed one.
  held.sync(view, host, sys, true);
  const reopened = held.sync(view, host, { ...sys, romPath: "/roms/other.gb" }, true);
  let read: unknown = null;
  held.read((s) => {
    read = s;
    return null;
  });
  expect(read === reopened).toBe(true);
  expect(read === null).toBe(false);
});

test("a reader that throws is reported once and drops the session until the key moves", () => {
  let reads = 0;
  const closed: string[] = [];
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "pixels",
      open: () => ({
        frame: () => {
          reads += 1;
          throw new Error("the frame is broken");
        },
        close: () => closed.push("test-view"),
      }),
    },
  };
  const held = new ViewSessionController();
  const sys = system();
  held.sync(view, host, sys, true);
  const { result, warnings } = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
  expect(result).toBe(null);
  expect(warnings).toEqual(["[views] test-view: reading a session failed: the frame is broken"]);
  expect(closed).toEqual(["test-view"]); // the faulted session is closed, not left half-usable
  // Dropped, so the next frame does not call it again: one report per faulty session.
  const after = warned(() => held.read((s) => (isPixelSession(s) ? s.frame() : null)));
  expect(after.result).toBe(null);
  expect(after.warnings).toEqual([]);
  expect(reads).toBe(1);
});

test("a live sampler's undefined passes through, and a dropped session answers null so its strip clears", () => {
  // The two "nothing shows" answers are different: a sampler's undefined is "unreadable this frame, keep what shows",
  // while a session the controller dropped is null ("draw nothing"), which is what clears a readout's last lines.
  const view: WindowView = {
    id: "test-view",
    label: "Test View",
    placement: "window",
    body: {
      kind: "readout",
      open: () => ({
        sample: () => {
          throw new Error("the RAM is unreadable");
        },
      }),
    },
  };
  const held = new ViewSessionController();
  held.sync(view, host, system(), true);
  const { result } = warned(() => held.read((s) => (isReadoutSampler(s) ? s.sample() : null)));
  expect(result).toBe(null); // the sampler threw, so the session was dropped
  const unreadable = new ViewSessionController();
  const view2: WindowView = {
    id: "test-view-2",
    label: "Test View 2",
    placement: "window",
    body: { kind: "readout", open: () => ({ sample: () => undefined }) },
  };
  unreadable.sync(view2, host, system(), true);
  expect(unreadable.read((s) => (isReadoutSampler(s) ? s.sample() : null))).toBe(undefined); // not dropped
});

test("a view that has not opened yet draws nothing, and a hide drops the session in the same render", () => {
  const opened: string[] = [];
  const closed: string[] = [];
  const view = fakeView("test-view", opened, closed);
  const sys = system();
  const session = openSession(view, host, sys.id);
  // Not opened yet: undefined, which a renderer draws nothing for. It must not read as "unsupported".
  expect(shownSession(undefined, true, sys)).toBe(undefined);
  // Opened, then hidden or its system gone: null in the render that sees it, not one effect later.
  expect(shownSession(session, true, sys) === session).toBe(true);
  expect(shownSession(session, false, sys)).toBe(null);
  expect(shownSession(session, true, null)).toBe(null);
});

test("a throwing when reads as no and a throwing visible as hidden, each reported once", () => {
  const sys = system();
  let whenCalls = 0;
  const visible: Signal<boolean> = {
    get: () => {
      throw new Error("the flag is broken");
    },
    set: () => {},
    subscribe: () => {
      throw new Error("the flag is broken");
    },
  };
  const view: TileView = {
    id: "test-overlay",
    label: "Test Overlay",
    placement: "tile",
    when: () => {
      whenCalls += 1;
      throw new Error("the predicate is broken");
    },
    visible,
    body: { kind: "readout", open: () => null },
  };
  const { result, warnings } = warned(() => ({
    applies: viewAppliesTo(view, sys),
    shown: viewShown(view),
    unsubscribe: subscribeViewShown(view, () => {}),
  }));
  expect(result.applies).toBe(false); // a throw is "no": nothing renders, and the render tree survives
  expect(result.shown).toBe(false);
  expect(typeof result.unsubscribe).toBe("function"); // nothing to watch, and no throw out of the hook
  expect(warnings.filter((w) => w.includes("its when threw"))).toEqual([
    "[views] test-overlay: its when threw: the predicate is broken",
  ]);
  expect(warnings.filter((w) => w.includes("its visible threw"))).toEqual([
    "[views] test-overlay: its visible threw: the flag is broken",
  ]);
  // Read again on the next render: still "no", and not a second report for the same thing.
  const again = warned(() => {
    viewAppliesTo(view, sys);
    viewShown(view);
  });
  expect(again.warnings).toEqual([]);
  expect(whenCalls).toBe(2);
});

test("a throwing signature is reported once and the default over the lines and the flag is taken", () => {
  const view = fakeView("test-view", [], []);
  const frame: ReadoutFrame = { lines: ["a", "b"], playing: true };
  const sampler = {
    sample: () => frame,
    signature: (): string => {
      throw new Error("the signature is broken");
    },
  };
  const { result, warnings } = warned(() => readoutKey(view, sampler, frame));
  expect(result).toBe("1|a\nb");
  expect(warnings).toEqual(["[views] test-view: its signature threw: the signature is broken"]);
  expect(warned(() => readoutKey(view, sampler, frame)).warnings).toEqual([]);
});

test("a component body's fault latches the boundary's state and is reported once per body", () => {
  // The boundary itself is the one piece of the shell's view code that cannot be tested here (it imports react, which
  // this tier's tsconfig does not resolve); its whole state logic is this function, and the class only wires React's
  // getDerivedStateFromError to it.
  const body = { kind: "component", component: () => null };
  const { result, warnings } = warned(() => componentFault(body, "test-component", new Error("the widget is broken")));
  expect(result).toEqual({ failed: true }); // what the boundary renders nothing for
  expect(warnings).toEqual(["[views] test-component: its component threw: the widget is broken"]);
  // The same body failing again (a remount under the same view) is the same fault: no second report.
  expect(warned(() => componentFault(body, "test-component", new Error("again"))).warnings).toEqual([]);
  // Another body is another fault, reported in its own words.
  const other = { kind: "component", component: () => null };
  expect(warned(() => componentFault(other, "other-component", "not an Error")).warnings).toEqual([
    "[views] other-component: its component threw: not an Error",
  ]);
});

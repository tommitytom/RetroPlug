// Each surface's host offers exactly the groups its type names (docs/modular-graph-phase-f4b-design.md section 3.2): a
// group a surface lacks does not exist on its host, so asking for one is a compile error rather than a member that is
// undefined when a hook runs - which is what makes a surface's abilities its host's group list. The cases below are
// held by `tsc`; the one runtime case checks the table the compiler reads.
import { test, expect } from "@retroplug/testing";
import type { Host, KitBank } from "@retroplug/core/host";
import type { KitCompileResult } from "@retroplug/dsp/kitContract";
import type { CliHost } from "@retroplug/core/cliTool";
import { cliHost } from "../../cli/cliHost";
import type { RenderHostGroups } from "../../src/render/renderHost";
import { renderHost } from "../../src/render/renderHost";
import type { RenderContext } from "../../src/render/types";
import type { Session } from "../../src/bootSession";
import { composeAppStores } from "../../src/appStores";
import { MockBackend } from "../../testing/mockBackend";
import { createUiHost, type UiHost } from "../../ui/host/uiHost";

/** A hook that opens a file dialog. */
function needsDialogs(host: Host<"dialogs">): unknown {
  return host.dialogs;
}
/** A hook that owns the process. */
function needsProcess(host: Host<"process">): unknown {
  return host.process;
}
/** A hook that drives a booted system, which is what a render hook does. */
function needsEmulator(host: Host<"emulator">): unknown {
  return host.emulator;
}

test("a UI host is its ten groups and no others", () => {
  const stores = composeAppStores({ backend: new MockBackend("/cfg") });
  const ui: UiHost = createUiHost(stores, {
    views: { open: () => {} },
    beginImport: () => {},
  });
  expect(Object.keys(ui).sort()).toEqual(
    ["config", "controller", "devices", "dialogs", "files", "liveSongs", "png", "songs", "systems", "views"].sort(),
  );
  // The UI offers what a construct hook, an import and a menu need, and a hook narrower than the host takes it.
  expect(needsDialogs(ui) === ui.dialogs).toBe(true);

  // The per-surface cases. Nothing here runs - the compiler is what checks it - so the CLI and render hosts are taken
  // as types: a mock-tier test has no native session and no render context to build one over.
  const typeCases = (cli: CliHost, render: RenderHostGroups): void => {
    // Each surface's exact group list: a group added to or dropped from a host fails one of these.
    const uiGroups: Record<keyof UiHost, true> = {
      files: true, png: true, systems: true, songs: true, liveSongs: true, dialogs: true, devices: true, views: true,
      controller: true, config: true,
    };
    const cliGroups: Record<keyof CliHost, true> = {
      files: true, png: true, kits: true, devices: true, process: true, songs: true,
    };
    const renderGroups: Record<keyof RenderHostGroups, true> = { emulator: true, files: true };
    void uiGroups;
    void cliGroups;
    void renderGroups;

    // @ts-expect-error a CLI host has no dialogs group: a tool takes paths, so it never asks for a picker
    needsDialogs(cli);
    // @ts-expect-error a render host has no dialogs group either
    needsDialogs(render);
    // @ts-expect-error the UI host has no process group: the UI does not own the process
    needsProcess(ui);
    // @ts-expect-error the CLI host has no emulator group: a tool runs no live system
    needsEmulator(cli);
    expect(needsEmulator(render) === render.emulator).toBe(true);
    // @ts-expect-error the UI host has no kits group: the UI composes no audio driver
    ui.kits;
    // @ts-expect-error the CLI host has no views group: a tool shows no screen
    cli.views;
    // @ts-expect-error the render host has no systems group: a render owns one booted system, not a project's list
    render.systems;

    // A compiled kit is dsp's own type, member for member (core declares it so a host can name it without importing
    // dsp).
    const asDsp = (b: KitBank): KitCompileResult => b;
    const asCore = (r: KitCompileResult): KitBank => r;
    void asDsp;
    void asCore;
  };
  // Marks the type cases used without running them.
  expect(typeof typeCases).toBe("function");
});

test("a CLI host is its six groups, and its arguments are the tool's own", () => {
  const backend = new MockBackend("/cfg");
  // A tool run has a native host and a real audio driver; a mock tier has neither, and neither is read to have a host.
  const session = {
    backend,
    audio: { compileKitFormat: async () => ({ bytes: new Uint8Array(0), errors: [] }) },
  } as unknown as Session;
  const cli = cliHost(session, ["--show-song", "cart.srm"]);
  const keys = Object.keys(cli).sort();
  expect(keys).toEqual(["devices", "files", "kits", "png", "process", "songs"]);
  // `run(host, args)`'s args are the host's, without the command name the dispatcher stripped, as a copy the tool
  // cannot mutate the dispatcher's vector through.
  expect(cli.process.args()).toEqual(["--show-song", "cart.srm"]);
  const args = cli.process.args();
  args.push("extra");
  expect(cli.process.args()).toEqual(["--show-song", "cart.srm"]);
  // A session run by path has none.
  expect(cliHost(session).process.args()).toEqual([]);
  // The RPC caller is built on first call, not on the host's: a mock tier has no channel, and only a tool that reaches
  // a native facet should need one.
  expect(typeof cli.devices.rpc).toBe("function");
  expect(() => cli.devices.rpc("version")).toThrow("no native backend");
});

test("a render host is its two groups", () => {
  const backend = new MockBackend("/cfg");
  // A render's context is the backend, the audio driver and the stores; this host reads the first two.
  const ctx = { backend, audio: { renderAudio: () => {}, pressButton: () => {} } } as unknown as RenderContext;
  const host: RenderHostGroups = renderHost(ctx);
  expect(Object.keys(host).sort()).toEqual(["emulator", "files"]);
  expect(Object.keys(host.emulator).sort()).toEqual([
    "pressButton",
    "readCpu",
    "readRam",
    "renderAudio",
    "writeRam",
  ]);
});

test("a controller target is checked against the launchpad package's list", () => {
  const stores = composeAppStores({ backend: new MockBackend("/cfg") });
  const ui = createUiHost(stores, { views: { open: () => {} }, beginImport: () => {} });
  // A package hands the target over as an open string, and an unknown one would leave launches going nowhere, so it is
  // refused here rather than written into the live settings and the next save.
  expect(() => ui.controller.set({ target: "nowhere" })).toThrow("controller target 'nowhere'");
  expect(ui.controller.settings().target).toBe("system");
  ui.controller.set({ target: "midiOut" });
  expect(ui.controller.settings().target).toBe("midiOut");
  // A patch without a target leaves it alone.
  ui.controller.set({ enabled: true });
  expect(ui.controller.settings().target).toBe("midiOut");
  expect(ui.controller.settings().enabled).toBe(true);
});

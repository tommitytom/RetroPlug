// The UI surface's host (docs/modular-graph-phase-f4b-design.md section 3.2): built once, in App.tsx, over the store
// graph, and the way a cart's or a console's UI code reaches the shell. It offers the ten groups a UI has - files, png,
// systems, songs, liveSongs, dialogs, devices, views, controller, config - and nothing a UI cannot do: no kits (the UI
// composes no audio driver) and no process (the UI does not own the process).
import type { UiHost } from "@retroplug/ui/contribution";
import type { AppStores } from "../../src/appStores";
import type { ControlPlaneBackend } from "../../src/backend";
import type { SystemsStore } from "../../src/systemsStore";
import type { Call, DevicesHost, Host, SystemInfo, SystemsHost, ViewsHost } from "@retroplug/core/host";
import { CONTROLLER_TARGET_VALUES, type ControllerTarget } from "@retroplug/launchpad/enums";
import { roleConfig } from "@retroplug/core/systemRoles";
import { filesPngHost } from "../../src/hostGroups";
import { resolveSavPath } from "../../src/savPaths";
import { cartNodeId, drivenCartSystem, systemNodeId } from "../../src/graph/buildGraph";
import { controllerSyncOverride } from "../../src/graph/controller";
import { controllerApps } from "../../src/controller/registry";
import { mutateLiveSav } from "../../src/tracker/liveSav";
import { makeCall } from "../../src/rpcClient";
import { createMidiClient } from "../../src/realBackend";
import { batteryListing } from "../../src/batteryListing";

/** The UI surface's host, and the one definition of it: the ui package declares it (`@retroplug/ui/contribution`), so a
 *  contribution's builder is typed against the host the shell builds and a package, which may never import the shell,
 *  can still name it. This module re-exports the package's type rather than restating the group list. */
export type { UiHost } from "@retroplug/ui/contribution";

/** What the host needs that is not a store: the window the views open in and the song-import picker, both of them App's
 *  React state (the picker is useSongImport's overlay). */
export interface UiHostPeers {
  readonly views: ViewsHost;
  readonly beginImport: (sys: SystemInfo, source: Uint8Array) => void;
}

/** The host groups an import uses - the file I/O, the PNG codec and the running systems - over a backend and a systems
 *  store. `createUiHost` spreads it into the UI host, and the host-groups test builds it on its own; a surface's whole
 *  host satisfies the import hook either way. */
export function importHost(
  backend: ControlPlaneBackend,
  systems: SystemsStore,
): Host<"files" | "png" | "systems"> {
  return { ...filesPngHost(backend), systems: systemsHost(backend, systems) };
}

export function createUiHost(stores: AppStores, peers: UiHostPeers): UiHost {
  const systems = stores.project.systems;
  // The RPC caller and a MIDI client are built on first use: a UI that never builds an N8 client (every mock-tier test,
  // a DAW host with no device) must not need the native RPC channel to exist to have a host at all. A function rather
  // than a getter, so copying the group (`{ ...host.devices }`) does not resolve it.
  let rpc: Call | undefined;
  const devices: DevicesHost = {
    rpc: (method, ...params) => (rpc ??= makeCall())(method, ...params),
    midi: () => createMidiClient(),
  };
  return {
    ...importHost(stores.backend, systems),
    songs: { list: (bytes) => batteryListing(bytes) },
    liveSongs: {
      mutate: (sys, fn) => mutateLiveSav(stores.backend, systems, sys, fn),
      beginImport: peers.beginImport,
    },
    dialogs: { browse: (opts) => stores.backend.openFileBrowser(opts) },
    devices,
    views: peers.views,
    controller: {
      settings: () => stores.project.settings().controller,
      // A package hands the target over as a plain string, and ProjectStore writes the patch into the live settings,
      // the published document and the next .rplg save unread, so an unknown value would be a controller that launches
      // nowhere. It is refused here, naming the value, rather than laundered into the closed type with a cast: a bad
      // target is a package's bug, not a user's input to drop quietly.
      set: (patch) => {
        const { target, ...rest } = patch;
        if (target === undefined) {
          stores.project.setController(rest);
          return;
        }
        if (!isControllerTarget(target))
          throw new Error(`controller target '${target}' is not one of ${CONTROLLER_TARGET_VALUES.join(", ")}`);
        stores.project.setController({ ...rest, target });
      },
      cartSync: () => stores.project.controllerCartSync(),
      // What the published document will have the cart's sync node run, computed by the SAME helper buildGraph uses, so
      // a row cannot describe something other than what is happening.
      syncOverride: () => {
        const settings = stores.project.settings().controller;
        return controllerSyncOverride(
          systems.view(),
          settings.enabled ? { ...settings, cartSync: stores.project.controllerCartSync() } : null,
        );
      },
      apps: () => controllerApps.list(),
    },
    config: {
      controlSurface: () => stores.userConfig.controlSurface(),
      setControlSurface: (patch) => void stores.userConfig.setControlSurface(patch),
    },
  };
}

/** The live systems of a project: the view, the battery, the role edits and the nodes the graph places them at. */
function systemsHost(backend: ControlPlaneBackend, systems: SystemsStore): SystemsHost {
  const view = () => systems.view();
  return {
    view,
    setRoleConfig: (id, kind, config) => systems.setRoleConfig(id, kind, config),
    reload: (id) => systems.reloadSystem(id),
    readSram: (id) => systems.readSram(id),
    readRam: (id) => systems.readRam(id),
    loadSram: (id, path) => systems.loadSram(id, path),
    savPathOf: (id) => {
      const sys = view().find((s) => s.id === id);
      // A system that went away between the caller reading it and its write has no path, and quietly writing its
      // battery to "" would be a failure reported as a success.
      return sys ? resolveSavPath(sys.romPath, sys.savSuffix, sys.savPath) : null;
    },
    nodeIdOf: (id) => systemNodeId(id),
    drivenCart: () => {
      const cart = drivenCartSystem(
        view().map((system) => ({
          system,
          platform: system.platform,
          coreConfig: roleConfig(system.roles, system.core),
        })),
      );
      return cart ? { id: cart.system.id, nodeId: cartNodeId(cart.system.id) } : null;
    },
    stageNodeMidi: (nodeId, bytes) => backend.stageNodeMidi(nodeId, bytes),
    capability: (nodeId, op, args, onProgress) => backend.capability(nodeId, op, args, onProgress),
  };
}

/** Whether `value` is a launch target the shell knows: the controller-host contract carries it as an open string, and
 *  this is the one place it becomes the launchpad package's closed type. */
function isControllerTarget(value: string): value is ControllerTarget {
  return (CONTROLLER_TARGET_VALUES as readonly string[]).includes(value);
}

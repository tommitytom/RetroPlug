// UI entry. Evaluated once by the host; the mount hooks render the tree after a display is
// attached (and unmount on detach), keeping the QuickJS context alive across a window close/reopen.
//
// The tree is the store-backed app: <StoreProvider> composes the store graph over the
// __rpcSend backend and multiplexes its change notifications to the hooks; <FocusProvider> owns the
// keyboard "sink" group; <App> is the controller (start menu / grid + instance menu). More screens grow
// under App as they're ported.

import { Render } from "lvgljs-ui";

import { StoreProvider } from "./stores/StoreProvider";
import { FocusProvider } from "./lvgl/FocusProvider";
import { App } from "./App";
import { native, pluginNamespace } from "./lvgl/nativeHooks";
import { gridContentSize, resolveZoom, SystemLayout } from "./screens/grid/layout";

function Root() {
  return (
    <StoreProvider>
      <FocusProvider>
        <App />
      </FocusProvider>
    </StoreProvider>
  );
}

function mountUI() {
  Render.render(<Root />);
}
function unmountUI() {
  (Render as unknown as { unmount?: () => void }).unmount?.();
}
// The window size the editor should open at for the already-loaded control-plane project, computed the same
// way App fits the window at runtime (resolveZoom + gridContentSize). The native editor calls this BEFORE the
// window first maps and sizes to it, so a compositor that captures a floating window's size at map time
// (Hyprland re-applies that remembered size on every drag — hyprwm/Hyprland#2105) remembers the real size
// instead of the 480×432 default. Returns null for an empty project (the start menu keeps the default) or
// when there's no control plane (the headless harness) — the native side leaves the default size then.
function initialWindowSize(): { width: number; height: number } | null {
  const stores = pluginNamespace()?.stores;
  if (!stores) return null;
  const count = stores.project.systems.view().length;
  if (count === 0) return null;
  const settings = stores.project.settings();
  const zoom = resolveZoom(settings.zoom, stores.userConfig.config().defaultZoom);
  const { width, height } = gridContentSize(count, settings.layout as SystemLayout, zoom);
  return { width, height };
}

native.__rp_mountUI = mountUI;
native.__rp_unmountUI = unmountUI;
native.__rp_initialWindowSize = initialWindowSize;

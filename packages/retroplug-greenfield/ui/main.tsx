// Greenfield UI entry. Evaluated once by the host; the mount hooks render the tree after a display is
// attached (and unmount on detach), keeping the QuickJS context alive across a window close/reopen.
//
// The tree is the store-backed app: <StoreProvider> composes the greenfield store graph over the
// __rpcSend backend and multiplexes its change notifications to the hooks; <FocusProvider> owns the
// keyboard "sink" group; <App> is the controller (start menu / grid + instance menu). More screens grow
// under App as they're ported.

import { Render } from "lvgljs-ui";

import { StoreProvider } from "./stores/StoreProvider";
import { FocusProvider } from "./lvgl/FocusProvider";
import { App } from "./App";

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
(globalThis as unknown as { __rp_mountUI?: () => void }).__rp_mountUI = mountUI;
(globalThis as unknown as { __rp_unmountUI?: () => void }).__rp_unmountUI = unmountUI;

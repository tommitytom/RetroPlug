// Greenfield UI entry. Evaluated once by the host; the mount hooks render the tree after a display is
// attached (and unmount on detach), keeping the QuickJS context alive across a window close/reopen.
//
// The tree is the store-backed app: <StoreProvider> composes the greenfield store graph over the
// __rpcSend backend and multiplexes its change notifications to the hooks; DevProbe is the current
// (minimal) screen. Real screens grow under here in Phase 4.

import { Render } from "lvgljs-ui";

import { StoreProvider } from "./stores/StoreProvider";
import { DevProbe } from "./DevProbe";

function Root() {
  return (
    <StoreProvider>
      <DevProbe />
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

// Throwaway smoke UI for the greenfield render scaffold.
//
// The smallest React tree that mounts on the headless LVGL display and proves the loop end to end: a
// static title (the screenshot assertion anchor) plus one BackendFacade round-trip via realBackend,
// rendered as text. This is NOT the real UI — the UI-over-stores port is Phase 4; this only verifies
// that RenderCore + the BackendFacade RPC bridge render + round-trip + snapshot.

import { View, Text, Render } from "lvgljs-ui";
import { useEffect, useState } from "react";

import { createRealBackend } from "../src/realBackend";

function SmokeUI() {
  const [cfg, setCfg] = useState("(pending)");
  useEffect(() => {
    try {
      const be = createRealBackend();       // targets globalThis[Symbol.for("plugin")].__rpcSend
      setCfg(be.configDir() || "(empty)");  // one synchronous BackendFacade round-trip
    } catch {
      setCfg("(rpc error)");
    }
  }, []);

  return (
    <View
      style={{
        width: 480,
        height: 432,
        "background-color": "#101024",
        display: "flex",
        "flex-direction": "column",
        "align-items": "flex-start",
        "justify-content": "flex-start",
        "padding-left": 8,
        "padding-top": 8,
      }}
    >
      <Text style={{ "text-color": "#ffffff", "font-size": 18 }}>RetroPlug Greenfield UI</Text>
      <Text style={{ "text-color": "#88ff88", "font-size": 12, "padding-top": 6 }}>{`cfg:${cfg}`}</Text>
    </View>
  );
}

// Mount/unmount hooks (mirror PluginUI.tsx): the host evals this bundle once, then calls __rp_mountUI
// after attaching a fresh LVGL display. Keeping the mount out of module top-level lets the QuickJS
// context persist across a display detach/re-attach without re-eval.
function mountUI() { Render.render(<SmokeUI />); }
function unmountUI() { (Render as unknown as { unmount?: () => void }).unmount?.(); }
(globalThis as unknown as { __rp_mountUI?: () => void }).__rp_mountUI = mountUI;
(globalThis as unknown as { __rp_unmountUI?: () => void }).__rp_unmountUI = unmountUI;

// nativeHooks: the whole `__rp_*` seam between the UI bundle and the native host, typed in one place.
//
// The native side binds these as plain globals on the UI's QuickJS context (PluginUI.cpp for the plugin
// editor, sdl/main.cpp for the SDL standalone, the N8 / Launchpad hook files for the hardware seams, and
// RenderCore.cpp for the UI test harness); the UI installs a few of its own the other way (mount, close
// request, the in-app file browser). Every hook is OPTIONAL: which ones exist depends on the host, and a
// missing one means "this host has no such thing", so callers optional-chain and fall back. Each module used
// to re-declare its slice of this as a structural cast at every call site; this is the one declaration.
//
// `native` is globalThis itself, not a snapshot - a hook bound (or a test fake installed) after import is seen.

import type { AppStores, StoreChannel } from "../../src/appStores";
import type { RenderJobStatus } from "./render";
import type { NativeAudioConfig } from "../screens/menu/audioDraft";
import type { MidiConfig } from "../screens/menu/midiDevices";
import type { TransportState } from "../screens/menu/transport";
import type { N8Config } from "../screens/menu/n8Devices";
import type { N8SdStatus } from "../screens/menu/n8SdOps";
import type { LaunchpadConfig, LaunchpadScanStatus } from "../screens/menu/launchpadDevices";

export interface RpHooks {
  // --- installed BY the UI (main.tsx), called by the native editor ---------------------------------------
  __rp_mountUI?: () => void;
  __rp_unmountUI?: () => void;
  /** The window size to open at for the already-loaded project (before first map), or null for the default. */
  __rp_initialWindowSize?: () => { width: number; height: number } | null;

  // --- window (plugin editor + SDL) -------------------------------------------------------------------------
  __rp_setWindowSize?: (width: number, height: number) => void;
  __rp_isWindowSizeControlled?: () => boolean;
  __rp_setWindowTitle?: (title: string) => void;
  __rp_quitWindow?: () => void;
  /** Installed by useCloseGuard; native calls it on a window close. True = veto (the prompt is showing). */
  __rp_onCloseRequested?: () => boolean;
  __rp_openPath?: (path: string) => void;
  /** Set (as a value, not a function) by the SDL host only. */
  __rp_isStandalone?: boolean;

  // --- file browser: native dialog, overridden by the in-app one (useFileBrowser) ---------------------------
  /** `patterns` is space-joined. The pick comes back through __rp_onFileBrowserResult. */
  __rp_openFileBrowser?: (
    title: string,
    patterns: string,
    saving: boolean,
    defaultName: string,
    startDir: string,
    directory: boolean,
  ) => void;
  __rp_onFileBrowserResult?: (path: string | null) => void;

  // --- background rendering (plugin editor) -----------------------------------------------------------------
  __rp_startRender?: (systemId: number, specJson: string) => number;
  __rp_cancelRender?: (jobId: number) => void;
  __rp_dismissRenderJob?: (jobId: number) => void;
  __rp_getRenderJobs?: () => RenderJobStatus[];

  // --- audio device / MIDI devices / transport (SDL standalone) -------------------------------------------
  __rp_getAudioConfig?: () => NativeAudioConfig;
  __rp_setAudioConfig?: (sampleRate: number, blockSize: number, outChannels: number, driver: string, device: string) => void;
  __rp_getMidiConfig?: () => Partial<MidiConfig>;
  __rp_setMidiInput?: (name: string) => void;
  __rp_setMidiOutput?: (name: string) => void;
  __rp_getTransport?: () => Partial<TransportState>;
  __rp_setTransport?: (playing: boolean) => void;
  __rp_setClockBpm?: (bpm: number) => void;

  // --- N8 Pro link + SD jobs (N8Hooks.cpp) ------------------------------------------------------------------
  __rp_getN8Config?: () => Partial<N8Config>;
  __rp_setN8Port?: (name: string) => void;
  __rp_connectN8?: (enabled: boolean) => void;
  __rp_setN8Lookahead?: (ms: number) => void;
  __rp_setN8ExpVol?: (v: number) => void;
  __rp_getN8SdStatus?: () => Partial<N8SdStatus>;
  __rp_n8LoadRom?: (path: string) => void;
  __rp_n8DumpSram?: (path: string) => void;
  __rp_n8RestoreSram?: (path: string) => void;

  // --- Launchpad control surface (LaunchpadHooks.cpp) -------------------------------------------------------
  __rp_getLaunchpadConfig?: () => Partial<LaunchpadConfig>;
  __rp_setLaunchpadPorts?: (input: string, output: string) => void;
  __rp_connectLaunchpad?: (enabled: boolean) => void;
  __rp_setLaunchpadFarewell?: (bytes: number[]) => void;
  __rp_scanLaunchpad?: (probe: number[], windowMs: number) => boolean;
  __rp_getLaunchpadScan?: () => Partial<LaunchpadScanStatus>;
  __rp_setLaunchpadDevice?: (label: string) => void;

  // --- UI test harness only (RenderCore.cpp) ----------------------------------------------------------------
  __rp_tagTestId?: (uid: string, name: string) => void;
}

export const native = globalThis as unknown as RpHooks;

/** What the control-plane bundle publishes on globalThis[Symbol.for("plugin")]: the one store graph the DSP
 *  plays, and the seam the editor registers its change fan-out through. Absent in the UI test harness. */
export interface PluginNamespace {
  stores?: AppStores;
  setUiNotify?: (fn: ((channel: StoreChannel) => void) | null) => void;
}

export function pluginNamespace(): PluginNamespace | undefined {
  return (globalThis as Record<symbol, unknown>)[Symbol.for("plugin")] as PluginNamespace | undefined;
}

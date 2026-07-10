# 07 — Migration: greenfield becomes the sole build

RetroPlug2 ships two plugin builds from one repo today: **greenfield** (the active
reimplementation documented by the rest of `spec/`) and **legacy** (the older build that
still ships only because greenfield hasn't reached full parity). One CMake configure builds
both trees — see [06-build-test.md](06-build-test.md). This document is the switchover plan:
what greenfield still owes before legacy can be deleted, exactly which C++ dies and which
survives, the rename/delete mechanics, and the sharp edges deletion exposes.

This is the **only** doc that inventories legacy internals or remaining work in depth. Other
docs note a gap in a line and point here.

The endpoint: legacy deleted, the `-greenfield` suffix dropped everywhere, greenfield's
plugin identity reverted to the canonical `RetroPlug` strings so existing DAW projects
resolve. Nothing about the runtime architecture changes — greenfield *is* the architecture in
[01-architecture.md](01-architecture.md); this is a removal-and-rename exercise, not a redesign.

---

## Final tasks before deletion

Most of the original §1 gap has since closed — live config apply, plugin state, **NES/GBA Mesen
systems + their TS store wiring**, the **Mesen state-snapshot arming** (§4.1), the **TS default-role
providers** (§4.4), the bindings + gamepad UI, and the confirm/prompt modals are all done, and
`gain`/`reload` are generalized off SameBoy-only. After the scoping decisions below, **four feature/
verification tasks remain** before legacy can be deleted:

1. **VST2 build — DONE (builds).** `vst2` added to the greenfield DPF `TARGETS`; `retroplug-greenfield-vst2.so` builds clean on Linux. Remaining polish (not the build): DAW-load validation + any Windows link fixes the legacy VST2 carried.
2. **Port the LSDj-sync / DAW-timing / audio-quality test matrix to greenfield.** The **headless** portion
   is **DONE** on `pnpm test:greenfield-native`: the sync-mode real-core tests (MidiSyncArduinoboy / MidiMap /
   KeyboardMidi / MidiPassthrough — `test-native/lsdj-*.test.ts`), link-cable sync via **per-system audio**
   (the new `renderAudioPerSystem` RPC — `lsdj-sync-{pattern,negative}.test.ts`), and audio-quality WAV staging
   (`reaper:analyze-{smoke,lsdj-sync}-greenfield`). **Remaining:** the **DAW-timing** leg — the real-Reaper
   metro / drift renders (`reaper:lsdj-*-metro` / `-drift`, a DAW-in-the-loop vehicle: greenfield `.rplg`
   fixtures + the reaper scripts pointed at the greenfield VST3 + `reaper-timing-analyze.py`). Deleting
   `packages/native/test`, `test/ts/**`, and the `reaper:*` scripts (§3) is gated on that leg **plus the
   ArduinoboyMaster serial-out test** (task 4).
3. **Native KeyboardMidi coverage — DONE.** KeyboardMidi (mode 5) is now driven against a **real LSDj core**
   ([test-native/lsdj-keyboardmidi.test.ts](../packages/retroplug-greenfield/test-native/lsdj-keyboardmidi.test.ts)):
   the mock [lsdj-modes.test.ts](../packages/retroplug-greenfield/test/dsp/lsdj-modes.test.ts) asserts the exact
   PS/2 scancodes, and the native test proves they reach a real LSDj and it keeps running (delivery, no crash).
4. **The final 2 LSDj sync modes** (§5): raw **Keyboard** (mode 4 — needs the per-block UI-keys feed) and **ArduinoboyMaster** (mode 7, LSDj→MIDI-out — needs the emulator **serial-OUT fed into the block** + `emitMidiOut` **drained to the host MIDI-out port** + the master-protocol behaviour). ArduinoboyMaster is the priority.

**Explicitly deferred / dropped (NOT blockers for deletion):**
- **Kit-patch UI + sample matcher + resampling** — will be **reworked from scratch on greenfield** after it becomes the main build; not gating deletion (the native `KitCompiler` primitives stay — §2).
- **About panel** — dropped (offered little).
- **LV2** — deferred indefinitely (its out-of-process DSP/UI split doesn't fit RetroPlug).
- **Live memory subscription** (§4.2), **savestate slots**, **sav inspector**, **web/Emscripten** — deferred; none are features the switchover depends on.

Beyond these four, the rest is **mechanical**: finish the DSP-roles C++→TS move + delete the dead role
classes (§5), re-home `retroplug-cli-core` (§3), delete legacy, then the rename + plugin-identity revert (§3).

---

## 1. Feature gap (greenfield vs legacy)

Greenfield is a real, buildable, multi-format plugin (`clap` / `vst3` / `jack` standalone),
with DPF get/setState project persistence, autoload, and a native + UI + store test suite.
The gap below is what legacy still does that greenfield does not yet — each row is a
precondition on deleting the corresponding legacy code.

| Feature | Legacy | Greenfield status |
|---|---|---|
| **Live per-system config apply** | model / highpass / linkGroup / fastBoot / gain / reloadOnRomChange applied live | **Done.** `applySystemSetting` (gain / reloadOnRomChange) + `applyRoleConfig` (sameboy model/highpass/link/fastBoot) run through the command ring ([EngineRpcService.cpp:99](../packages/native-greenfield/src/EngineRpcService.cpp#L99)). `fastBoot` takes effect on the next restart ([Engine.cpp:191](../packages/native-greenfield/src/Engine.cpp#L191)). |
| **Plugin state (get/setState)** | project chunk in `PluginDSP` | **Done.** Base64 `.rplg` via the control plane; autoload via `__rp_loadProjectPath`. See [05-data-persistence.md](05-data-persistence.md). |
| **NES / GBA (Mesen) systems** | fully wired | **Done.** `MesenBackend` builds `MesenNesSystem` / `MesenGbaSystem` and arms their state snapshot (`bootMesen`). The TS store wires them: `defaultCoreFor` routes `nes`/`gba` → `mesen`, `coreRoles` registers the `"mesen"` system-role (NES Region + Remove Sprite Limit knobs), `romProviders` attaches `nes-n8-midi`, and `Engine` gain/reload are generalized (`setGainDb` is a `SystemBase` virtual). NES host-MIDI + the N8 FIFO play. Further NES↔SameBoy polish is product-scoped, not a legacy-parity blocker. |
| **MIDI routing** | routing cycler + native routing roles | **Done.** `midi-routing` is a project-scope feature-role (`src/midiRouting.ts` + `src/dspRoles.ts`); host MIDI is staged per block and fanned to systems; routing-config edits live-re-push via `setRoleConfig` → `syncDspFromStore`. |
| **About panel** | `packages/ui/src/menu/AboutPanel.tsx` + menu item | **Dropped** — not a deletion blocker (it offered little). |
| **Bindings / keymap editor UI** | full keyboard+gamepad capture submenu | **Built.** Settings → Keyboard Bindings + Gamepad Bindings ([menuDefs.ts](../packages/retroplug-greenfield/ui/screens/menu/menuDefs.ts) `bindingsChildren` per channel): a profile switcher, one capture row per GB button, three **app-action** capture rows (Open Menu / Cycle Instances / Cycle Instances (Back) → the `keyboardActions`/`gamepadActions` binding sections), a channel reset (GB buttons + actions), and New / Rename / Delete, write-through via `bindingsStore`. |
| **LSDj mode selection (live apply)** | 8-mode cycler + `setLsdjSyncConfig` RPC | **Largely done.** An "LSDj" instance submenu (Mode + Tempo Divisor cyclers) drives `setRoleConfig`, which live-applies via the feature-role re-push path (no dedicated RPC). Behaviours built for MidiSync + MidiSyncArduinoboy + MidiMap + KeyboardMidi + MidiPassthrough; raw Keyboard + ArduinoboyMaster remain (§5). |
| **Text-prompt / confirm-modal flows** | rename prompts, remove-confirm, incompatible-project / unsaved-changes / relink modals | **Largely built.** The `capture`/`prompt` menu kinds exist (bindings capture rows + New/Rename/delete-confirm prompts), and the unsaved-changes / discard / notice / relink modal overlays are built ([App.tsx](../packages/retroplug-greenfield/ui/App.tsx) `useCloseGuard` / `useProjectModals`); relink uses the OS "Locate on Disk" dialog. |
| **Kit-patch UI + sample matcher + resampling** | `KitEditor.tsx`, native `KitCompiler` (r8brain + enkiTS) | **Deferred — reworked from scratch on greenfield post-switchover; NOT a deletion blocker.** Kit *compilation* stays native (§2); the greenfield UI-thread behaviour + sample matcher + resampling are a fresh build once greenfield is the main build. |
| **Savestate slots** | — (never built in legacy either) | **Deferred.** Never in legacy → not a parity blocker. The state-snapshot triple exists for save/duplicate; a user-facing multi-slot feature does not. |
| **Sav inspector** | — (design only) | **Deferred.** Design-only in legacy → not a blocker. The LSDj sav codec exists; RPC exposure + a React view do not. |
| **Live memory subscription** | `subscribeMemory` → `enableMemorySnapshot` streaming | **Deferred (§4.2).** Greenfield has one-shot frame/state/SRAM reads through the read door but no live region pump; nothing arms `enableMemorySnapshot`. Not a switchover blocker unless a feature needs it. |
| **Gamepad (SDL) input** | `GamepadManager.{hpp,cpp}` | **Live input built (default bindings).** The legacy `GamepadManager` is compiled into greenfield too and polled from `PluginGreenfieldUI::uiIdle` (emitting the `gamepad-*` JS bus); `ui/input/useGamepadInput.ts` maps via the existing `bindings.gamepad` and routes to the focused core, twin of `useGameInput`. An in-app Gamepad Bindings editor (rebind by pressing a button *or* flicking a stick) is built, and the **left stick drives the d-pad** via half-axis tokens + hysteresis (`keyCodes.ts` `axisToken`, seeded in the default map, consumed in `useGamepadInput`). The **gamepad also drives the menu** ([Menu.tsx](../packages/retroplug-greenfield/ui/screens/menu/Menu.tsx)): d-pad / left stick move + cycle, A selects, B backs out (fixed SDL names via `keyCodes.ts` `menuNavForButton` / `menuNavForAxisToken`). Opening the menu and cycling the focused instance are **rebindable app actions** (`buildKeyToAction` / `buildGamepadToAction`, dispatched in [App.tsx](../packages/retroplug-greenfield/ui/App.tsx)): Open Menu defaults to Esc / `leftshoulder`, Cycle Instances to Tab / `rightshoulder` (Back unbound), so a gamepad-only user is never stranded. |
| **Plugin formats VST2 + LV2** | JACK / CLAP / VST2 / VST3 / LV2 | **VST2 builds** — `vst2` added to the DPF `TARGETS` (`retroplug-greenfield-vst2.so`, Linux); DAW-load / Windows validation is the only remaining polish. **LV2 = deferred indefinitely** — its out-of-process DSP/UI split doesn't fit RetroPlug. Greenfield now builds `clap vst3 vst2 jack`. |
| **Web / Emscripten port** | — (design only) | **Deferred** (design-only in legacy). |

---

## 2. Shared-vs-legacy C++ map

The shared-core `.cpp` files are compiled **twice** today: once into `retroplug-cli-core`
(the static lib greenfield links) and once inline inside the legacy `dpf_add_plugin(retroplug …)`.
**Deleting the legacy plugin deletes the legacy *compile* of the shared core, not the sources** —
the sources survive through `retroplug-cli-core`.

### Permanent (the shared core — survives legacy deletion)

Greenfield `#include`s and links these from [packages/native/src](../packages/native/src); all are
part of the **shared core** described in [01-architecture.md](01-architecture.md).

| Area | Files | Notes |
|---|---|---|
| System layer | `system/SystemBase`, `system/sameboy/SameBoySystem` + `SameBoyConfig` + `LinkGroup` + `RomSniffer`, `system/mesen/Mesen{Nes,Gba}System` + `MesenNesDebugSession` + `MesenVideoDevice`, `system/BlockRunner`, `system/RomFormat` | greenfield holds `unique_ptr<SystemBase>`, drives the per-block triad through `BlockRunner`, rebuilds link groups on every adopt/replace |
| Project | `project/Project` | greenfield's `Engine` owns a `Project` as its live-systems container and uses its **runtime** methods only (`adoptSystem` / `removeSystemAndRelease` / `swapSystem` / `findSystem` / `systems` / `rebuildLinkGroups`) — never the persistence methods `loadFromConfig` / `snapshotConfig`, which stay legacy callers |
| LSDj sav codec + model | `lsdj/codec/*`, `lsdj/SavSerialization`, `lsdj/model/*` | exposed as a stateless host service ([HostRpcService.cpp](../packages/native-greenfield/src/HostRpcService.cpp)); see [05-data-persistence.md](05-data-persistence.md) |
| Kit compilation | `lsdj/KitCompiler`, `KitUtil`, `SampleCache`, `Effects` | linked; the eventual kit-patch UI-thread behaviour compiles over these primitives (the C++ `LsdjKitPatchRole` is not used — see below) |
| Transport primitives | `transport/SpscRing`, `transport/FrameBufferTriple`, `transport/MemorySnapshotTriple` | the lock-free ring + tear-free triples greenfield's command ring / release ring / read door are built on |
| Util | `util/MinizZip`, `EmbeddedRoms` | `zip`/`unzip` for `.rplg`; embedded mGB bytes |

`GamepadManager.{hpp,cpp}` is now **also** compiled into greenfield (its plugin `FILES_UI`; SDL2 linked onto
`retroplug-greenfield-ui`) — see the §1 gamepad row — so it survives legacy deletion and re-homes into the
greenfield tree alongside `retroplug-cli-core` (§3), rather than dying with the legacy plugin.

The C++ feature-role classes — `LsdjSyncRole`, `ArduinoboyMaster`, `MgbPassthroughRole`,
`LsdjKitPatchRole`, `mesen/roles/NesN8MidiRole` — are compiled into `retroplug-cli-core` and
therefore *linked* into greenfield, but they are **runtime-dead** there: greenfield builds bare
cores (`setSniffDefaultRoles(false)`) and never populates `config_.roles`. Their behaviour is
reimplemented in the TS DSP kernel ([04-roles-dsp-kernel.md](04-roles-dsp-kernel.md)). They are
deleted from the tree once the TS path drives every host that needs them (§5).

### Dies with the legacy build

| File / dir | What it is |
|---|---|
| `PluginDSP.cpp` | legacy DPF audio-thread run loop — drains `CommandQueue`, applies via `CommandApply`, publishes snapshots, orchestrates `loadFromConfig` / `projectConfigToZip` |
| `PluginRpcService.{hpp,cpp}` + `PluginRpcRegistration.hpp` | the whole legacy UI↔DSP rpcpp surface |
| `PluginJsBridge.{hpp,cpp}` | wraps `PluginRpcService` in a `TypedRpcServer` for the legacy JS |
| `PluginUI.cpp`, `PluginShared.hpp`, `RpcSchemaDump.cpp`, `Version.hpp` | legacy editor window + plugin glue |
| `transport/CommandApply.{hpp,cpp}`, `transport/CommandQueue.hpp`, `transport/EventQueue.hpp` | the legacy command/return machinery — greenfield reimplements this as the command ring + release ring ([EngineInvoker.hpp](../packages/native-greenfield/src/EngineInvoker.hpp)) |
| `config/UserConfig*`, `RecentFiles*`, `config/SramMirror.hpp`, `config/SchemaVersions.hpp` | legacy config persistence + efsw watch. Greenfield reimplements the config dir in `HostRpcService` to stay free of `UserConfig.hpp` / efsw, and never validates a C++ schema version — persistence + versioning are TS-owned ([05-data-persistence.md](05-data-persistence.md)) |
| `project/ProjectSerialization.hpp`, `ProjectBinaries.hpp`, `ProjectPaths.hpp`, `ProjectMissingFiles.hpp`, the `ProjectConfig` persistence path | `.rplg` / project.json persistence — TS-owned in greenfield |
| `packages/retroplug/` (TS) | generated legacy RPC client + project/schema TS |
| `packages/ui/` (TS) | legacy LVGL/React menu + tiles |
| `packages/cli/` + the CLI-facing pieces of `packages/native/cli/` (`retroplug-cli`, `HarnessRpcService`, `TestHarness`, `harness-schema-dump`) | legacy CLI + test harness |

`Project.cpp` itself is **shared** — only its persistence *callers* are legacy. The class keeps
`loadFromConfig` / `snapshotConfig`; greenfield just doesn't call them.

---

## 3. Rename / delete checklist

This is an inventory of the mechanical moves, not a sequenced plan. See
[06-build-test.md](06-build-test.md) for the current target/script topology.

### Delete (legacy)

- **Packages:** `packages/retroplug/`, `packages/ui/`, `packages/cli/`, `packages/native/test/`
  (all Catch2 suites + `retroplug-ui-test`, unless ported).
- **Legacy C++ in `packages/native/src`:** `PluginDSP.cpp`, `PluginUI.cpp`, `PluginJsBridge.cpp`,
  `PluginRpcService.cpp`, `RpcSchemaDump.cpp`, the `transport/Command*`
  + `EventQueue` + `config/*` serialization files listed in §2. Verify each is not reachable
  through `retroplug-cli-core` first. (**Not** `GamepadManager.cpp` — greenfield now compiles it; re-home it
  into the greenfield tree instead of deleting.)
- **Root `CMakeLists.txt` blocks:** `rpc-schema-dump`, `ui-regenerate`, the legacy
  `sav-regenerate` / `cli-regenerate` / `cli-bundle-regenerate` portions, the
  `dpf_add_plugin(retroplug …)` definition + its Windows link fixes, and
  `add_subdirectory(packages/native/test …)`.
- **CMake targets:** `retroplug`, `retroplug-{clap,vst3,vst2,lv2,lv2-ui,jack,au,ui,dsp}`,
  `retroplug-cli`, `harness-schema-dump`, `rpc-schema-dump`, `ui-regenerate`, and the Catch2
  test targets. Keep `sav-schema-dump` / `sav-regenerate` if greenfield still consumes the sav
  schema; **keep `retroplug-cli-core`** (§4).
- **pnpm scripts (root `package.json`):** `test`, `test:cli`, `test:ui`, `smoke`, `screenshot`,
  `validate`, and every non-greenfield `reaper:*`.
- **Tools:** `tools/run-standalone.sh`, `scripts/run-ts-tests.js`, `test/ts/**`, `test/harness/**`,
  legacy `examples/reaper/*.rpp`.

### Rename (drop the `greenfield` suffix)

| From | To |
|---|---|
| `retroplug-greenfield` (+ `-clap`/`-vst3`/`-jack`/`-ui`/`-dsp`) | `retroplug` (+ variants) |
| `retroplug-greenfield-backend` | `retroplug-backend` |
| `retroplug-greenfield-cp-bundle` / `-ui-bundle` | `retroplug-cp-bundle` / `-ui-bundle` |
| `retroplug-greenfield-ui-test` | `retroplug-ui-test` |
| `native-greenfield-host` | `retroplug-host` (or `native-host`) |
| `packages/native-greenfield/` | `packages/native/` (after the old one is gutted — **name collision** with the current `@retroplug/native` manifest) |
| `packages/retroplug-greenfield/` | `packages/retroplug/` (**collides** with the legacy `@retroplug/retroplug` package name) |
| pnpm `test:greenfield*` / `screenshot:greenfield` / `validate:greenfield` / `reaper:mgb-smoke-greenfield*` | drop the infix; drop the `RETROPLUG_{CLAP,VST3}_NAME=retroplug-greenfield` env overrides |
| `tools/run-standalone-greenfield.sh`, `run-greenfield-sanitizer.sh`, `author-greenfield-rplg.js`, `build-greenfield-controlplane.js`, `reaper-mgb-greenfield-author.lua` | drop the `greenfield` infix / merge into their legacy-named counterparts |
| bundle symbol prefixes `gfcp_` / `gfui_` | `cp_` / `ui_` (cosmetic) |

**Plugin identity** ([DistrhoPluginInfo.h](../packages/native-greenfield/plugin/DistrhoPluginInfo.h)):
today greenfield is deliberately distinct — `DISTRHO_PLUGIN_NAME "RetroPlug Greenfield"`, URI
`urn:distrho:retroplug-greenfield`, CLAP id `studio.kx.distrho.retroplug-greenfield` — precisely so
the two plugins coexist in a DAW. On switchover these revert to the canonical `RetroPlug` /
`urn:distrho:retroplug` / `studio.kx.distrho.retroplug` strings so existing DAW projects that
reference the legacy plugin resolve. This is an on-disk identity change; sequence it after legacy
is removed.

### Re-home `retroplug-cli-core`

`retroplug-cli-core` ([CMakeLists.txt:23](../packages/native/cli/CMakeLists.txt#L23)) is the single
load-bearing shared C++ lib — greenfield's whole emulator/Project surface flows through it
([CMakeLists.txt:41](../packages/native-greenfield/CMakeLists.txt#L41), made PIC at
[:54](../packages/native-greenfield/CMakeLists.txt#L54)). It currently lives inside a target that also
compiles the CLI-only `HarnessRpcService`, so greenfield transitively links a CLI surface it never
uses. Any "delete `packages/native/cli`" step must first **extract the shared core into a
neutrally-named lib** (e.g. `retroplug-core`) in the greenfield tree, preserving the PIC setting, or
greenfield stops building. This is the one delete step with a hard ordering constraint.

### tsconfig drift

`tsconfig.base.json` still maps `react` / `lvgljs-ui` / `@rpcpp/*` to a `../dpf.js` **sibling**
path, but the live location is the `deps/dpf.js` submodule. Greenfield's own
`packages/retroplug-greenfield/tsconfig.json` does not extend base (`"types": []`, relies on
esbuild resolution), so this does not affect the greenfield build — but it is a stale mapping to
fix on the way through.

---

## 4. Gaps greenfield must own before it stands alone

These are arming steps and responsibilities a from-scratch greenfield host must cover. They are
**not** masked by legacy today — greenfield builds and arms its own cores in its own `Engine` /
`Project`, independent of the legacy `PluginDSP` — so #1 and #2 are *present* gaps, not things the
deletion introduces. They are listed here because they're the loose ends that must close for
greenfield to be the sole build.

1. **`enableStateSnapshot()` for Mesen — DONE.** (Was SameBoy-only.) `MesenBackend`'s `bootMesen`
   now calls `enableStateSnapshot()` for both NES and GBA (mirroring
   [SameBoyBackend.cpp:37](../packages/native-greenfield/src/SameBoyBackend.cpp#L37)), so a Mesen
   system's read-door slot ([EngineRpcService.cpp:80](../packages/native-greenfield/src/EngineRpcService.cpp#L80))
   has live `stateRegions()` — savestate-based `readSram` / duplicate work on Mesen cores. Closed
   during the Mesen-parity FOUNDATION work.

2. **`enableMemorySnapshot(type)` has no greenfield equivalent.** Legacy arms live region streaming on a
   `SubscribeMemory` command. Greenfield has no equivalent subscription path (§1, "live memory
   subscription"). If the "watch RAM" / HD-player subscription seam is ever built, this arming must
   be rebuilt from scratch — it is not carried over.

3. **The read-door double-copy is deliberate, and its collapse is a follow-up, not a bug.** The
   read door copies from each core's own tear-free triple because the shared `SystemBase` can't yet
   publish straight into the registry ([SnapshotRegistry.hpp:23](../packages/native-greenfield/src/SnapshotRegistry.hpp#L23)).
   Deleting legacy is the trigger that lets `SystemBase` "become greenfield-only" and collapse the
   second copy. Until someone does that refactor both copies live — a documented redundancy, not a
   regression.

4. **The sniffer's default-role suggestion moved to TS — DONE.** Legacy relied on `RomSniffer` to
   auto-attach LSDj-sync / mGB / N8-MIDI role config on a fresh ROM; greenfield turns the C++ sniffer
   off ([SameBoyBackend.cpp:35](../packages/native-greenfield/src/SameBoyBackend.cpp#L35)) and supplies
   the equivalent in TS: `registerRomProviders`
   ([romProviders.ts](../packages/retroplug-greenfield/src/romProviders.ts)) attaches `mgb` (title /
   embedded), `lsdj-sync` (LSDj title), and `nes-n8-midi` (platform). Kit-patch default attachment
   follows if/when that feature is (re)built.

---

## 5. In-flight workstream: DSP roles C++ → TS

The largest open workstream is moving the emulator feature-roles off their legacy C++ classes onto
the TS DSP kernel, per "Native owns bytes and cores; TS owns meaning." The role model and kernel are
described in [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md); this is the remaining-work summary.

**Built:** the pure-TS DSP-thread role kernel — `DspKernel.processBlock`, the per-system context
with its byte-sinks (`pushSerialIn` / `emitMidiOut` / `pressButton`) + `eachTick`, `RoleType` with
`scope` + a typed `dsp` behaviour, and the first three roles (`mgb`, `lsdj-sync`, `midi-routing`) as
plain TS, unit-tested. The native runner that hosts it (`DspRuntime`, the bare DSP QuickJS context)
and the store→kernel projection are wired.

**Open:**

- **DSP-role behaviours (pure TS):** LSDj sync authors MidiSync, MidiSyncArduinoboy, MidiMap,
  KeyboardMidi, MidiPassthrough (including the `0xFA`/`0xFC` transport-start/stop bytes), backed by the
  kernel's `ctx.state` per-system scratch bag ([dspRoles.ts](../packages/retroplug-greenfield/src/dspRoles.ts)).
  Missing: raw **Keyboard** mode (needs the `keys` feed, below) and **ArduinoboyMaster** MI.OUT (the
  `emitMidiOut` host-out direction, which also needs the emulator serial-out fed *into* the block).
  These two are **final task 4** (ArduinoboyMaster is the priority); and KeyboardMidi, though built +
  unit-tested ([test/dsp/lsdj-modes.test.ts](../packages/retroplug-greenfield/test/dsp/lsdj-modes.test.ts)),
  still needs **real-core coverage** (final task 3) — no test drives it against an actual LSDj ROM yet.
- **Legacy C++ role removal:** delete `LsdjSyncRole` / `MgbPassthroughRole` (and the rest of the
  role classes) once the TS path drives every host — blocked until the shared CLI/plugin consumers
  are gone.
- **App / store wiring:** host MIDI-in + transport/tempo/ppq are fed per block, and feature-role config
  edits (LSDj mode, routing mode) already re-push to the running behaviour via `setRoleConfig` →
  `markDirty` → `syncDspFromStore`. Still unfed: UI-mapped buttons and raw keys (blocks raw Keyboard
  mode).
- **NES/GBA store arms — DONE.** `MesenBackend` + the TS store select and wire NES/GBA:
  `defaultCoreFor` → `mesen`, the `coreRoles` `"mesen"` system-role (Region + sprite-limit knobs), and
  `romProviders`' `nes-n8-midi` attachment.
- **UI-thread behaviours + extensions:** the UI-thread behaviour kind (kit-patch first — it compiles
  on the UI thread and writes patched memory regions into the core, needing a control-plane
  memory-write path that does not exist yet, **not** a DSP-kernel sink), the `RoleType.ui`
  render-descriptor seam, and the third-party extension model.

> Caution: the in-flight todo's own "no-op stub" note on `applySystemSetting` / `applyRoleConfig` is
> stale — those are implemented (§1). Its framing of "how the audio thread runs TS behaviours" as an
> open decision is also resolved and built (the `DspRuntime` runner). Treat the *behaviour authoring*
> and *legacy removal* buckets as the live work.

---

## 6. Code-comment cleanup backlog (note, don't fix)

Greenfield carries in-code deferral markers that should be swept once the corresponding feature
lands. This is a **follow-up bookkeeping task, not work to do during the migration** — listed here
so the intent isn't lost. (These are the actionable IOUs; the many purely-descriptive "ported from
legacy" provenance comments are context, not action items.)

| Location | Marker |
|---|---|
| [DspRuntime.cpp:15](../packages/native-greenfield/src/DspRuntime.cpp#L15) | GB serial pump is a plain FIFO; intra-block frame not yet modelled |
| [Engine.cpp](../packages/native-greenfield/src/Engine.cpp) | **Resolved** — gain/reload generalized: `setGainDb` is a `SystemBase` virtual applied before the SameBoy cast (was SameBoy-only). |
| [Engine.cpp:191](../packages/native-greenfield/src/Engine.cpp#L191) | `fastBoot` deferred to the next restart |
| [PluginGreenfieldDSP.cpp:135](../packages/native-greenfield/plugin/PluginGreenfieldDSP.cpp#L135) | host MIDI short messages only; SysEx deferred |
| [systemRoles.ts:13](../packages/retroplug-greenfield/src/systemRoles.ts#L13) / [:58](../packages/retroplug-greenfield/src/systemRoles.ts#L58) | kit-patch stays a deferred `ui` UI-thread behaviour |
| [backend.ts:109](../packages/retroplug-greenfield/src/backend.ts#L109) | feature-role config never reaches native — the deferred TS-script future |
| [systemsStore.ts:328](../packages/retroplug-greenfield/src/systemsStore.ts#L328) | feature behaviour is the deferred script future |
| [menuDefs.ts](../packages/retroplug-greenfield/ui/screens/menu/menuDefs.ts) (About panel marker) | Deferred About panel only — LSDj mode, live gamepad input, gamepad menu navigation, and the keyboard + gamepad rebinding editors (with analog-stick-as-dpad) are all now built |

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug-greenfield/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

---

## Key files

- [packages/native-greenfield/CMakeLists.txt](../packages/native-greenfield/CMakeLists.txt) — greenfield targets + the `retroplug-cli-core` link
- [packages/native/cli/CMakeLists.txt](../packages/native/cli/CMakeLists.txt) — `retroplug-cli-core` (the shared lib to re-home)
- [packages/native-greenfield/src/SameBoyBackend.cpp](../packages/native-greenfield/src/SameBoyBackend.cpp) / [MesenBackend.cpp](../packages/native-greenfield/src/MesenBackend.cpp) — snapshot arming asymmetry
- [packages/native-greenfield/src/SnapshotRegistry.hpp](../packages/native-greenfield/src/SnapshotRegistry.hpp) — the read-door claim contract + double-copy collapse note
- [packages/native-greenfield/plugin/DistrhoPluginInfo.h](../packages/native-greenfield/plugin/DistrhoPluginInfo.h) — the distinct plugin identity to revert
- [packages/retroplug-greenfield/src/dspRoles.ts](../packages/retroplug-greenfield/src/dspRoles.ts) — the shipped roles + the sync-mode gap

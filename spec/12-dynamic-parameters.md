# Dynamic parameters (planned, not built)

**Status: design only. No code exists yet.** This doc plans per-ROM DAW parameters: when mGB or
BlipToaster is loaded, the host should show named automation lanes ("PU1 Duty", "Noise Env") that map
to that ROM's MIDI CCs, instead of one generic list. It spans two repos: the DPF fork
(`deps/dpf.js/deps/dpf`, `git@github.com:tommitytom/DPF.git`) and RetroPlug's plugin + TS layer.

Scope is **CLAP and VST3**. LV2, VST2, AU, JACK and the standalone keep today's behaviour unchanged.

## 1. Why this does not work today

Four facts, all in DPF:

1. **Count is frozen at construction.** [DistrhoPlugin.cpp:43-59](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPlugin.cpp#L43-L59)
   does `new Parameter[parameterCount]` once. RetroPlug passes `1`
   ([PluginDSP.cpp:79](../packages/native/plugin/PluginDSP.cpp#L79)).
2. **`initParameter` is called exactly once per index**, in the `PluginExporter` constructor
   ([DistrhoPluginInternal.hpp:362](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginInternal.hpp#L362)).
   There is no re-init path.
3. **Nothing can mutate a `Parameter` afterwards.** `pData` is private with only `PluginExporter` as
   friend ([DistrhoPlugin.hpp:423-424](../deps/dpf.js/deps/dpf/distrho/DistrhoPlugin.hpp#L423-L424)),
   and the `PluginExporter::getParameter*` accessors are not virtual.
4. **No backend ever signals a parameter-list change.** VST3 calls `restart_component` with only
   `V3_RESTART_PARAM_VALUES_CHANGED` / `V3_RESTART_LATENCY_CHANGED`
   ([DistrhoPluginVST3.cpp:1150](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp#L1150),
   [:2100](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp#L2100)), never
   `V3_RESTART_PARAM_TITLES_CHANGED`. CLAP sends only
   `CLAP_PARAM_RESCAN_VALUES|CLAP_PARAM_RESCAN_TEXT`
   ([DistrhoPluginCLAP.cpp:1750](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginCLAP.cpp#L1750)).

The good news, and what makes this a small feature: **both backends already read the descriptor live
on every host query.** CLAP `getParameterInfo`
([DistrhoPluginCLAP.cpp:1145](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginCLAP.cpp#L1145)) and
VST3 `getParameterInfo`
([DistrhoPluginVST3.cpp:1781](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp#L1781)) both
call `fPlugin.getParameterName(index)` at query time. Only the mutation path and the invalidation
signal are missing.

## 2. What the formats actually allow

This constrains the design more than anything else.

| Change | CLAP | VST3 |
|---|---|---|
| name / short name / unit | `CLAP_PARAM_RESCAN_INFO`, live, plugin stays active | `V3_RESTART_PARAM_TITLES_CHANGED`, live |
| hidden flag | `CLAP_PARAM_IS_HIDDEN` + `RESCAN_INFO`, live | `V3_PARAM_IS_HIDDEN` + titles-changed, live |
| value display text | `CLAP_PARAM_RESCAN_TEXT`, live | re-read via `getParameterStringForValue`, live |
| min / max / stepped | `CLAP_PARAM_RESCAN_ALL`, **requires deactivation** | invisible: VST3 is normalised 0..1, DPF converts |
| add / remove a parameter | `CLAP_PARAM_RESCAN_ALL`, **requires deactivation** | needs `kReloadComponent`; hosts handle it badly, orphans automation |

Sources: [clap/ext/params.h:253-289](../deps/dpf.js/deps/dpf/distrho/src/clap/ext/params.h#L253-L289)
(`RESCAN_INFO` explicitly covers name / module / is_hidden and "takes effect immediately";
`RESCAN_ALL` "can only be used while the plugin is deactivated") and
[travesty/edit_controller.h:73-79](../deps/dpf.js/deps/dpf/distrho/src/travesty/edit_controller.h#L73-L79).

Note also that DPF's VST3 backend **already** exposes `130 * 16` hidden MIDI CC parameters via
`IMidiMapping` ([DistrhoPluginVST.hpp:72-73](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST.hpp#L72-L73)),
so raw CC automation already works in VST3 hosts. This feature is about **naming and curation**, not
about enabling CC control.

## 3. Design

### 3.1 The contract: fixed pool, mutable descriptors

The plugin declares a **fixed maximum pool** of parameters at construction and never changes the
count, the order, the symbols, the ranges or the hints. What changes per ROM is the **descriptor**:
`name`, `shortName`, `unit`, `description`, and a hidden flag.

Reasons, in order of weight:

- Both formats can do descriptor changes **live**; neither can change the count without deactivating
  (CLAP) or reloading the component (VST3).
- **DPF's state save/restore is keyed by parameter symbol** in both backends
  ([CLAP:1716](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginCLAP.cpp#L1716),
  [VST3:1104](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp#L1104)). Stable symbols are
  what make a saved DAW project round-trip. CLAP additionally puts the symbol in `info->module`, and
  `clap_id` is the parameter index, so both must stay put.
- Unused slots being *hidden* rather than *absent* is the behaviour the feature actually wants.

So: a slot that is unused for the current ROM is hidden and named generically; a slot in use gets the
ROM's name for that CC.

### 3.2 Re-declare and diff

Rather than adding a pile of per-field setters, `initParameter` stays the single source of truth. The
plugin calls one method; DPF re-runs `initParameter` and works out what changed.

```
Plugin::requestParameterInfoUpdate()      // plugin -> DPF, any thread
  -> backend defers to its main thread
  -> PluginExporter::reinitParameters()   // re-runs initParameter into a scratch Parameter,
                                          // diffs against live, applies the safe fields,
                                          // returns a change mask
  -> backend maps the mask to the format's notification
```

`reinitParameters()` classifies each index:

- **Descriptor change** (`name` / `shortName` / `unit` / `description` / the `kParameterIsHidden`
  bit): applied immediately, contributes `kParameterInfoChanged`.
- **Enum values / value formatting**: applied, contributes `kParameterTextChanged`.
- **Structural change** (`symbol`, `designation`, `groupId`, `ranges`, or any hint other than
  `kParameterIsHidden`): **not applied**. Logged via `d_stderr2` and skipped. Structural changes are
  a contract violation for this feature, and silently applying them would desync the audio thread
  (see 3.3) and reinterpret recorded automation.

Rejecting rather than supporting structural changes is deliberate: supporting them means a
deactivate/reactivate cycle on CLAP and a component reload on VST3, which is a much larger feature
with a much worse user experience. If it is ever needed, it is a follow-up, not part of this.

### 3.3 Threading and RT safety

The audio thread reads `hints` and `ranges` during parameter-change handling
([DistrhoPluginVST3.cpp:722-743](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp#L722-L743)).
It never reads the name strings. That split drives two rules:

1. **`hints` and `ranges` are immutable for the life of the instance.** This is what makes the
   structural-change rejection in 3.2 load-bearing rather than pedantic.
2. **The hidden flag does not live in `hints`.** `kParameterIsHidden` inside `hints` stays the
   static, init-time declaration. The dynamic hidden state goes in a separate
   `std::atomic<bool>` side array in `PrivateData`, read only by the two backends' `getParameterInfo`.
   Toggling a bit inside `hints` would be a data race on a word the audio thread reads, and this repo
   gates on ThreadSanitizer ([tools/run-sanitizer.sh](../tools/run-sanitizer.sh)) - a "benign" race
   is still a failed run.

String mutation happens on the main thread only, and both formats specify their `get_info` query as
main-thread, so there is no reader to race with.

CLAP already has the deferral machinery: `fHostExtensions.threadCheck->is_main_thread()`,
`fHost->request_callback()` and `clap_plugin_on_main_thread` -> `PluginCLAP::onMainThread()`. The
latency path at [DistrhoPluginCLAP.cpp:1408-1450](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginCLAP.cpp#L1408-L1450)
is the exact pattern to copy: do it now if already on the main thread, otherwise set a flag and
request a callback.

VST3 has no equivalent hop. `restart_component` is called from the host's own main-thread calls
today. The request will be latched into an atomic flag and flushed from the next main-thread entry
point (`setState`, `getState`, `set_component_handler`, or the edit controller's parameter queries),
which is sufficient for RetroPlug because the trigger is always a `setState` or a UI-thread project
change.

### 3.4 Separate-controller mode

`DPF_VST3_USES_SEPARATE_CONTROLLER` is 1 only when `DISTRHO_PLUGIN_HAS_UI == 1 &&
DISTRHO_PLUGIN_WANT_DIRECT_ACCESS == 0`
([DistrhoPluginVST.hpp:30-34](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST.hpp#L30-L34)).
RetroPlug sets `DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1`
([DistrhoPluginInfo.h:50](../packages/native/plugin/DistrhoPluginInfo.h#L50)), so it is 0 and there
is a single `PluginExporter` answering both the component and the controller.

In separate-controller mode the controller is a **different plugin instance** that never sees the ROM
change, so its names would not update. That mode is **out of scope**: guard the feature with
`#if DPF_VST3_USES_SEPARATE_CONTROLLER` + `#error`, so anyone enabling both gets a compile failure
instead of a silent half-feature.

## 4. DPF fork changes

The fork already carries feature commits (`feat(dnd)`, `feat(filebrowser)`), so this is the
established place for it. Everything is gated behind a new opt-in macro defaulting to 0, so every
other DPF plugin is bit-identical.

| File | Change |
|---|---|
| [DistrhoPluginChecks.h](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginChecks.h) | `#define DISTRHO_PLUGIN_WANT_DYNAMIC_PARAMETERS 0` default, next to the existing `DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST` at [:76-78](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginChecks.h#L76-L78). |
| [DistrhoPlugin.hpp](../deps/dpf.js/deps/dpf/distrho/DistrhoPlugin.hpp) | `bool canUpdateParameterInfo() const noexcept;` + `bool requestParameterInfoUpdate() noexcept;`, modelled on `canRequestParameterValueChanges` / `requestParameterValueChange` at [:153](../deps/dpf.js/deps/dpf/distrho/DistrhoPlugin.hpp#L153) and [:162](../deps/dpf.js/deps/dpf/distrho/DistrhoPlugin.hpp#L162). Doc comment states the fixed-pool contract. |
| [DistrhoPlugin.cpp](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPlugin.cpp) | Two-line forwarders into `pData`. |
| [DistrhoPluginInternal.hpp](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginInternal.hpp) | `typedef bool (*parameterInfoChangedFunc)(void* ptr);` alongside [:48-50](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginInternal.hpp#L48-L50); the callback field + `std::atomic<bool>` hidden side array in `PrivateData`; a **defaulted trailing** 5th `PluginExporter` ctor param at [:331-333](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginInternal.hpp#L331-L333); and `reinitParameters()` returning the change mask. |
| [DistrhoPluginCLAP.cpp](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginCLAP.cpp) | Thunk + main-thread deferral copying the latency pattern; `onMainThread()` flush; `rescan(CLAP_PARAM_RESCAN_INFO\|CLAP_PARAM_RESCAN_TEXT)`; map the hidden state to `CLAP_PARAM_IS_HIDDEN` in `getParameterInfo`. |
| [DistrhoPluginVST3.cpp](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginVST3.cpp) | Thunk + latched flag flushed on the next main-thread entry; `restart_component(V3_RESTART_PARAM_TITLES_CHANGED)`; map the hidden state to `V3_PARAM_IS_HIDDEN` in `get_parameter_info`; the separate-controller `#error` guard. |
| Other backends | **No edit.** The defaulted ctor param means LV2 / VST2 / AU / JACK / Carla / the two static exporters compile untouched, which keeps upstream merges clean. |

`kParameterIsHidden` is currently honoured **only** by the LV2 ttl exporter
([DistrhoPluginLV2export.cpp:964](../deps/dpf.js/deps/dpf/distrho/src/DistrhoPluginLV2export.cpp#L964)).
Mapping it in CLAP and VST3 is a small standalone improvement and can land as its own commit before
the dynamic part, since it is useful with or without this feature.

LV2 is not fixable and should be documented as such: its control ports are baked into the static
`.ttl` at build time. LV2 builds get the generic pool names.

## 5. RetroPlug changes

Per the spec thesis, native owns bytes and TypeScript owns meaning: the CC map is meaning.

**The pool.** `PluginDSP` declares `1 + kSystemCount * kCcSlotsPerSystem` parameters (proposal:
`1 + 4 * 16 = 65`, one constant). Symbols are `gain`, then `sys1_cc1` .. `sys4_cc16`. Every slot is
`0..127`, `kParameterIsInteger | kParameterIsAutomatable`. Default names are `"1: CC 1"` etc, hidden
until claimed.

**The map.** Each role that wants named parameters declares a static CC table next to its role
definition ([systemRoles.ts](../packages/retroplug/src/systemRoles.ts) /
[coreRoles.ts](../packages/retroplug/src/coreRoles.ts)): a list of `{ slot, cc, channel, name,
shortName }`. mGB and BlipToaster each get one. The control plane projects the active systems'
tables into a flat list for the pool.

**The seam.** `PluginDSP` polls a new `__rp_parameterMapJson` global exactly where it already polls
`__rp_syncLatencyMs` in `updateLatency()`
([PluginDSP.cpp:347-351](../packages/native/plugin/PluginDSP.cpp#L347-L351)) - that is, after
`setState` and on `activate`. If the JSON differs from the cached copy it stores the new descriptors
and calls `requestParameterInfoUpdate()`. `initParameter` then reads from that cached copy, which is
what makes the DPF re-declare/diff loop work.

**Writing values.** A DAW parameter write in `setParameterValue` becomes a synthesised CC event
pushed into the engine alongside the real MIDI in `run()`
([PluginDSP.cpp:182-186](../packages/native/plugin/PluginDSP.cpp#L182-L186)). Values arrive on the
audio thread already, so this is the existing path with a different source.

**Persistence.** Parameter *values* are host-owned and symbol-keyed, so they need nothing from us.
The map is derived from the project (role + ROM), so it also needs nothing. A **user-editable**
override would live in the project JSON, and per the config-migration rule that means bumping
`K_PROJECT` and adding one raw `(obj) => obj` step in
[migrate.ts](../packages/retroplug/src/migrate.ts). Treat the override as a follow-up.

## 6. Verification

- **`pnpm test`** covers the TS projection: role table plus active systems in, flat descriptor list
  out. Pure logic, no host.
- **`retroplug-plugin-test`** (Catch2, `cmake --build build --target retroplug-plugin-test`) covers
  the diff classifier: descriptor-only change applies; a range or symbol change is rejected and does
  not mutate the live `Parameter`.
- **Real VST3 host.** A new `tools/reaper-params.lua` + `tools/run-reaper-params.sh`, modelled on
  [tools/reaper-editor-open.lua](../tools/reaper-editor-open.lua): insert RetroPlug, read
  `TrackFX_GetNumParams` / `TrackFX_GetParamName`, load an mGB project, re-read, and assert the names
  changed while the count did not. This is the only check that proves a host honours the restart
  flag.
- **Real CLAP host.** Reaper reads CLAP too. [tools/reaper-env.sh:132-142](../tools/reaper-env.sh#L132-L142)
  currently symlinks only `build/bin/retroplug.vst3` into `$HOME/.vst3`; add the `retroplug.clap` ->
  `$HOME/.clap` twin and run the same Lua with the format forced. `build/bin/retroplug.clap` already
  builds.
- **`tools/run-sanitizer.sh`** must stay clean, specifically the TSAN leg: the whole point of the
  side array in 3.3 is that the audio thread's `hints` read is never written.
- Add the new job to [tools/run-reaper-suite.sh](../tools/run-reaper-suite.sh) once it passes, keeping
  the `/dev/shm` concurrency cap in mind.

## 7. Risks and open questions

- **Host compliance varies.** `kParamTitlesChanged` is advisory. Reaper and Bitwig honour it; some
  hosts cache parameter names until the plugin is reinserted. The Reaper check in section 6 proves
  one host, not all. Worst case is stale names, never a crash, and the values keep working.
- **Automation follows the slot, not the name.** If a user automates `sys1_cc4` under mGB and then
  loads BlipToaster, the lane keeps controlling slot 4, which is now a different CC. That is inherent
  to a fixed pool and should be called out in the UI, not engineered around.
- **Pool size is a guess.** 16 slots per system is a starting point. Too small and ROMs cannot expose
  everything; too large and the DAW's parameter list is noisy even with hidden flags (not all hosts
  honour hidden). Pick after auditing mGB's and BlipToaster's actual CC maps.
- **Upstream divergence.** This is the fork's third feature commit. Keeping the ctor parameter
  defaulted and every edit behind `DISTRHO_PLUGIN_WANT_DYNAMIC_PARAMETERS` keeps the merge surface at
  two backend files. Worth offering upstream.

## 8. Sequencing

Each item is a commit that builds and passes on its own.

1. DPF: map `kParameterIsHidden` to `CLAP_PARAM_IS_HIDDEN` and `V3_PARAM_IS_HIDDEN`. Standalone
   improvement, no new API.
2. DPF: the macro, the `Plugin` API, the `PrivateData` fields, `reinitParameters()` and its diff
   classifier. No backend wiring yet, so nothing observable changes.
3. DPF: CLAP wiring (deferral + rescan). Verify against Reaper's CLAP scan.
4. DPF: VST3 wiring (latched flag + titles-changed) and the separate-controller guard.
5. RetroPlug: grow the pool, add the `__rp_parameterMapJson` seam, wire `setParameterValue` to
   synthesised CC. Names still generic.
6. RetroPlug: the mGB and BlipToaster CC tables plus the TS projection, with `pnpm test` coverage.
7. Tooling: `run-reaper-params.sh` for both formats, then add it to the suite.

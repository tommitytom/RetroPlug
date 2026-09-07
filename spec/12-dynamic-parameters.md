# Dynamic parameters

**Status: built.** Per-ROM DAW parameters: when mGB or BlipToaster is loaded, the host shows named
automation lanes ("PU1 Pulse Width", "Noise Volume") mapped to that ROM's MIDI CCs, instead of one
generic list. It spans two repos: the DPF fork (`deps/dpf.js/deps/dpf`,
`git@github.com:tommitytom/DPF.git`) and RetroPlug's plugin + TS layer.

Scope is **CLAP and VST3**. LV2, VST2, AU, JACK and the standalone keep today's behaviour unchanged:
they pass no callback, `Plugin::canUpdateParameterInfo()` is false there, and the pool keeps the names
it was declared with.

Verified against real Reaper in both formats (`pnpm reaper:params` / `reaper:params-clap`) and per
BlipToaster build (`reaper:params-vrc7`). Sections 1-5 below describe the design as built; section 9
records where the build diverged from the original plan and why.

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
- **Structural change** (`symbol`, `designation`, `groupId`, `ranges`, `enumValues`, or any hint other
  than `kParameterIsHidden`): **not applied**, and refused for the whole index so a mixed edit cannot
  half-apply. Logged via `d_stderr2`, contributes `kParameterInfoRefused`. Structural changes are a
  contract violation for this feature, and silently applying them would desync the audio thread (see
  3.3) and reinterpret recorded automation.

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

**The pool.** `PluginDSP` declares `1 + kMaxSystems * kCcSlotsPerSystem` = `1 + 4 * 160` = **641**
parameters. Symbols are `gain`, then `sys1_cc1` .. `sys4_cc160`. Every slot is `0..127`,
`kParameterIsInteger | kParameterIsAutomatable`. Default names are `"1: CC 1"` etc, hidden until
claimed.

160 is sized from the real CC tables, not guessed: the largest ROM in the set is BlipToaster's VRC7
build at **137** lanes (12 channels), then MMC5 106, VRC6 103, N163 101, S5B 95, the 2A03 core 65, and
mGB 24. `pnpm test midi/parameterMap` fails the build if any table outgrows the budget, since the
projection would otherwise truncate silently. **Changing `CC_SLOTS_PER_SYSTEM` shifts host
automation** on systems 2-4, whose pool indices are `system * CC_SLOTS_PER_SYSTEM` (symbols are
derived from `(system, n)` so DPF's own state restore survives, but CLAP's `clap_id` and VST3's
`param_id` are the flat index), so it is deliberately generous rather than tight.

**The map.** [parameterMap.ts](../packages/retroplug/src/parameterMap.ts) holds one `RomSpec` per ROM
build: its voice names per 0-based MIDI channel, and every CC it responds to with the channels that
accept it. `expandRomSpec` turns that into one lane per (CC, channel), named `"<Voice> <Control>"`.
The tables are transcribed from each ROM's own documentation - mGB's from the MIDI implementation map
in trash80/mGB's README, BlipToaster's from the per-chip tables in its `docs/chips/*.md`, each of
which lists every CC that build responds to and on which channels.

Two things are deliberately excluded from every table: the RPN bend-range handshake (CC101/100/6/38
is a three-message sequence, not a value) and the channel-mode messages (CC120 All Sound Off, CC121
Reset All Controllers, CC123 All Notes Off) - automating a panic message is actively harmful.

**Chip-global controls get one lane, not one per voice.** VRC7's custom patch is a single shared user
instrument (OPLL has only one), and the S5B envelope and noise generator are chip-wide. A `shared`
flag on the CC spec emits one un-prefixed lane addressed on the first channel that owns it.

**The vibrato/tremolo LFO, by contrast, is genuinely per-voice.** `lfo_tick()` in the ROM walks
`st = 0..CH_COUNT-1` reading `_lfoRate[st]` / `_lfoShape[st]` / `_lfoPhase[st]`, so every expansion
voice has its own - "FM 3 LFO Shape" is a real control. That same loop dips expansion volume through
`exp_trem()` (including the VRC6 sawtooth, whose accumulator base *is* its amplitude), which is why
tremolo depth belongs on the expansion voices too. Only the poly channel differs: it has one shared
LFO for the whole chord (`_polyLfo*`), and it is one lane because it is one channel.

**Which BlipToaster build.** Only one expansion chip can be active at a time, so each is a separate
`.nes` with its own CC set. It is read from the **chip label the ROM prints on its own monitor
header** - `blit_str(0, 1, AUDIO_CHIP_NAME)` in the ROM repo's `src/ui/ui.c`, where `AUDIO_CHIP_NAME`
is picked by the same `#if` chain that selects the audio driver. That is a NUL-terminated ASCII
literal in RODATA, so the ROM states which chip it drives in bytes we can read
([romDetect.ts](../packages/retroplug/src/bliptoaster/romDetect.ts)). The ROM provider records it as
`chip` on the system's `bliptoaster` role, which is where the projection reads it.

Two details make this exact rather than a substring guess:

- **The scan bound is enforced by the ROM's build, not estimated.** Its linker config gives the PRG
  region a hard `start = $8000, size = $4000` holding SIG + CODE + RODATA, with the DMC kit banks
  starting at file offset `$4010`, so every string literal in the main window is inside `$10..$4010`
  or the link fails. `BLIPTOASTER_CHIP_SCAN_LEN` is that bound.
- **The NUL terminator is required.** The VRC7 ROM contains `VRC7` twice - the chip label and the
  `"VRC7 PATCH"` heading - and the terminator picks the label. It also makes a chance hit in 6502
  code vanishingly unlikely. A ROM naming two different chips reports no match rather than picking
  one, falling through to the mapper.

**Why the label and not the header: mapper 69 is ambiguous.** The base 2A03 build takes FME-7 (69)
for kit banking alone, which is also the Sunsoft 5B mapper, and the two ROMs are byte-identical
across all 16 header bytes, the same size, and not iNES 2.0 (so there is no submapper to split
them); their bodies differ in ~16.7 KB. Nothing in the header can tell them apart, which is what
rules out a header-only read. The **iNES mapper survives as the fallback** for a caller holding only
a prefix, mapping 5 MMC5, 19 N163, 24 VRC6, 85 VRC7 and 69 to `2a03` - the subset that can never be
wrong, since the 2A03 CC set is a strict subset of S5B's.

Reaching the label costs a deeper read, so NES joins Sega in `roleSniffLen`
([systemsStore.ts](../packages/retroplug/src/systemsStore.ts)) - the same precedent as smsggdj's
marker at `$3640`. One read at construct, not on the classify path.

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

- **`pnpm test midi`** covers the TS projection
  ([test/midi/parameterMap.test.ts](../packages/retroplug/test/midi/parameterMap.test.ts)): slot
  arithmetic, the per-system cap, and the routing-reachability rule.
- **`pnpm test:plugin`** runs `retroplug-dynparams-test`
  ([test/plugin/DynamicParameters.test.cpp](../packages/native/test/plugin/DynamicParameters.test.cpp)),
  which drives `reinitParameters()` directly over a toy plugin with no plugin format built at all.
  It pins the classifier and, critically, that toggling hidden never writes `Parameter::hints`.
- **Real hosts, per-chip.** `pnpm reaper:params-vrc7` autoloads a BlipToaster VRC7 ROM through a
  hand-written thin `.rplg` (no `roles` key, so loading it re-runs the ROM providers - the detection
  chain under test) and asserts the host reports that build's lanes: a core 2A03 lane, a per-voice FM
  lane, and a chip-global custom-patch lane, so a build that silently fell back to the 2A03 table
  fails. Deterministic, no mouse. It is the only coverage of the per-chip tables, which the mGB path
  below never reaches. Point `RP_PARAMS_ROM` at any build to check another.
- **Real hosts, re-labelling.** `pnpm reaper:params` (VST3) and `pnpm reaper:params-clap` (CLAP) insert RetroPlug
  with no project, read every parameter name through ReaScript, click-load mGB **through the UI**,
  and re-read: the names must become mGB's map, `"1: CC 1"` must be gone, and the count must not have
  moved. This is the only check that proves a host acts on the flag the plugin raises, and loading
  through the UI also covers the editor idle poll. All three are in
  [run-reaper-suite.sh](../tools/run-reaper-suite.sh) as `params-vst3` / `params-clap` / `params-vrc7`;
  none is in CI (they need a full DAW + X stack).
- The format is requested by its **prefixed** name (`"CLAPi: RetroPlug"`) and then re-checked against
  what Reaper actually loaded. A bare `"RetroPlug"` let Reaper pick VST3 for both legs, so the CLAP
  run was silently a second VST3 run that still reported PASS.
- **`tools/run-sanitizer.sh thread`** passes, but note what it does *not* cover: it builds and runs
  `retroplug-host`, which does not link DPF, so it never exercises this code. The guard for the
  `hints` invariant in 3.3 is the Catch2 case above, not TSAN.

## 7. Risks and open questions

- **Host compliance varies.** `kParamTitlesChanged` is advisory. Reaper and Bitwig honour it; some
  hosts cache parameter names until the plugin is reinserted. The Reaper check in section 6 proves
  one host, not all. Worst case is stale names, never a crash, and the values keep working.
- **Automation follows the slot, not the name.** If a user automates `sys1_cc4` under mGB and then
  loads BlipToaster, the lane keeps controlling slot 4, which is now a different CC. That is inherent
  to a fixed pool and should be called out in the UI, not engineered around.
- **The pool is large, and hidden is only a hint.** 641 parameters, of which a typical single-system
  2A03 project claims 65. Hosts that honour `CLAP_PARAM_IS_HIDDEN` / `V3_PARAM_IS_HIDDEN` show only
  the claimed ones; hosts that ignore it list all 641. pluginval passes both formats at this size and
  Reaper is unbothered (its VST3 view is 2725, since DPF already prepends 2081 internal MIDI CC
  parameters), but a host with a tighter parameter budget is untested.
- **Upstream divergence.** This is the fork's third feature commit. Keeping the ctor parameter
  defaulted and every edit behind `DISTRHO_PLUGIN_WANT_DYNAMIC_PARAMETERS` keeps the merge surface at
  two backend files. Worth offering upstream.

## 8. Sequencing

Landed in this order, each a commit that builds and passes on its own. The first four are in the DPF
fork, the rest here.

1. DPF: map `kParameterIsHidden` to `CLAP_PARAM_IS_HIDDEN` and `V3_PARAM_IS_HIDDEN`.
2. DPF: the macro, the `Plugin` API, the `PrivateData` fields, `reinitParameters()` and its diff
   classifier.
3. DPF: CLAP wiring (deferral + rescan).
4. DPF: VST3 wiring (latched flag + titles-changed) and the separate-controller guard.
5. RetroPlug: the mGB and BlipToaster CC tables plus the TS projection.
6. RetroPlug: the pool, the `__rp_parameterMapJson` seam, the editor idle poll, and
   `setParameterValue` to synthesised CC.
7. `retroplug-dynparams-test`, then `run-reaper-params.sh` for both formats and the suite entry.

## 9. Where the build diverged from the plan

- **Enum values are structural, not a descriptor.** The plan applied them under a
  `kParameterTextChanged` flag. They cannot be: VST3 derives `step_count` from `enumValues.count`, and
  CLAP's `IS_STEPPED` is on its critical list needing `RESCAN_ALL`. Applying them live would leave a
  host with a stale step count. The separate text flag went with them; a change now notifies with
  `RESCAN_INFO|RESCAN_TEXT` unconditionally, which is what DPF already does elsewhere and is cheap.
- **A refusal is whole-index.** The plan did not say what happens to the descriptor fields of an index
  whose ranges also moved. They are dropped, so a mixed edit cannot half-apply.
- **The pool went to 160 slots per system, and the tables are complete rather than curated.** The
  first cut shipped 16 slots and a hand-picked ~16-entry table per ROM, which dropped most of what
  BlipToaster exposes (the Pulse MOD hack, fine bend, the Wave Traveler, the envelope/length block,
  the DMC address override) and could not represent VRC7 at all. Re-done from each ROM's own
  per-chip CC tables: every CC a build responds to, on every channel that accepts it, minus the RPN
  handshake and the channel-mode messages. See section 5 for the sizing and the index-shift hazard.
- **Slots are only claimed when the routing can reach them.** Not in the plan at all, and it turned out
  to matter: a parameter write becomes an ordinary staged CC, so `FourChannelsPerInstance` cannot
  address a ROM's fifth voice and the one-channel modes reach only its first. Those slots stay
  unclaimed rather than pretending to work.
- **The editor idle poll.** The plan polled only from `setState` and `activate`, which misses the main
  case: a ROM loaded through the UI never passes through `setState`. `SharedDSP` gained a
  `pollParameterMap` hook the editor drives from `uiIdle`.
- **The build is read from the ROM's chip label, not its mapper.** The first cut used the iNES mapper
  and shipped mapper 69 as a documented ambiguity, since the base 2A03 and S5B builds are
  header-identical. They are not otherwise identical: each prints its chip in its own monitor header,
  and that label is plain NUL-terminated ASCII the linker pins inside the PRG region. Reading it
  needed only a deeper prefix in `roleSniffLen`, so it closed with no change to the ROM. The mapper is
  still the fallback for a short read. See section 5.
- **TSAN does not cover this.** See section 6.

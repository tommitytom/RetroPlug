# 07 — Remaining work

The port is complete: the legacy build is gone, the transitional target suffix is dropped, and the
plugin identity is the canonical `RetroPlug`. What is left is **feature work, not migration** — a
short list of gaps, deferred features, one pending refactor, and a code-comment cleanup backlog.
This is the authoritative inventory; the other docs note a gap in a line and point here.

The runtime architecture is [01-architecture.md](01-architecture.md); nothing below changes it.

---

## 1. Feature gaps

Things a user could reasonably expect that aren't wired yet. None block shipping.

### Native file watcher — ROM hot-reload / config live-reload is inert

The design is "watcher = C++, policy = TS": native watches the config dir + `bindings/` and per-ROM
mtimes and reports changed paths through `HostRpcService::drainChangedPaths()`; TS
([fileWatcher.ts](../packages/retroplug/src/fileWatcher.ts) `FileWatcher.pump`) drains at idle and
reloads systems whose ROM changed with `reloadOnRomChange` on. The **TS policy half is built +
unit-tested**, but the **native half is a stub** — `drainChangedPaths()` returns `{}`
([HostRpcService.cpp:155](../packages/native/src/host/rpc/HostRpcService.cpp#L155)), and the host
links no watcher. So the "Reload on ROM Change" menu item stores/applies its preference but nothing
triggers the reload. Closing it means adding a per-ROM mtime poll (and/or `deps/efsw` over the config
dir) behind `drainChangedPaths`, then confirming `FileWatcher.pump` is driven from the plugin idle
loop. (`deps/efsw` is vendored and kept for exactly this — it is not linked today.)

### Raw LSDj Keyboard mode (LsdjSyncMode 4)

`lsdj-sync` implements every sync mode except raw **Keyboard**: MidiSync, MidiSyncArduinoboy,
MidiMap, KeyboardMidi, MidiPassthrough, MidiOut (ArduinoboyMaster), and MasterSync all ship
([dspRoles.ts](../packages/retroplug/src/dspRoles.ts)). Raw Keyboard emits nothing because it needs
the per-block **`keys` feed** that the live `Engine` path doesn't marshal yet — `Engine::processBlock`
passes `kNoButtons`/`kNoKeys` ([Engine.cpp:20](../packages/native/src/host/engine/Engine.cpp#L20)),
so the kernel's `buttons`/`keys` ABI is present but unfed. Feeding host keys/buttons per block unblocks
this mode. (See [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).)

### NES per-mapper expansion sub-channels (multichannel audio)

Per-console channel output ships end-to-end ([10-multichannel-audio-out.md](10-multichannel-audio-out.md),
steps 1–6): Game Boy 4-stem output, NES stereo-mod pins, and NES 5 individual core channels. The one
open piece is the **individual expansion voices** (VRC6 pulse/saw, VRC7 6×FM, N163, MMC5, FDS, S5B),
which live inside each mapper's audio class before they sum into the chip's `AudioChannel` delta, so
they need a per-mapper audio tap deeper than the `NesSoundMixer` edit. Deferred: no expansion-chip ROM
is committed to test them, and VRC7's emu2413 core is the one large tap.

### Kit-patch UI (rework from scratch)

LSDj kit patching (sample import / matcher / resampling) is a **fresh build**, not a port. The native
compilation primitives stay (`lsdj/KitCompiler` + `KitUtil` + `SampleCache` + `Effects`, over r8brain
+ enkiTS); the UI-thread behaviour is new. It runs entirely on the UI/control thread — it compiles the
kit and writes the already-patched memory regions **into the core**, with **no DSP-kernel component**.
It needs two seams that don't exist yet: the `ui?` render descriptor on `RoleType`
([04-roles-dsp-kernel.md](04-roles-dsp-kernel.md)) and a control-plane "write a memory region into a
core" capability. Kit (sample-patch) **state is also not serialized** into the thin config yet
([05-data-persistence.md](05-data-persistence.md)).

### Live `applyConfigField` for Mesen

SameBoy knobs (model / highpass / link group / fast boot) apply live via the command ring. The NES
role knobs (Region / Remove Sprite Limit) apply **at construct** only — the `dynamic_cast` in
[`Engine::applyConfigField`](../packages/native/src/host/engine/Engine.cpp#L152) is SameBoy-only, so
changing a Mesen knob rebuilds the system rather than nudging the running core. Generalise it to a
`SystemBase` virtual when Mesen grows live knobs.

### Standalone disk-wins reopen

In a DAW the host chunk is authoritative (get/setState). The standalone starts **empty** unless
`RETROPLUG_AUTOLOAD_PROJECT` seeds it; reopening the last-saved project from disk on launch is not
built ([05-data-persistence.md](05-data-persistence.md)).

### Live memory-region subscription

There is no live "watch RAM" streaming path (`enableMemorySnapshot(type)`). Reads are one-shot through
the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door. If live
memory streaming to the UI (an HD-player-style view) is ever wanted, that arming seam must be built.

### CLI debugger: Mesen `.mlb` labels

The debug RPC facet ([09-cli-debugging.md](09-cli-debugging.md)) is built out — APU/PPU state,
CPU/memory peek + poke, per-frame register-event capture (`drainEvents`), breakpoints, trace, step,
profiler, and cc65 `.dbg` labels. The one remaining item is Mesen native `.mlb` label files, which
need a new parser (Mesen's C# one isn't vendored). A CLI-only nicety.

---

## 2. Deferred / dropped

Intentionally not planned; listed so the intent isn't lost.

- **About panel** — dropped (offered little); the one menu item still marked deferred in
  [menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts).
- **LV2** — deferred indefinitely; its out-of-process DSP/UI split doesn't fit RetroPlug. (VST2 + AU
  now build — see [06-build-test.md](06-build-test.md).)
- **`ui?` render descriptor + third-party extension model** — a role's own settings UI, and
  registering `RoleType`s / ROM providers / behaviours from outside the built-ins, are future work.
- **Savestate slots** / **sav inspector** — never built; the state-snapshot machinery and the pure-TS
  sav codec exist, but a user-facing multi-slot feature and a React sav view do not.
- **Web / Emscripten port** — design-only.

---

## 3. Pending refactor

- **The `SnapshotRegistry` double-copy.** The read door copies from each core's own tear-free triple
  into a registry-owned buffer because the shared `SystemBase` can't yet publish straight into the
  registry ([SnapshotRegistry.hpp:22](../packages/native/src/host/engine/SnapshotRegistry.hpp#L22)).
  It is a documented redundancy, not a bug; collapsing it is a `SystemBase` refactor, not urgent.

---

## 4. Code-comment cleanup backlog (note, don't fix)

Stale scaffolding to sweep opportunistically — not work to schedule.

A handful of in-code deferral / provenance comments still describe an earlier state and should be
swept as their feature lands or when touched. Known ones:

| Location | Marker |
|---|---|
| [appStores.ts:15](../packages/retroplug/src/appStores.ts#L15) | calls the plugin "headless" — stale; the editor now reuses the published control-plane graph ([03-ts-layer.md](03-ts-layer.md)) |
| [host/dsp/DspRuntime.cpp](../packages/native/src/host/dsp/DspRuntime.cpp) | the GB serial pump is a plain FIFO; intra-block frame timing not yet modelled |
| [host/engine/Engine.cpp](../packages/native/src/host/engine/Engine.cpp) | `fastBoot` takes effect on the next restart, not live |
| [plugin/PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp) | host MIDI short messages only; SysEx deferred |
| [systemRoles.ts](../packages/retroplug/src/systemRoles.ts) / [systemsStore.ts](../packages/retroplug/src/systemsStore.ts) | kit-patch stays a deferred `ui` UI-thread behaviour; feature behaviour is the deferred script future |

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

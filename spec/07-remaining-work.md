# 07 — Remaining work

The port is complete: the legacy build is gone, the transitional target suffix is dropped, and the
plugin identity is the canonical `RetroPlug`. What is left is **feature work, not migration** — a
short list of gaps, deferred features, one pending refactor, and a code-comment cleanup backlog.
This is the authoritative inventory; the other docs note a gap in a line and point here.

The runtime architecture is [01-architecture.md](01-architecture.md); nothing below changes it.

---

## 1. Feature gaps

Things a user could reasonably expect that aren't wired yet. None block shipping.

### Raw LSDj Keyboard mode (LsdjSyncMode 4)

`lsdj-sync` implements every sync mode except raw **Keyboard**: MidiSync, MidiSyncArduinoboy,
MidiMap, KeyboardMidi, MidiPassthrough, MidiOut (ArduinoboyMaster), and MasterSync all ship
([dspRoles.ts](../packages/retroplug/src/dspRoles.ts)). Raw Keyboard emits nothing because it needs
the per-block **`keys` feed** that the live `Engine` path doesn't marshal yet —
`Engine::runBlockWithRouter` passes the empty
`kNoButtons`/`kNoKeys` ([Engine.cpp:163](../packages/native/src/host/engine/Engine.cpp#L163)),
so the kernel's `buttons`/`keys` ABI is present but unfed. Feeding host keys/buttons per block unblocks
this mode. (See [04-roles-dsp-kernel.md](04-roles-dsp-kernel.md).)

### NES per-mapper expansion sub-channels (multichannel audio)

Per-console channel output ships end-to-end ([10-multichannel-audio-out.md](10-multichannel-audio-out.md),
steps 1–6): Game Boy 4-stem output, NES stereo-mod pins, and NES 5 individual core channels. The one
open piece is the **individual expansion voices** (VRC6 pulse/saw, VRC7 6×FM, N163, MMC5, FDS, S5B),
which live inside each mapper's audio class before they sum into the chip's `AudioChannel` delta, so
they need a per-mapper audio tap deeper than the `NesSoundMixer` edit, and VRC7's emu2413 core is the
one large tap. (Test material is no longer the obstacle: `resources/roms/bliptoaster-{vrc6,vrc7,n163,
mmc5,s5b}.nes` are all committed, and `reaper:params-vrc7` already drives one of them.)

### Standalone disk-wins reopen

In a DAW the host chunk is authoritative (get/setState). The standalone starts **empty** unless
`RETROPLUG_AUTOLOAD_PROJECT` seeds it; reopening the last-saved project from disk on launch is not
built ([05-data-persistence.md](05-data-persistence.md)).

### Live memory-region subscription

There is no live "watch RAM" streaming path (`enableMemorySnapshot(type)`). Reads are one-shot through
the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
([useLsdjHdSession.ts](../packages/retroplug/ui/screens/hd/useLsdjHdSession.ts)) on a frame divider,
because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
cost question, not a capability one - an arming seam would let those two stop copying the whole image
to read a few hundred bytes of it.

### MIDI **out** is capped at 4 bytes

MIDI **in** takes every size (`PluginDSP.cpp:300` reads `dataExt` when a message outgrows the inline
`data[4]`), so a DAW can upload an N163 wave the way the CLI does. The **out** path drops anything
longer: `PluginDSP.cpp:344` skips events over `MidiEvent::kDataSize`. Lifting it is not a one-line
change - the plugin never sets `dataExt`, so simply removing the guard is a null dereference in
DPF's VST3 writer. It also buys little: VST2 drops messages over 4 bytes itself, CLAP drops over 3,
and VST3 drops SysEx regardless, so the payoff exists only on the JACK standalone.

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
  registry ([SnapshotRegistry.hpp:23](../packages/native/src/host/engine/SnapshotRegistry.hpp#L23)).
  It is a documented redundancy, not a bug; collapsing it is a `SystemBase` refactor, not urgent.

---

## 4. Code-comment cleanup backlog (note, don't fix)

Stale scaffolding to sweep opportunistically — not work to schedule.

A handful of in-code deferral / provenance comments still describe an earlier state and should be
swept as their feature lands or when touched. Known ones:

| Location | Marker |
|---|---|
| [host/dsp/DspRuntime.cpp](../packages/native/src/host/dsp/DspRuntime.cpp) | the GB serial pump is a plain FIFO; intra-block frame timing not yet modelled |
| [host/engine/Engine.cpp](../packages/native/src/host/engine/Engine.cpp) | `fastBoot` takes effect on the next restart, not live |
| [systemRoles.ts:13,59](../packages/retroplug/src/systemRoles.ts#L13) | names kit-patch as the motivating example for the deferred `ui` descriptor. The descriptor is still unbuilt, but kit patching shipped without it - `lsdj-assets` is a no-DSP role that compiles on the control plane through the `compileKit` RPC - so the example needs replacing, not the entry |

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

# Engineering Report & Plan: Novation Launchpad support in RetroPlug

**Date:** 2026-08-11 · **Branch:** `feature/launchpad` · **Status:** planned, nothing built
**First consumer:** LSDj MI.MAP (song-row launching from the 8x8 grid)
**Target device:** Launchpad Pro [MK3]

---

## 0. Decisions taken

| Question | Decision | Consequence |
|---|---|---|
| Where does the mapping layer run? | **DSP role** (audio thread), not the UI thread | Sample-accurate pad handling; identical in plugin + standalone; needs a memory seam into the kernel for the emulated case (§7.2) |
| Which hosts first? | **Standalone first** | RetroPlug owns the Launchpad's MIDI port directly; plugin (DAW-routed) deferred |
| What does "scriptable in TS" mean for v1? | **In-repo TS modules registered like roles** | A controller app is a module conforming to an interface, registered by name. No runtime script loader (that stays the deferred extension model in [spec/07](../spec/07-remaining-work.md)) |
| Must it work with a **real Game Boy**? | **Yes** | No memory reads at all on that path. This is the single biggest shaper of the design (§4, §7.2) |

The general layer is device-agnostic by construction, but only the Pro [MK3] profile ships in v1.

---

## 1. Executive summary

### What we are building

Three layers, all pure TypeScript except a thin native device seam:

1. **A Launchpad protocol module** - no I/O, no RetroPlug knowledge. Grid addressing, colour palette, mode entry/exit, message decode, and a diffing surface that emits minimal byte batches.
2. **A controller-app seam** - a registry of TS modules that consume surface events and produce surface lighting, mirroring how [`RoleRegistry`](../packages/retroplug/src/systemRoles.ts) holds DSP behaviours today.
3. **An LSDj MI.MAP app** - the first consumer. Pads launch song rows; LEDs show the song grid and the playhead.

### Verdict

Feasible, and most of it is pure TS on rails that already exist. The LSDj side needs **no new tracker code at all**: [`dspRoles.ts:103`](../packages/retroplug/src/dspRoles.ts#L103) already implements MI.MAP byte-for-byte. The native work is bounded and enumerable (§6).

### The one real complication

Supporting a **real Game Boy** removes every memory read, and MI.MAP carries no return channel. So playback state cannot be observed on that path - it has to be *predicted*. The design absorbs this by putting a `PlaybackModel` seam between the app and its state source, with two implementations (observed vs predicted) and a differential test that measures how far the predictor drifts from ground truth (§4, §9). That turns "how blind are we" from a worry into a number.

---

## 2. The consumer: LSDj MI.MAP

### 2.1 What crosses the wire

MI.MAP is **one-directional**. Per Arduinoboy's `Mode_LSDJ_Map.ino`:

| Event | Byte sent to LSDj |
|---|---|
| NoteOn, MIDI ch 1 | song row = note (0..127) |
| NoteOn, MIDI ch 2 | song row = note + 128 (128..255) |
| NoteOff of the last-sounded row | `0xFE` |

Nothing is read back. The row readout LSDj shows top-right on the song screen is drawn on the LCD; it never leaves the cart.

### 2.2 RetroPlug already implements exactly this

[`dspRoles.ts:103-121`](../packages/retroplug/src/dspRoles.ts#L103-L121) (`midiMap`) is a byte-for-byte match, including the `0xFE` handshake and the `lastRow` cross-block state. `midiMapRow` ([`dspRoles.ts:62`](../packages/retroplug/src/dspRoles.ts#L62)) does the two-channel row extension. **The Launchpad app produces the MIDI this role already consumes** - it does not reimplement LSDj.

### 2.3 Correction: LIVEMAP no longer exists

Earlier design discussion assumed the cue-quantised LIVEMAP variant. It was **removed** in LSDj aboy 4.7.8 / Arduinoboy 1.3.0, where SYNCMAP was reworked and renamed MI.MAP. Consequences:

- LSDj is a **clock slave** in MI.MAP. The host owns tempo and transport.
- A row change is **immediate**, not quantised to the chain boundary.
- Therefore **launch quantisation is ours to implement**, on emulator and hardware alike. This is a feature the app must provide, not a behaviour we inherit.

### 2.4 Sync modes are mutually exclusive

`SYNC` is a single value on LSDj's PROJECT screen. A cart in MI.MAP is not in MI.OUT, so there is no way to get note/row feedback alongside map mode. The only row byte LSDj ever emits is in LSDJ-master mode, once, at play start.

### 2.5 MEASURED: what a real cart actually does in MI.MAP

Everything above is documentation. The following is measured against a real `lsdj9_3_3-arduinoboy.gb`
by [test-native/lsdj-playback-probe.test.ts](../packages/retroplug/test-native/lsdj-playback-probe.test.ts),
which drives the wire protocol directly through the `midiPassthrough` role and samples position from
WRAM. These are the rules the predictor implements.

| # | Question | Measured answer |
|---|---|---|
| B1 | Does the cart advance through the **shipped `midiMap` role**? | **No.** The launched row triggers and sounds, then freezes on step 0 forever. The role never sends a clock, so the cart never steps. |
| B0 | Does a bare row byte trigger, and does `0xFF` disturb a playing cart? | Row byte triggers immediately; `0xFF` leaves it playing. `0xFF` is a transparent clock, not a row. |
| B2 | Ticks per step? | **Exactly 6**, every time. A 24-PPQN clock is 4 steps/beat; one 16-step phrase is 96 ticks (one bar). |
| B3 | Does it auto-advance past the launched row? | **Yes**, one row per chain, and it **wraps** at the end of the song: launching row 0 walks 0,1,2,0,1,2,... at 96-tick intervals. |
| B4 | Is a launch song-wide, and do channels then diverge? | Launch sets **all four** channels to the row; they then advance **independently**. With a two-phrase chain on pu2 against one-phrase neighbours, pu2 falls a row behind and stays behind. **Four cursors, not one.** |
| B5 | What does the `0xFE` NoteOff handshake do? | **Nothing to playback.** The cart keeps playing and stepping normally. Releasing a pad is not a stop, so the app needs its own stop affordance. |
| B6 | How does a chain treat an empty phrase slot? | A chain **ends at its first empty slot** - in `[phrase 0, EMPTY, phrase 2]`, phrase 2 never plays. Chain length is therefore just "slots before the first null". |
| B7 | Do rows 254/255 collide with the sentinels? | They are byte-identical to `0xFE`/`0xFF`, so the launchable range is **0..253**. |

Two consequences worth pulling out:

- **`midiMap` was incomplete as shipped, and is now fixed.** B1 is a real gap, not a quirk of the test:
  Arduinoboy's map mode forwards MIDI clock and ours did not, so a RetroPlug MI.MAP cart triggered rows
  but never played through them. The existing `lsdj-midimap.test.ts` missed it because it only asserts
  that a mapped row makes sound - and a frozen first step is still audible. The role now emits `0xFF` at
  24 PPQN from `eachTick` (transport-gated for free, so a stopped host leaves the cart untouched), at
  frame 0 like the other Arduinoboy-family modes. Guarded by a second test in `lsdj-midimap.test.ts`
  that watches POSITION rather than loudness.
- **The predictor is a four-cursor, row-level model** whose only real input is chain length. B2/B3/B6
  together give the whole arithmetic: chain length = slots before the first null, row duration =
  slots x 96 ticks, advance one row per chain end, wrap at the end of the song.

### 2.6 RESULT: dead reckoning is exact at row level

M0 is built and measured. [`PredictedLsdjModel`](../packages/retroplug/src/lsdj/playback/predict.ts) runs
the rules above against a real cart in
[lsdj-playback-differential.test.ts](../packages/retroplug/test-native/lsdj-playback-differential.test.ts),
driven byte-for-byte off the same clock:

```
offset +0: 100.0% over 2400 comparisons
  pu1: 100.0%   pu2: 100.0%   wav: 100.0%   noi: 100.0%
```

600 ticks, several song wraps, with pu2 deliberately diverging from the other three. **No disagreement
at all.** The test sweeps alignment offsets rather than assuming one, which is how it found that the
cart spends the launch byte as its first tick (the peak sat at +1 until the model accounted for it -
independently corroborated by B2's first step landing at tick 5 and B3's first row change at 95).

So for the hardware path the honest answer to "how blind are we" is: **not blind at all about position,
as long as the player does not touch the handheld.** What remains unknowable is unchanged (4.3) - device
navigation, a manual START, live edits, and the state at connect. Those are detectable by nothing in the
protocol, which is what the optional re-anchor (4.4) exists for.

---

## 3. The device: Launchpad Pro [MK3]

All facts below are from the official [Programmer's Reference](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/LPP3_prog_ref_guide_200415.pdf).

### 3.1 Three USB interfaces - picking the right one is load-bearing

| Interface | Purpose |
|---|---|
| `LPProMK3 MIDI` | Custom modes + **Programmer mode**. **This is the one we use.** |
| `LPProMK3 DIN` | Traffic to/from the physical DIN jacks |
| `LPProMK3 DAW` | Session mode only, and only once DAW mode is enabled |

Programmer-mode LED traffic and pad events live on the **MIDI** interface. Sending our SysEx to the DAW port is the classic first-attempt failure.

### 3.2 SysEx

Header for every message, both directions: `F0 00 20 29 02 0E` (`0Eh` = Pro MK3; Mini MK3 is `0Dh`, X is `0Ch`).

| Function | Message |
|---|---|
| Live / Programmer toggle | `F0 00 20 29 02 0E 0E <0=Live,1=Programmer> F7` |
| Select layout | `F0 00 20 29 02 0E 00 <layout> <page> 00 F7` (Programmer = layout `11h`) |
| Bulk LED lighting | `F0 00 20 29 02 0E 03 <ColourSpec>... F7` |
| DAW / Standalone | `F0 00 20 29 02 0E 10 <mode> F7` (we never enable DAW mode) |
| Device inquiry | `F0 7E 7F 06 01 F7` -> reply carries family `13h 01h` |

Two behaviours the driver **must** honour:

- The device **always boots into Live mode**. We enter Programmer mode explicitly at connect.
- While Programmer mode is selected via SysEx, the Settings menu is locked out on the hardware. **We must send Live mode on disconnect / shutdown**, or we leave the user's device in a state they cannot escape from the front panel.

### 3.3 Grid and lighting

- 8x8 grid addressed as `row * 10 + col + 11`, row 0 at the **bottom**: `11` = bottom-left, `81` = top-left, `18` = bottom-right. Surrounding CC buttons: top row `91..98`, logo `99`, bottom row `101..108`.
- Lighting by MIDI channel: **ch 1 = static, ch 2 = flashing, ch 3 = pulsing**; velocity indexes a 128-entry palette. Velocity 0 blanks.
- Flashing and pulsing are **synchronised to incoming MIDI beat clock** (one beat / two beats per period). Since RetroPlug is the clock master in MI.MAP, a "cued but not yet playing" pad can pulse *on the beat* for free, with no per-frame LED traffic.
- The bulk LED SysEx carries up to **106 colour specs** - one message repaints the entire surface. Spec = lighting type (1B) + LED index (1B) + data (1-3B), where type 3 is direct RGB.

### 3.4 BUILT: what the protocol module covers

[`src/launchpad/`](../packages/retroplug/src/launchpad/) implements all of the above as a pure layer -
no I/O, no RetroPlug types, no tracker knowledge. Every builder is pinned to one of the manual's own
worked hex examples in [test/launchpad/](../packages/retroplug/test/launchpad/), so the reference is the
oracle rather than our reading of it.

Two limits found while building it, both from the PDF rather than the protocol:

- **The 128-colour palette is an image**, so its RGB values cannot be extracted and are not invented. A
  palette colour is an opaque 0..127 index (with names for the seven the manual states in prose), and
  anything needing an exact colour uses the RGB lighting type, which needs no palette. RGB is
  static-only, since lighting types 1 and 2 carry palette indices - the encoder refuses an RGB flash or
  pulse rather than sending a different colour.
- **The programmer-mode layout diagram is an image too**, so only the 8x8 grid anchors (11 / 81 / 18) are
  text-verifiable. The edge-button CC numbers are the community mapping, marked as unverified in
  `profile.ts` and to be confirmed on hardware in M4.

The API uses a **top-left origin** (y = 0 at the top) against a device that counts rows upwards from the
bottom; the flip lives in `padIndex`/`padAt` alone.

### 3.5 What we deliberately do not use

DAW mode, Session layout, faders, the DIN port, the internal sequencer. Programmer mode disables all of it anyway, which is precisely why it is the right layer to build on.

---

## 4. Playback state: what can be known

### 4.1 Emulated cart - ground truth is cheap

The DSP kernel runs **on the same thread as the live cores, before they render**: [`Engine.cpp:77-127`](../packages/native/src/host/engine/Engine.cpp#L77-L127) runs `dsp_.processBlock`, fans its sinks to the cores, and only then calls `runBlock`. A role reading memory therefore sees end-of-previous-block state - the same one-block latency `ctx.serialOut` already documents, and consistent with it.

`SystemBase::getMemory(MemoryType::Ram, Read)` returns a [`MemoryAccessor`](../packages/native/src/system/MemoryAccessor.hpp#L30) holding a raw pointer + size, so the kernel can be handed a zero-copy typed-array view. No copy, no snapshot, no RPC (the existing `readRam` RPC goes through `SnapshotRegistry` only because it is called from the *control* thread).

Two sources, both direct:

| Want | Read |
|---|---|
| Playhead: per-channel song row / chain / phrase row / playing | WRAM, decoded by [`LsdjReader`](../packages/retroplug/src/lsdj/runtime/) |
| Which song rows hold chains | SRAM offset `0x1290`, 1024 bytes (256 rows x 4 channels), raw - no codec ([`regions.ts:85`](../packages/retroplug/src/lsdj/codec/regions.ts#L85); the working song sits uncompressed at sav offset 0) |

The SRAM read needs **no decode and no config push**, and it tracks the user's live edits in LSDj for free.

### 4.2 Real Game Boy - dead reckoning

Topology: RetroPlug -> MIDI out -> Arduinoboy (or Teensy/RP2040 equivalent) -> link cable -> Game Boy. RetroPlug cannot drive a link port itself.

No memory. But in MI.MAP **we are the clock master**, so a great deal is ours by construction:

- **Tempo and absolute tick count** - we generate them. Under MIDI sync LSDj advances one step per 6 ticks, which is arithmetic `ctx.eachTick` already performs.
- **Transport** - we command start/stop.
- **The cued row** - we sent it.
- **The song** - it is a file. The cart's `.sav` is the same `.sav` we have a complete pure-TS codec for: chain assignments, chain lengths, phrase contents, grooves, hop commands. All offline-derivable.

So the LED state can be driven by **simulating a deterministic sequencer that we are clocking**, seeded from the song file. This is the same class of thing as `risaSync`'s locate arithmetic, not guesswork.

### 4.3 Knowability matrix

| | Emulated | Real GB (MI.MAP) |
|---|---|---|
| Row we launched | known | known (we sent it) |
| Tick / step position | exact, from WRAM | derived - we are the clock |
| Transport (as commanded) | exact | known (we command it) |
| Which rows hold chains | exact, from SRAM | from the user-supplied `.sav` |
| Chain / phrase position | exact | modelled from the sav, drift-prone |
| User navigating on the device | visible | **invisible** |
| User pressing START on the device | visible | **invisible** |
| Live song edits on the device | visible | **invisible** |
| State at connect time | exact | **unknown** |
| Detecting that we have drifted | n/a | **impossible** |

### 4.4 Re-anchoring

Because a row launch is immediate and repeatable, the model can force truth to match itself by periodically re-sending the row it believes is current. This is a **"model wins"** strategy: it overrides anything the player did on the handheld. It ships as an option, off by default, with the trade-off stated in the UI.

---

## 5. What RetroPlug already provides

| Rail | Where | Relevance |
|---|---|---|
| MI.MAP implementation | [`dspRoles.ts:103`](../packages/retroplug/src/dspRoles.ts#L103) | The LSDj side is done |
| Role registry + zod-validated config, testable with zero C++ | [`systemRoles.ts`](../packages/retroplug/src/systemRoles.ts), [`dspRoles.ts`](../packages/retroplug/src/dspRoles.ts) | The template for the controller-app registry |
| Project-scope behaviours run **regardless of system count** | [`dspKernel.ts:399`](../packages/retroplug/src/dspKernel.ts#L399) | A device-only project (real GB, no emulated system) works structurally |
| SysEx already contemplated in TS routing | [`midiRouting.ts:43`](../packages/retroplug/src/midiRouting.ts#L43) | Long messages broadcast unchanged; no TS-side change needed |
| Pure-TS LSDj sav codec + runtime WRAM reader | [`src/lsdj/`](../packages/retroplug/src/lsdj/) | Both `PlaybackModel` implementations are built from existing parts |
| **`N8Link`**: audio thread -> SpscRing -> dedicated device thread, injected `PortFactory` so it is testable hardware-free | [`N8Link.hpp`](../packages/native/src/host/n8/N8Link.hpp) | **The direct architectural precedent** for a device-scoped link |
| `Engine::setCoreByteSink` - mirror a role-generated stream to external hardware, in sync by construction | [`Engine.hpp:92`](../packages/native/src/host/engine/Engine.hpp#L92) | Exactly the shape of "emulated cart and real cart get the same bytes" |
| Device picker UI pattern | [`n8Devices.ts`](../packages/retroplug/ui/screens/menu/n8Devices.ts), [`midiDevices.ts`](../packages/retroplug/ui/screens/menu/midiDevices.ts) | Copy for the Launchpad picker |

The N8 work merged to main is worth dwelling on: driving a real Everdrive N8 from a role-generated byte stream is the *same problem shape* as driving a real Game Boy through an Arduinoboy. The transport differs (USB serial vs MIDI out), the pattern does not.

---

## 6. Gaps and blockers

### 6.1 SysEx cannot cross any MIDI seam today

| Seam | Limit | Where |
|---|---|---|
| RtMidi input | `ignoreTypes(true, ...)` drops SysEx at the source | [`MidiIo.cpp:23,82`](../packages/native/src/host/input/MidiIo.cpp#L23) |
| Standalone MIDI in | `bytes.size() > 3` -> dropped | [`sdl/main.cpp:809`](../packages/native/sdl/main.cpp#L809) |
| Standalone MIDI out | only 1..3 bytes sent | [`sdl/main.cpp:829`](../packages/native/sdl/main.cpp#L829) |
| Plugin MIDI in | capped at 4 | [`PluginDSP.cpp:189`](../packages/native/plugin/PluginDSP.cpp#L189) |
| Plugin MIDI out | capped at `MidiEvent::kDataSize` (4) | [`PluginDSP.cpp:203`](../packages/native/plugin/PluginDSP.cpp#L203) |
| Control plane -> engine | `DspCommand.stageMidi` holds 4 bytes | [`DspCommand.hpp:31`](../packages/native/src/host/engine/DspCommand.hpp#L31) |

Nothing is architecturally blocked: `MidiIo::send` already takes an arbitrary length, and DPF's `MidiEvent` carries a `dataExt` pointer for sizes above 4. Standalone needs the first three rows fixed; the plugin rows are deferred with the plugin.

### 6.2 There is no device-scoped MIDI

Standalone opens **every** hardware input and merges them into one engine stream (`MidiIo::openHardwareInputs`), and `send` mirrors to the single selected hardware output. Consequences if we do nothing:

- Pad presses arrive as ordinary musical MIDI and get routed into mGB / LSDj.
- LED traffic sprays at every other listening device.
- This is the same class as the known "All Devices forwards a controller's mixer ports" quirk.

The Launchpad needs its **own** in/out pair, excluded from the engine MIDI stream.

### 6.3 With a real Game Boy there is no system to attach to

Feature roles attach per-system by ROM identity. On the hardware path there is no `SystemBase` at all. Hence the app must be **project-scope** (like `midi-routing`), with the target chosen by config. Verified that project stages run with zero systems: [`dspKernel.ts:399-401`](../packages/retroplug/src/dspKernel.ts#L399-L401).

### 6.4 `stageMidiIn` is harness-only

[`BackendRpcRegistration.hpp:76`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp#L76) mounts it on the **harness** facet, deliberately not exposed to the plugin or SDL channels. Noted for completeness; the DSP-role decision means we do not need it.

---

## 7. Architecture

### 7.1 Layers

```
src/launchpad/            pure protocol, zero I/O, zero RetroPlug knowledge
  profiles.ts             device table (Pro MK3 first): sysex id, grid math, palette, mode entry/exit
  surface.ts              per-LED style + dirty diffing -> minimal byte batches (bulk sysex vs 3-byte notes)
  decode.ts              incoming note/CC/aftertouch -> typed pad events

src/controller/           host-agnostic app seam
  session.ts              { onMidi(bytes), tick(ctx), send(bytes) }
  registry.ts             name -> app module, mirroring RoleRegistry
  playbackModel.ts        the observed/predicted seam (§7.2)
  trackerTarget.ts        the emulated-core/external-device seam (§7.3)

src/controller/apps/
  lsdjMidiMap.ts          the first consumer
```

### 7.2 The `PlaybackModel` seam

One interface, two implementations, chosen by config. The app never knows which it has; LED fidelity degrades, function does not.

| Implementation | Source | Fidelity |
|---|---|---|
| `ObservedLsdjModel` | WRAM + SRAM via a new `ctx.ram` / `ctx.sram` kernel seam | ground truth |
| `PredictedLsdjModel` | the `.sav` file + our own clock | dead reckoning, drift-prone |

The predicted model is the one the real-Game-Boy path lives or dies on, and it is **pure TS with no native dependency**, so it is built and validated first.

### 7.3 The `TrackerTarget` seam

The app emits row launches; the target decides where they go.

| Target | Sink |
|---|---|
| Emulated cart | `ctx.pushSerialIn` via the existing `midiMap` role |
| Real Game Boy | `ctx.emitMidiOut` to the Arduinoboy |

Both receive the same launch stream, so the two paths are in sync by construction - the same property `setCoreByteSink` gives the risa/N8 mirror.

### 7.4 Where each piece runs

| Piece | Thread | Notes |
|---|---|---|
| Pad decode, app logic, LED diffing | audio (DSP kernel) | sample-accurate launches |
| Launchpad port I/O | dedicated device thread | `N8Link` pattern: SpscRing + injected port factory |
| Device selection, connect/disconnect | UI | `n8Devices.ts` pattern |
| Programmer-mode entry, and **Live-mode restore on exit** | UI (connect/disconnect lifecycle) | must survive an abnormal shutdown as best we can |

### 7.5 Scriptability

A controller app is a TS module implementing `session.ts`, registered by name and picked from a menu - exactly how DSP roles work today, with zod-validated config on the same rails. Runtime-loadable user scripts remain out of scope (the deferred extension model).

---

## 8. Milestones

Ordered so that everything testable without hardware or native changes comes first.

| M | Deliverable | Native? | Verified by |
|---|---|---|---|
| **M0** ✅ | `PlaybackModel` interface + `PredictedLsdjModel` (sav + clock) | no | DONE - 100% agreement vs a real cart (§2.6) |
| **M1** ✅ | `src/launchpad/` protocol + `Surface` diffing, Pro MK3 profile | no | DONE - 37 tests, the manual's own hex as golden vectors |
| **M2** | `src/controller/` session + registry + `lsdjMidiMap` app against a fake device | no | `pnpm test` |
| **M3** | SysEx widening (RtMidi `ignoreTypes`, the two `sdl/main.cpp` caps) | yes | loopback: send a bulk-LED SysEx, observe it intact |
| **M4** | Launchpad device link: own in/out pair, excluded from the engine stream, injected port factory, Settings picker | yes | hardware-free test via the fake factory; real-device smoke |
| **M5** | Project-scope role wiring `TrackerTarget` (emulated core vs Arduinoboy MIDI out) | no | `pnpm test` + `test:native` |
| **M6** | `ctx.ram` / `ctx.sram` kernel seam + `ObservedLsdjModel` | yes | `test:native`; closes the differential loop from M0 |

M0-M2 deliver a fully testable app with no device and no native change. M3-M4 make a physical Launchpad work. M6 upgrades emulated-cart fidelity and is genuinely optional.

---

## 9. Testing

- **Protocol**: golden byte vectors for grid addressing, palette, mode entry/exit, and bulk-LED batching. Pure `pnpm test`.
- **App logic**: a fake surface (pad events in, byte batches out) plus a fake `PlaybackModel`. Pure `pnpm test`.
- **Predictor accuracy (the important one)**: run `PredictedLsdjModel` and `ObservedLsdjModel` side by side against a real emulated cart over a long render, and assert divergence stays within a stated bound. Same shape as the existing `--drift` analysis. This is the only honest measure of how well the real-hardware path behaves, and it is writable before any hardware exists.
- **Device link**: injected port factory, as `N8Link` does, so connect/disconnect/teardown are covered with no Launchpad attached.
- **Manual, hardware**: real Pro MK3 on the MIDI port; confirm Programmer mode entry, LED batching, and - specifically - that **Live mode is restored on exit**.

---

## 10. Risks and open questions

1. **Predictor drift is undetectable on hardware** (§4.3). Mitigated by the M0 differential test and the optional re-anchor (§4.4), not eliminated. This is inherent to MI.MAP being one-directional.
2. **Initial state on connect is unknown** on hardware. Likely answer: define a known starting state by commanding transport ourselves rather than trying to infer one.
3. **Launch quantisation is now our job** (§2.3). Needs a product decision: launch immediately, on the next step, or on the next bar.
4. **Leaving the device in Programmer mode** after a crash locks the user out of its Settings menu until a power cycle. Best-effort restore on exit; document the recovery.
5. **A user-supplied `.sav` may not match what is actually on the cart.** The predictor is only as good as the song file it is given. Consider surfacing an explicit "song loaded for LED feedback" state rather than failing silently.
6. **Arduinoboy throughput.** The link is a slow serial path; the app should rate-limit launches rather than assume MIDI-rate delivery. Unmeasured.
7. **Plugin path deferred**, so LED output in a DAW (routed back by the user) is unvalidated.

---

## 11. References

- [Launchpad Pro MK3 Programmer's Reference](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/LPP3_prog_ref_guide_200415.pdf) - the authority for §3
- [trash80/Arduinoboy README](https://github.com/trash80/Arduinoboy/blob/master/README.md) and [`Mode_LSDJ_Map.ino`](https://github.com/trash80/Arduinoboy/blob/master/Arduinoboy/Mode_LSDJ_Map.ino) - the authority for §2.1
- [Arduinoboy 1.3.0 changelog](https://github.com/trash80/Arduinoboy/commit/f154263f197eb3eea7948c5b56bac0acc06a7ee6) - LIVEMAP removal, SYNCMAP -> MI.MAP rename
- [jkotlinski/lsdj-doc `sync.tex`](https://github.com/jkotlinski/lsdj-doc/blob/master/sync.tex) - LSDj's own sync documentation
- [docs/lsdj.md](lsdj.md) - RetroPlug's LSDj domain reference, including the sync-mode table
- [spec/04-roles-dsp-kernel.md](../spec/04-roles-dsp-kernel.md) - the role model the controller registry mirrors

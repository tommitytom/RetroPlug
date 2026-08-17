# Engineering Report & Plan: Novation Launchpad support in RetroPlug

**Date:** 2026-08-12 · **Branch:** `feature/launchpad` · **Status:** M0-M5 built - a real Launchpad on a real
MIDI port drives a real cart. Only M6 (observed model, emulated-cart fidelity) is outstanding, and it is
optional.
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
| B8 | Is launching an **empty row** a stop? | **No** - and it is not ignored either. The cart keeps playing, and from stopped it **starts anyway**, on a row nobody asked for. So MI.MAP has no stop at all. |
| B9 | Where does an empty-row launch land, and what happens on advancing into one? | Two rules, pointing **opposite ways**. Advancing into an empty row **ends the song and wraps to the start** - it is not stepped over. Launching an empty row **scans BACK** to the nearest playable row at or before it. |

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
- **B9 corrected a rule M0 got wrong.** `predict.ts` scanned FORWARD past an empty row for the next
  populated one, so a song with a gap in the middle would walk over it into a second section a real cart
  can never reach. The probe song could not expose that (rows 0-2 populated, nothing above), which is why
  the differential test read 100% with the rule wrong - B9 uses a song with a hole, so the four candidate
  behaviours land on four different chain numbers. B6 had the corroborating evidence all along and it went
  unread: a row whose chain holds no playable phrase kept pu1 looping row 0 for 400 ticks. Fixed, and the
  differential is still 100.0%.
- **There is no stop, and the app must not pretend otherwise.** B5 killed the `0xFE` handshake as a stop
  and B8 killed the empty-row launch as one. Nothing in MI.MAP stops a cart except the host transport,
  which is not a pad.

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
  text-verifiable. The edge-button CC numbers started as the community mapping, marked unverified.
  **RESOLVED on hardware** (§3.6) - and the community mapping turned out to be wrong.

The API uses a **top-left origin** (y = 0 at the top) against a device that counts rows upwards from the
bottom; the flip lives in `padIndex`/`padAt` alone.

### 3.6 MEASURED: the edge buttons, and the grid orientation

Run against a real Pro MK3, `retroplug-cli launchpad-probe --sweep` lights one named button per second so
the physical button each addresses can be read off directly. Two results.

**The community mapping was wrong across the whole top row, shifted by two.** It assumed the up/down arrows
live there. They do not - they are the top of the LEFT column, and the top row's last two buttons were
missing from the map entirely.

| | CCs, left→right / top→bottom |
|---|---|
| Top row | `left` 91, `right` 92, `session` 93, `note` 94, `chord` 95, `custom` 96, `sequencer` 97, `projects` 98 |
| Logo | 99 (lightable, not pressable) |
| Left column | `up` 80, `down` 70, `clear` 60, `duplicate` 50, `quantise` 40, `fixedLength` 30, `play` 20, `record` 10 |
| Right column | `patterns` 89, `steps` 79, `patternSettings` 69, `velocity` 59, `probability` 49, `mutation` 39, `microStep` 29, `printToClip` 19 |
| Bottom row | `recordArm` 101, `mute` 102, `solo` 103, `volume` 104, `pan` 105, `sends` 106, `device` 107, `stopClip` 108 |

The positions were all right; only the NAMES were wrong. That mattered anyway: the LSDj app pages with
`up`/`down`, so under the old map it was driving the top row's ◀/▶ instead of the ▲/▼ beside the grid.
Renaming fixed the app's behaviour without touching the app. Pinned by tests in
[test/launchpad/protocol.test.ts](../packages/retroplug/test/launchpad/protocol.test.ts). `Shift` and
`Setup` (the two corners) are deliberately absent - whether they emit anything in Programmer mode is
untested, so they are not guessed at.

**The grid orientation is correct**: on hardware the ramp runs in palette order from index 1 at the
top-left, which is what a top-left origin should produce against a device that numbers rows from the bottom.

**And the device free-runs a MIDI clock** out of the same port. Harmless here (the probe counts rather than
prints it), but it is a third reason the Launchpad's port must be excluded from the shared musical stream in
M4: merged in, that clock would drive RetroPlug's transport.

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

### 6.1 RESOLVED: SysEx crosses the standalone seams

| Seam | Was | Now |
|---|---|---|
| RtMidi input | `ignoreTypes(true, ...)` dropped SysEx at the source | `ignoreTypes(false, false, true)` on the virtual + hardware inputs |
| Standalone MIDI in | `bytes.size() > 3` -> dropped | delivered, but NOT staged as musical MIDI (see below) |
| Standalone MIDI out | only 1..3 bytes sent | any length; `MidiIo::send` always took one |
| Plugin MIDI in | capped at 4 | **unchanged** - deferred with the plugin |
| Plugin MIDI out | capped at `MidiEvent::kDataSize` (4) | **unchanged** - DPF's `MidiEvent` has a `dataExt` pointer for this |
| Control plane -> engine | `DspCommand.stageMidi` holds 4 bytes | **unchanged**, and not on the path: the device link hands the audio thread its bytes directly |

Two things the widening had to be careful about, both of which bit in review:

- **A sysex message reaching the standalone is deliberately NOT staged as host MIDI.** The passthrough
  translators (mGB, LSDj `MidiPassthrough`) push every byte they are given straight down the cart's link
  port, so a control surface's handshake would arrive at the Game Boy as noise. Control-surface traffic has
  its own stream for exactly this reason.
- **The N8 forward excludes it too**, beside the existing realtime exclusion. A 538-byte bulk-LED message
  has no business being shovelled into an Everdrive's FIFO.

### 6.2 RESOLVED: the Launchpad has its own in/out pair

Standalone used to open **every** hardware input into one engine stream and mirror `send` to the single
selected output. `LaunchpadLink` now claims its pair exclusively, and `MidiIo` skips the claimed input
(`setReservedInput`, threaded through the pure `hardwarePortIndices` / `matchPortIndex` helpers so
`retroplug-midi-test` covers it).

The exclusion is load-bearing rather than tidy: a pad press is a NoteOn, and `midiMap` reads a NoteOn as a
row launch, so a surface sharing the musical stream fires every launch **twice** - once quantised by the app,
once raw. "All Devices" is the default, and it is exactly the case that would do it.

Two limits worth stating rather than burying. The Pro MK3's *other* USB interfaces (DAW, Custom) are not
excluded and stay open under "All Devices"; they are idle while the MIDI interface is in Programmer mode. And
a physical unplug is not detected - RtMidi will not say so without re-probing the port list - so the user
disconnects from the menu.

### 6.3 With a real Game Boy there is no system to attach to

Feature roles attach per-system by ROM identity. On the hardware path there is no `SystemBase` at all. Hence the app must be **project-scope** (like `midi-routing`), with the target chosen by config. Verified that project stages run with zero systems: [`dspKernel.ts:399-401`](../packages/retroplug/src/dspKernel.ts#L399-L401).

### 6.4 `stageMidiIn` is harness-only

[`BackendRpcRegistration.hpp:76`](../packages/native/src/host/rpc/BackendRpcRegistration.hpp#L76) mounts it on the **harness** facet, deliberately not exposed to the plugin or SDL channels. Noted for completeness; the DSP-role decision means we do not need it.

---

## 7. Architecture

### 7.1 Layers

As BUILT (M0-M2). Two things moved from the original sketch: the playback seam lives under `src/tracker/`
because risa is the same shape and should implement it without the app changing, and a controller app is a
plain function over a context rather than an object of callbacks - the same shape as a DSP role's
`SystemBehavior`, so decoding and LED flushing happen once in the session instead of in every app.

```
src/launchpad/            pure protocol, zero I/O, zero RetroPlug knowledge
  profile.ts              device table (Pro MK3 first): sysex id, grid math, edge buttons, capabilities
  protocol.ts             colours + message builders (mode, layout, bulk LED, inquiry, short form)
  surface.ts              per-LED style + dirty diffing -> minimal byte batches (bulk sysex vs 3-byte notes)
  decode.ts               incoming note/CC/aftertouch -> typed pad events

src/tracker/
  playbackModel.ts        the observed/predicted seam (§7.2), console-agnostic

src/controller/           host-agnostic app seam
  session.ts              ControllerSession: decode in, run the app, flush LEDs, own the device lifecycle
  registry.ts             name -> app module, mirroring RoleRegistry (and reusing RoleConfigSchema)
  trackerTarget.ts        the emulated-core/external-device seam (§7.3)
  apps/lsdjMidiMap.ts     the first consumer
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

**BUILT.** The difference between the two is only the `send` a caller injects; the app emits the ordinary
NoteOn/NoteOff the shipped `midiMap` role already consumes, so there is no second copy of the wire format
to keep in step. `midiMapRow` is exported from `dspRoles.ts` purely so the encoder can be round-tripped
against the decoder that actually ships.

The session also wraps the target so a launch reaches the cart **and** the predictor together: there is no
code path that sends a row to the cart without telling the model, so the LEDs cannot describe a position
the cart was never sent to.

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
| **M2** ✅ | `src/controller/` session + registry + `lsdjMidiMap` app against a fake device | no | DONE - 45 tests, and the two rules B8/B9 settled (§8.1) |
| **M3** ✅ | SysEx widening (RtMidi `ignoreTypes`, the two `sdl/main.cpp` caps) | yes | DONE - loopback carries a 299-byte bulk-LED SysEx intact, and a real Pro MK3 lights up |
| **M4** ✅ | Launchpad device link: own in/out pair, excluded from the engine stream, injected port factory, menu picker | yes | DONE - 14 hardware-free Catch2 cases over a fake port factory, plus 11 menu-seam cases (§8.3) |
| **M5** ✅ | Project-scope role wiring `TrackerTarget` (emulated core vs Arduinoboy MIDI out) | no | DONE - the whole chain runs per audio block (§8.2) |
| **M6** | `ctx.ram` / `ctx.sram` kernel seam + `ObservedLsdjModel` | yes | `test:native`; closes the differential loop from M0 |

M0-M2 and M5 delivered a fully testable, fully WIRED app with no device and no native change - the role runs
in the audio thread and its launches reach a cart. M3-M4 made a physical Launchpad work. M6 upgrades
emulated-cart fidelity and is genuinely optional.

### 8.1 BUILT: what M2 delivers, and the decisions it fixed

**The grid is LSDj's song screen, twice.** Four columns are the four channels and eight rows are eight song
rows; the right half continues the song, so all 64 pads show 16 consecutive rows - one LSDj song-screen
page.

```
      x=0 x=1 x=2 x=3 | x=4 x=5 x=6 x=7
      pu1 pu2 wav noi | pu1 pu2 wav noi
 y=0   r0  r0  r0  r0 |  r8  r8  r8  r8
 ...
 y=7   r7  r7  r7  r7 | r15 r15 r15 r15
```

A column **shows** a channel but cannot **select** one - MI.MAP launches a whole song row and every channel
jumps (B4), so pressing pu2 at row 5 does what pressing pu1 at row 5 does. Four columns earn half the grid
anyway, because the channels then advance independently as their chains end, and four columns are the only
way to see that.

| Decision | Value |
|---|---|
| Launch quantisation | `bar` (96 ticks) by default; `immediate` / `beat` / `rowEnd` in config. One bar is also exactly one LSDj phrase at the factory groove, so launches land where the music does. |
| Nothing playing | Quantisation is skipped and the launch is immediate - otherwise the first press waits for a boundary a stopped cart never reaches. |
| `rowEnd` | Fires on the model's next row change, so it works identically on the observed and predicted paths with no "ticks remaining" accessor only one of them could answer. |
| Scrolling | Auto-follows the first playing channel, page-aligned to 16 rows, so a diverging playhead cannot make the display slide about. |
| LEDs | off / dim green (content) / bright green (playhead) / pulsing yellow (cued). The cue lights all four cells of its row, because a launch is song-wide - and the device syncs pulse to MIDI beat clock itself, so "waiting for the bar" animates on the beat with no per-update LED traffic. |
| Stop | **None.** B5 and B8 between them establish that nothing in MI.MAP stops a cart, so there is no stop pad to offer. |

**The fake device is what makes "no hardware" mean something.** It decodes the host's own messages back
into per-LED state - short form and bulk SysEx alike - so a test asserts "pad (2,3) is bright green" and
fails when the *device* would end up wrong, not merely when our bytes change. It also round-trips M1's
encoder against itself.

**Paging depended on the unverified CCs, and they were wrong.** The `up` / `down` / `session` bindings were
pointing at the top row's ◀/▶ and at Chord. Confirming the map on hardware (§3.6) corrected the names, and
the app's bindings became right without the app changing - `up`/`down` now mean the ▲/▼ beside the grid, and
`session` means Session. Paging was never droppable: seeing rows you are *not* playing is the entire point
of a launcher.

### 8.2 BUILT: M5, the thing that actually runs

Everything above was a library nothing called. [`src/controllerRole.ts`](../packages/retroplug/src/controllerRole.ts)
is the `launchpad` project-scope DSP role that owns a `ControllerSession` and runs it once per audio block.

**Project scope needed three things it did not have.** A project stage got `(block, inboxes, config)` -
no state across blocks, no sinks - which made a role that OWNS something impossible. `ProjectBehavior` now
takes a `ProjectCtx` (state, `controllerIn`, `emitControllerOut`, `toSystem`, `emitMidiOut`), built once per
`setSystems` and pointing at things the kernel mutates in place, exactly as `SystemCtx` is. `midiRouting`
is the only other implementation, and its tests passing untouched is the guard on that change.

**`controllerIn` is a separate stream from `midiIn`, and that is load-bearing.** A pad press is a NoteOn and
`midiMap` reads a NoteOn as a row launch, so a surface sharing the musical stream would fire every launch
twice - once through the app's quantiser, once raw. A test pins exactly that difference.

**Neither seam needed native work.** Native builds the block-input object field by field, so an absent
`controllerIn` reads as the kernel's stable empty array; `emitControllerOut` is feature-gated on an unbound
global the way the tracing thunks already are. M4 fills both from a real device link.

**The song table, not the song.** A DSP role cannot decode a sav (that is M6), but the model only ever
consults ticks-per-(channel, row). `songRowTicks` is extracted from the model's constructor, the control
plane derives the table from the cart's live battery, and the role is handed it through config. It arrives
across a JSON boundary, so `normaliseRowTicks` coerces it to exactly 4 x 256 rather than letting a ragged
table read `undefined` for a missing row.

**Nothing derived reaches the `.rplg`.** The role is SYNTHESIZED in `kernelProjection` from an additive
`controller` project setting, exactly as `midi-routing` is, so the table lives only in the kernel push. Only
the user's own choices persist. Additive means no migration step, which the tolerance test now covers.

**Two allocation fixes**, since this is where the code first runs on the audio thread: `Surface.flush()`
built a scratch array and a result object even when nothing changed (the common case), and the session
rebuilt its ctx every update. Both are now built once and mutated. Message arrays still allocate when LEDs
genuinely change, which is unavoidable and rare.

**What the native test found.** [lsdj-launchpad.test.ts](../packages/retroplug/test-native/lsdj-launchpad.test.ts)
launches rows either side of the 128 boundary to confirm the two-channel split against LSDj rather than
against our own decoder - and rows 128/129 came back as "not playing". That was the runtime WRAM reader: it
applied the `> 0x7f` "parked at 0xFF" rule to the song row, which is right for chain and phrase indices but
wrong for a row, since LSDj has 256 of them. **Any song longer than 128 rows was misreported.** The
channel's own active flag already answers "is it playing", so the row is now read raw.

**Known gap.** The table is rebuilt on structure push, so edits made inside LSDj leave it stale until
something else re-pushes - the standing predictor limitation (risk 5), not a new one. And a second
`LsdjProbe` in one process behaved oddly under transport (the row jumped once at transport-start then
froze while steps kept advancing), so "what the cart does under the role's clock" stays covered by
`lsdj-midimap.test.ts` alone. That is a probe-lifecycle question rather than a controller one, and is
recorded rather than papered over.

### 8.3 BUILT: M3 + M4, the device

`controllerIn` was always empty and `emitControllerOut` went nowhere, because nothing owned a Launchpad.
[`LaunchpadLink`](../packages/native/src/host/launchpad/LaunchpadLink.hpp) now does, on its own in/out pair,
behind an injected `IMidiPort` so the whole lifecycle is tested with nothing plugged in.

**The two rings are deliberately different, because their producers are.** Device -> audio copies `MidiIo`'s
ring (a `std::vector` per slot): the producer is the MIDI backend's callback thread, where allocating is that
thread's own problem, and the audio thread MOVES the vector out - so the drain allocates nothing and reads
exactly like the `midi.poll` drain two lines above it. Audio -> main is a POD `SpscRing`, because there the
producer IS the audio thread. **LED traffic leaves on the main loop**, not the audio thread: a frame of
latency is invisible on a light, and it keeps a possible 538-byte bulk-SysEx USB write off the RT path
without adding a thread to own.

**Native never learns the protocol.** Programmer mode locks the device's front panel, so the message that
releases it has to survive a path where the audio thread is already stopped. TS hands down an opaque blob
(`exitToLiveMode`); the link replays it in `disconnect()` AND in its destructor, and never parses it. Both
paths are pinned by tests, because either one missing strands the user's hardware.

**The connect edge, and the gap M5 actually left.** M5's role built a session but never called
`session.connect()`, so Programmer mode was never entered. Sending it once at build time would not have
fixed it: the real sequence is "start RetroPlug, THEN plug the Launchpad in and hit Connect", by which point
the session is thousands of blocks old. Re-pushing the structure cannot serve either - project stage state
deliberately survives `setSystems` (§8.1's own guarantee, working against us here). So the block carries
**`controllerConnected`** beside `transport`, and the role acts on its rising edge. `connect()` invalidates
the shadow buffer as well as entering Programmer mode, which matters: entering the mode BLANKS the device,
so a reconnect that merely diffed would leave the grid dark. The role still runs `update()` while nothing is
attached, so the predictor keeps advancing and the first frame after plugging in is correct rather than
frozen at whenever the app started.

**The menu is gated on the SEAM, not on detecting a device - and that is the opposite of N8 Pro beside it.**
An Everdrive is identified by its USB VID:PID and there is no other way to attach one, so its submenu can
hide until one appears. A Launchpad also speaks TRS/DIN: short of USB ports it arrives through an ordinary
MIDI interface, on a port named after the INTERFACE, and nothing in that name says "Launchpad". A detection
gate would hide the only menu that could configure exactly that setup. So every hardware port is offered,
and `PRO_MK3_PORT_HINT` is demoted to picking the default and tagging a row "(detected)".

The submenu straddles two scopes on purpose, because setting the feature up needs both: ports + Connect are
HOST state (persisted natively in `launchpad.cfg`, shared by every project), while app / quantise / follow /
target / enable are PROJECT state, persisted in the `.rplg` as M5 already arranged. `setController` is the
one settings writer that re-pushes the kernel, since the role is synthesized at projection time.

### 8.4 FIXED: three things a real session found

M4 shipped and was played. Three faults, all of which needed hardware to see.

**The grid did not follow the song.** Editing a chain into a song row inside LSDj left the pads showing the
song as it was; toggling "Use in Project" off and on was the only way through. Two causes, stacked. The role
built its `ControllerSession` once and kept it forever, so a re-pushed `songRowTicks` never reached the
model - and menu knobs were equally dead, for the same reason. And nothing re-pushed anyway: the table is
derived at projection time from the cart's battery, and a cart being edited on its own screen emits no
signal.

So the role now watches its config, and treats two kinds of change differently. A new table is adopted **in
place** (`PredictedLsdjModel.setRowTicks`), so a chain added to row 12 lights up without the playhead
moving; anything structural rebuilds the session, **carrying the predictor across** so the song does not
restart under the player. And `ProjectStore.refreshControllerSong` polls the battery on the existing
song-watch timer, gated on a signature over only the four regions row timing depends on (chain assignments,
chain phrases, chain allocations, groove 0) - so an instrument tweak or a rename costs nothing, and the sav
decode happens only once the answer has actually moved.

**A cart that leaves MI.MAP goes deaf, silently.** Measured in
[test-native/lsdj-sync-toggle](../packages/retroplug/test-native/lsdj-sync-toggle.test.ts): LSDj **refuses to
change SYNC while it is playing** (editable when idle, editable when merely being clocked, not while
playing), so reaching for the setting mid-session leaves it wherever the first press landed with no way
back. A cart in MI.OUT then ignores row launches completely while still playing and still stepping to the
host clock - nothing looks wrong, the pads just stop working. Not our bug to fix, but very much ours to
report: the poll reads the SYNC byte and the Launchpad submenu says
`Cart SYNC is MIDIOUT, not MI.MAP - pads will do nothing`.

That file also carries the instrument that got there: `LsdjProbe` gained button presses, chords, screen
navigation and `autoClock` (a clock running underneath every rendered frame, so "the player did this while
it was syncing" is one flag rather than hand-interleaved bytes).

**Pressing START on the cart desynced the display.** LSDj starts at whatever row ITS cursor is on; the
predictor carried on from where it thought it was, and the lit playhead stayed wrong until the next pad
press put both back in step. This is the re-anchor of section 4.4, applied at the one moment it is cheap and
unambiguous: the **not-playing -> playing edge**, which the control plane sees through the WRAM reader it
already owns. The rows ride to the audio thread as role config with a sequence number, and
`PredictedLsdjModel.anchorTo` applies them as a correction rather than an event - no tick consumed, channels
that already agree left alone, and per channel because they genuinely diverge (B4). Mid-song correction is
still M6; a start edge is not.

### 8.5 FIXED: the two sync settings never had to agree, and nothing made them

The fault the first session actually hit hardest, found on the second. `lsdj-sync` defaults to **`midiSync`**,
while the MI.MAP app's launches are NoteOns that only the **`midiMap`** translator turns into row bytes. So
enabling a Launchpad on a fresh cart produced a cart being clocked for a mode it was not in, launches that
went nowhere, and LSDj sitting on "WAIT" - with nothing anywhere saying why.

`controllerSyncOverride` now decides the cart's sync mode from the controller, at **projection** time rather
than by editing the project. That placement is the design: nothing reaches the `.rplg`, turning the
controller off restores whatever the user had, and the two settings cannot drift apart because only one of
them is now authoritative.

It has two answers, and the second matters as much as the first:

| The cart's OWN SYNC | What we run | Why |
|---|---|---|
| MI.MAP, or unreadable | `midiMap` | What the app needs. Unreadable means a battery not yet published, and refusing to drive it would be a worse guess than trying. |
| anything else | `off` | A cart in LSDJ (master) mode drives the link ITSELF, so our clock collides with its own and LSDj reports TOO BUSY and stops rendering properly. Better to send a cart that is not listening nothing at all. |

Shown rather than applied silently: the submenu carries a row saying which of those is in force, because the
LSDj submenu goes on reporting the user's own stored setting.

Guarded where it counts - against a real cart, through the REAL projection rather than a hand-written
pipeline ([test-native/lsdj-launchpad](../packages/retroplug/test-native/lsdj-launchpad.test.ts)): a project
whose cart is in the default `midiSync` launches row 42; the same project with no controller ignores the
same bytes (the bug, pinned as the control); and a cart whose own SYNC says LSDJ is left alone.

---

## 9. Testing

BUILT (`pnpm test`, no device, no native build):

- **Protocol** ✅ golden byte vectors from the manual's own worked examples - grid anchors, both lighting
  forms, bulk-LED batching, mode entry/exit, device inquiry. `test/launchpad/`, 37 tests.
- **Predictor** ✅ two halves. Deterministic arithmetic in `test/lsdj/playback.test.ts`; agreement with a
  REAL cart in `test-native/lsdj-playback-differential.test.ts`, which sweeps alignment offsets rather than
  assuming one. 100.0% over 2400 comparisons.
- **Controller** ✅ `test/controller/`, 45 tests against a fake device that decodes the host's own messages
  back into per-LED state, so an assertion is "pad (2,3) is bright green" and fails when the DEVICE would
  be wrong. Covers the session lifecycle, tick-driving, launch mirroring, the target encoding round-tripped
  through `midiMapRow`, the registry's config tolerance, and the app's layout, LEDs, quantisation, follow
  and paging - ending with an end-to-end run over several song wraps.
- **Semantics** ✅ `test-native/lsdj-playback-probe.test.ts` (B0-B9) measures what a real cart does rather
  than inferring it. Assertions there guard the instrument; the semantics are locked in by the differential
  and unit tests once a model exists to hold them to.

- **Device link** ✅ `retroplug-launchpad-test` (Catch2, 14 cases / 66 assertions), over a fake `IMidiPort`:
  connect / refusal / disconnect, the farewell on BOTH disconnect and destruct and across a reconnect, a
  received message reaching the audio-thread drain, LED traffic leaving on `pump()` rather than on push, an
  oversized message dropped rather than overrunning its slot, and `LaunchpadHost`'s `launchpad.cfg`
  round-trip + reserved-port reporting. No rtmidi, no MIDI system, no hardware.
- **Port exclusion** ✅ `retroplug-midi-test` gained the reserved-port cases: a claimed port drops out of
  "All Devices" and cannot be selected explicitly either.
- **The connect edge** ✅ `test/controller/role.test.ts` drives the real role through a kernel: no hello
  while nothing is attached, hello FIRST on the block a device appears, not re-sent while it stays, a full
  repaint on reconnect (not a diff), and the predictor still advancing while disconnected.
- **The menu + seam** ✅ `test/menu/launchpad.test.ts`, 11 cases: the seam gate, the unfiltered port list,
  the hint tag stripped back off before a name goes to native, Connect resolving the hinted default (and
  declining to invent one when nothing is hinted), the farewell landing before anything can connect, the
  status line, and the project rows re-pushing the kernel.
- **Hardware** ✅ `retroplug-cli launchpad-probe` on a real Pro MK3 confirmed Programmer-mode entry, LED
  batching and the edge-button CCs (§3.6 - which corrected them).

OUTSTANDING:

- **Manual, hardware** (M4): standalone + a real Pro MK3 + an LSDj cart in MI.MAP - press a pad, hear the row
  launch on the bar, watch the playhead track it; then quit and confirm the device's own Settings menu opens
  again. The probe proved the protocol against the hardware and the fake device proves the lifecycle; what is
  left is the two ends meeting.

---

## 10. Risks and open questions

1. **Predictor drift is undetectable on hardware** (§4.3). Mitigated by the M0 differential test and the optional re-anchor (§4.4), not eliminated. This is inherent to MI.MAP being one-directional.
2. **Initial state on connect is unknown** on hardware. Likely answer: define a known starting state by commanding transport ourselves rather than trying to infer one.
3. ~~**Launch quantisation is now our job**~~ RESOLVED in M2: quantise to the bar by default, with
   `immediate` / `beat` / `rowEnd` in config (§8.1).
4. **Leaving the device in Programmer mode** after a crash locks the user out of its Settings menu until a power cycle. Best-effort restore on exit; document the recovery.
5. **A user-supplied `.sav` may not match what is actually on the cart.** The predictor is only as good as the song file it is given. Consider surfacing an explicit "song loaded for LED feedback" state rather than failing silently.
6. **Arduinoboy throughput.** The link is a slow serial path; the app should rate-limit launches rather than assume MIDI-rate delivery. Unmeasured. Quantised launch happens to bound this - at most one launch per bar - but `immediate` does not.
7. **No stop.** Nothing in MI.MAP can stop a cart (B5, B8), so a performer's only stop is the host
   transport. This is the protocol's limitation rather than ours, and the app does not paper over it.
8. **The predictor's row rules were wrong once already** (B9), and the differential test did not catch it
   because the probe song had no gap. Other rules may have the same shape of blind spot - notably H hop
   commands and phrase-level `G` groove changes, both still unmodelled and both able to change row
   duration. Worth a probe song built to expose them before anyone plays a real set on this.
9. **Plugin path deferred**, so LED output in a DAW (routed back by the user) is unvalidated.

---

## 11. References

- [Launchpad Pro MK3 Programmer's Reference](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/LPP3_prog_ref_guide_200415.pdf) - the authority for §3
- [trash80/Arduinoboy README](https://github.com/trash80/Arduinoboy/blob/master/README.md) and [`Mode_LSDJ_Map.ino`](https://github.com/trash80/Arduinoboy/blob/master/Arduinoboy/Mode_LSDJ_Map.ino) - the authority for §2.1
- [Arduinoboy 1.3.0 changelog](https://github.com/trash80/Arduinoboy/commit/f154263f197eb3eea7948c5b56bac0acc06a7ee6) - LIVEMAP removal, SYNCMAP -> MI.MAP rename
- [jkotlinski/lsdj-doc `sync.tex`](https://github.com/jkotlinski/lsdj-doc/blob/master/sync.tex) - LSDj's own sync documentation
- [docs/lsdj.md](lsdj.md) - RetroPlug's LSDj domain reference, including the sync-mode table
- [spec/04-roles-dsp-kernel.md](../spec/04-roles-dsp-kernel.md) - the role model the controller registry mirrors

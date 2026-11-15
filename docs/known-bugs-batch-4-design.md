# Known bugs, batch 4: the tracker marker, quantise, the JACK handoff, the handover and the N8 menu

The five entries left in [spec/07](../spec/07-remaining-work.md)'s known bugs, each checked against the code at
dda4d4961 (the native research ledger is `.superpowers/kb4/native-ledger.md`, gitignored):

| # | Bug | Where | Found |
|---|-----|-------|-------|
| 1 | A project storing two node-less tracker markers takes the first in product order | `SystemsStore`, `trackerRegistry` | Known bugs batch 2 |
| 2 | A `bar` or `beat` quantise counts the host's bars, not the cart's | `lsdj-launchpad`, `launchpad` | Known bugs batch 2 |
| 3 | The JACK standalone hands the engine back on JACK's thread | `PluginDSP` | after C5 |
| 4 | A cart-node handover can cut a key the new node strikes | `n8` package | C5 |
| 5 | MIDI streamed to a cart at its menu reaches the menu's command parser | `n8` package | C5 |

User decisions (2026-11-14 and 15):
- **Quantise:** offer both clocks. `bar` and `beat` count the cart's, and new `hostBar` and `hostBeat` count the DAW's
  (section 2).
- **The N8 menu:** guarded headlessly, with no hardware run. The link stops streaming to a cart at its menu, and the
  cart node's `connected` reads 0 there. A framed link that never goes quiet is read anyway after about 1 s (section 5).
- **The rest:** sections 1 and 3 are as presented and approved; sections 3 to 5 take the ledger's recommendations.

## 1. A cart's tracker is the one its ROM earns

The `risa` and `bliptoaster` markers have no protocol node, so both count as placed whenever they are stored. A NES
project stored with risa whose ROM file is now BlipToaster carries both. `resolveTracker`
([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)) then takes the first in product order, whatever
the ROM is.

Every load already asks the ROM providers which roles today's file earns (`suggested` in `SystemsStore`'s load, which
`withMissingFeatureRoles` appends from), and `suggestedFirst` already moves a suggested role ahead of a stale one that
collides with it.

**Fix.**
- **Load:** tracker marker kinds, the `markerRole` of every entry in `TRACKER_INTEGRATIONS`, collide with each other in
  `suggestedFirst`. A marker the ROM earns moves ahead of a stored marker it does not.
- **Resolve:** `resolveTracker` takes the first placed marker in the system's own role order, not in product order.
- **Nothing is deleted:** a stale `risa` and its `risa-assets` overrides stay stored, so a ROM swapped back flips the
  order and brings them back. An embedded or unreadable ROM earns nothing, so the stored order stands.
- **What is unchanged:** a marker with a node (`lsdj-sync`, `sms-sync`) is already decided by the pipeline and is
  unaffected.

**Tests** (`pnpm test`, the systems store): a project storing `[risa, risa-assets, bliptoaster]` over a BlipToaster
ROM resolves to BlipToaster; the reverse store over a risa ROM resolves to risa; both over an unreadable path keep the
stored order. Each fails today on one of the two directions.

## 2. Two clocks for a launch's quantise

MI.MAP's `beat` and `bar` snap a launch to 24 and 96 ticks of the DAW's clock
([lsdjMidiMap.ts](../packages/lsdj-launchpad/ts/lsdjMidiMap.ts) `boundaryTick`). The cart is clocked at
`24 / tickDivisor` ticks a quarter, so at a divisor of 2 an LSDj phrase lasts two DAW bars and a `bar` launch can land
mid-phrase. That contradicts the constant's own comment ("exactly one 16-step LSDj phrase").

**Fix.**
- **The cart's clock:** `beat` and `bar` snap to `24 x divisor` and `96 x divisor` DAW ticks. Their phase is the cart's
  own clock, which the session walks from ppq 0.
- **The DAW's clock:** new values `hostBeat` and `hostBar` keep 24 and 96, labelled "Next host beat" and "Next host
  bar". `QUANTISE_VALUES` gains them after `bar`, so the settings row cycles immediate, beat, bar, host beat, host bar,
  row end.
- **Plumbing:** the session passes its `tickDivisor` (already in its settings, `controllerSession.ts`) to the app on the
  controller context.
- **Saved projects:** the default stays `bar`. No migration: the stored strings keep their names, and a project at
  divisor 1, the default, behaves exactly as before.

**Tests** (`pnpm test`, `lsdjMidiMap.test.ts` and the session tests): at divisors 1, 2 and 3, a `bar` launch fires at
the cart's phrase boundary and a `hostBar` launch at the DAW's bar. The same holds for `beat` and `hostBeat`, and the
values round-trip through the app's schema. The divisor 2 and 3 `bar` cases fail today.

## 3. The JACK handoff stays on the control thread

DPF's JACK host runs `deactivate()`, the change and `activate()` on JACK's own thread for a buffer-size or sample-rate
change (`DistrhoPluginJACK.cpp:352-360` -> `DistrhoPluginInternal.hpp:1215-1253`). JACK2 and PipeWire-JACK call these
callbacks on a notification thread and JACK1 on its process thread; all three keep the process callback from running
during them (ledger Q1.1). PipeWire changes the buffer size whenever another client asks for a different latency, so on
a PipeWire desktop this is routine, not the rare manual case spec/07 says.

On that thread, `PluginDSP` ([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), `activate` and `deactivate`)
does three unsafe things:
- **It flips the engine's ownership** and drains the command ring and the release ring, while the editor's `uiIdle`
  reclaims on the main thread every frame.
- **It runs the script host's GC and free**, so two threads can be in `JS_RunGC` or `freePack` at once.
- **It runs `updateLatency` and `updateParameterMap`,** which call into the shared QuickJS context the main thread
  pumps every frame. spec/07's entry missed this one.

Nothing can grow the plan or change the rate while the audio thread owns the engine. `GraphHost::reactivate` refuses
(`GraphHost.cpp:366`), and no ring command carries a size or a rate (ledger Q1.3).

**Fix**, all inside `PluginDSP`, in two tiers:
- **The thread check:** `PluginDSP` records its control thread at construction; DPF builds the plugin and runs its
  first `activate` on the main thread. `deactivate`, `bufferSizeChanged`, `activate` and `sampleRateChanged` called on
  any other thread take the deferred path.
- **Tier 1: a size that fits the plan is a no-op.** A buffer-size change within the plan's maximum (its floor is
  `kDefaultMaxBlockFrames`, 8192) touches nothing on the foreign thread: no flip, drain, reclaim or JS. The engine
  never leaves the audio thread, and the next `run()` drains as usual. This is every PipeWire quantum change.
- **Tier 2: a rate change or an over-plan size parks `run()`.**
  - **Parked:** a parked `run()` writes silence and copies its block's MIDI into a fixed plugin-side buffer. It never
    allocates, and an overflow is dropped and counted. It stores `Parked` and returns without touching the engine.
  - **The handoff:** the control thread's idle tick (the `reclaimReleased` hook `uiIdle` already calls every frame)
    sees `Parked` and does the whole quiescent handoff there. It takes the engine back, drains, reclaims, applies the
    rate and the size, and hands the engine back. Then it runs `updateLatency` and `updateParameterMap` and stores
    `Running`.
  - **Resuming:** the first resumed block replays the held MIDI at frame 0, so a note-off is not lost.
  - **The handshake:** a release/acquire flag. `run()` stores `Parked` only on entry to a later block, so the handoff
    never relies on JACK's overlap guarantee.
- **A foreign deactivate with no activate after it** (a VST2 `SUSPEND` off the main thread) parks too, and the control
  thread's tick may then take the engine back, since DPF guarantees no `run()` after a deactivate. One mutex, which
  `run()` never takes, serialises the foreign lifecycle calls against that tick.
- **No tick at all:** a VST2 host that changes the rate off its main thread with the editor closed has no idle tick to
  resume it. `getState` and `setState` also apply a pending park, and a park longer than a second is logged once.

Costs: a rate change or an over-plan size is a short silence, about one UI frame, like a transport pause. The engine's
ppq does not advance while parked.

**Tests.**
- **Catch2, red then green:** the handoff policy is extracted into a unit `PluginDSP` delegates to, and a case drives
  its lifecycle calls from a `std::thread`. It asserts no engine call ran on that thread, a fitting size parks nothing,
  a rate change parks, and the idle tick resumes with the new rate and the held MIDI first.
- **TSan:** an engine-level case shows the race today and its absence after.
- **By hand, under the device guard:** the JACK standalone (`retroplug-jack`, `tools/run-standalone.sh`) on jackd's
  dummy backend, with `jack_bufsize 2048` and back mid-run while the editor edits.

## 4. A handover releases only the old node's notes

When a cart node is destroyed, its held notes are released ([N8Node.cpp](../packages/n8/native/src/n8/N8Node.cpp)
destroy, `releaseNotes`). [N8Link](../packages/n8/native/src/n8/desktop/N8Link.cpp) takes the release's turn as
everything pushed so far (`releaseAfter_ = chunksPushed_`), the new node's first blocks among them. So a note the new
node starts on a key the old one held is cut by the old one's release.

A per-key holder count would be wrong, because a node forwards the DAW's duplicate note-ons (`N8Node.cpp:167-181`), so
a count would rise and never come back down.

**Fix.**
- **The interface:** `CartLink` gains `pushMark()`, the link's push count, and a `releaseNotes(notes, mark)` overload,
  both defaulted, so the Pico's link and the stub are unchanged.
- **The node:** `rp.n8.nes` records the mark after each of its own pushes and hands it over from its destroy.
- **The release's turn:** `N8Link` waits behind that mark, not behind every push.
- **Skipping the new node's keys:** the serial thread keeps, per channel and key, the index of the last chunk whose
  note-on it wrote, and a released key whose last note-on is at or past the mark is skipped: the new node holds it.
- **Coverage:** this works the same on the raw and framed paths, and also stops a release landing inside a SysEx split
  across chunks.

**Tests** (`retroplug-n8-test`): an `N8CartJobs.test.cpp` `[cart-release]` case where the new node strikes a key the old
one holds, then the release runs: the key is not released, and the old node's other keys are. An `N8Adapters` case
(`retroplug-engine-graph-test`) runs the same over FakeN8 behind a real Engine. Both fail today.

## 5. A cart at its menu is not streamed to

The N8 menu reads its commands over the same FIFO a game reads MIDI from, so streamed MIDI can be read as a command.
For example, the data pair `0x2A 0x73` (`*s`) asks it to start an app. Two cases reach it: raw MIDI after a connect at
the menu, and framed MIDI after a console reset, where the stale window is re-sent every 100 ms. The same stream into a
menu also keeps a job's menu probe from finding a quiet turn past about 190 messages a second.

Where the cart runs is readable without touching its bus: the FPGA config block's map_idx is 255 at the menu
(`N8Host.cpp` `kMenuMapper`, `runningMapper`), a system-region read.

**Fix.**
- **The at-menu state:** `N8Link` keeps one, seeded at connect by `N8Host` (`runningMapper == kMenuMapper`).
- **When it is re-read:** between chunks, with a short (about 100 ms) read timeout:
  - every 250 ms while raw, while framed and idle, and while at the menu;
  - at once when a framed link with frames outstanding goes silent (the console-reset case). New frames and re-sends
    are held until the read answers. A back-channel that never goes quiet is read anyway after 1 s, the job probe's
    own deadline.
- **At the menu the link drops, it does not hold:** `push` answers false and counts, and the ring, the reliable window
  and any pending note releases are discarded. Nothing sounds at a menu.
- **What the graph sees:** the cart node's `connected` output reads 0, so owed note-offs follow the existing
  disconnected rule. Settings > N8 shows "at menu".
- **Jobs and leaving the menu:** jobs still run at the menu, and leaving it re-runs the link probe.
- **The starved probe:** with no stream into a menu, a job's menu probe always finds a quiet turn.

**Tests** (`retroplug-n8-test`, FakeN8):
- **Two red cases, each failing today:** FakeN8 at its menu treats a pushed `90 2A 73` as `*s` and boots, raw after a
  connect and framed after a reset.
- **Green:** after the fix nothing reaches the menu, and the pushes are counted dropped. A boot to a game re-enables
  the stream and `connected` reads 1 again. A job submitted under a 200-message-a-second stream into the menu is
  accepted.
- **Rewrites:** the existing cases that stream MIDI at the menu are rewritten to boot a game first.

**What stays in spec/07:** a single line under hardware: confirm on a cart that the menu writes nothing unprompted on
the back-channel, which the quiet rule and the guard's read both assume.

## 6. Plan, execution and acceptance

**Tasks**, each test-first, in this order:
1. the tracker marker;
2. quantise;
3. the JACK handoff;
4. the handover;
5. the menu guard, after 4, since both change `N8Link`;
6. docs:
   - spec/07 loses entries 1 to 4 and their checklist lines, and entry 5 becomes the one hardware line;
   - spec/13: the controller role's quantise values, the N8 bridge's menu guard and `connected`, the plugin lifecycle's
     thread rule;
   - spec/06's line on node-less markers' product order;
   - the comment above `activate` in `PluginDSP.cpp`;
   - this design's as-built and execution sections.

**Process:** as batch 3, with pi:
- **Plan:** a plan generated by DeepSeek and validated on the tree red then green, its every block replayed from its
  text, then reviewed by Opus.
- **Implementers:** Flash-Next at xhigh, a task each, with the spec check being tree identity with the validated diff.
- **Code reviews:** DeepSeek for tasks 1 and 2, Opus for tasks 3 to 5, then follow-up passes.
- **Final review:** Opus.
- **Escalation:** a DeepSeek role that fails Opus review twice moves to Opus.

Work files are in `.superpowers/kb4`, `kb4plan` and `kb4exec`.

**Acceptance**, on the tip:
- **The serial kit:** strict parity with fixtures and `RP_FAIL_ON_SKIP=1` (no parity case moves); TSan and ASan;
  BlipToaster and NESVJ against the rebuilt CLI; the guarded SDL legs, `sdl:controller` among them since the launch
  quantise changes.
- **Then the parallel kit.**
- **The by-hand JACK leg** of section 3.
- **Devices:** no run opens an audio, MIDI or serial device outside the guard and the fakes.
- **Hardware:** no run touches the N8 or the NES.

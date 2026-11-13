# 07 - Remaining work

This file lists open work only. The commit that finishes an item deletes it, its checklist line and its detail entry
together: never tick an item and leave it. The design docs and the git log are the history, and nothing here records
what was done. Each entry says what is missing and why it matters, and links the design section that found or
deferred it; another spec doc names a gap in a line and links its entry here. Deferred, last, records intent rather
than work.

## Checklist

**Before merge**
- [ ] [The Launchpad real-device checklist](#the-launchpad-real-device-checklist)
- [ ] [The Launchpad's Windows items on a real device](#the-launchpads-windows-items-on-a-real-device)
- [ ] [The overlay strip placement on a real host](#the-overlay-strip-placement-on-a-real-host)
- [ ] [The `RP_NATIVE_USB` box image on hardware](#the-rp_native_usb-box-image-on-hardware)
- [ ] [A `release.yml` run](#a-releaseyml-run)

**Package split**
- [ ] [A real component view on a host](#a-real-component-view-on-a-host)
- [ ] [A UI kit compile](#a-ui-kit-compile)
- [ ] [Gamepad axis tokens as action bindings](#gamepad-axis-tokens-as-action-bindings)
- [ ] [Downloadable packs](#downloadable-packs)
- [ ] [The app loads no script pack](#the-app-loads-no-script-pack)
- [ ] [The template gaps a second pack meets](#the-template-gaps-a-second-pack-meets)
- [ ] [The N8 provider named by plugin.cmake and the test host](#the-n8-provider-named-by-plugincmake-and-the-test-host)
- [ ] [The view reopen key's width](#the-view-reopen-keys-width)
- [ ] [One owner for the `rp.midi` family](#one-owner-for-the-rpmidi-family)
- [ ] [A config section paired with its UI service](#a-config-section-paired-with-its-ui-service)
- [ ] [F5a's other leftovers](#f5as-other-leftovers)

**Known bugs**
- [ ] [A tracker marker with no protocol node](#a-tracker-marker-with-no-protocol-node)
- [ ] [The JACK standalone's control handoff](#the-jack-standalones-control-handoff)
- [ ] [A cart-node handover can cut a key](#a-cart-node-handover-can-cut-a-key)
- [ ] [The N8 menu, unverified on a cart](#the-n8-menu-unverified-on-a-cart)
- [ ] [The stripper bundle outside the repository](#the-stripper-bundle-outside-the-repository)
- [ ] [A bar or beat quantise counts host bars](#a-bar-or-beat-quantise-counts-host-bars)
- [ ] [MidiStream::send blocks the audio callback](#midistreamsend-blocks-the-audio-callback)
- [ ] [A MIDI pick dirties the untitled project](#a-midi-pick-dirties-the-untitled-project)
- [ ] [Settings > MIDI misses a claim made while open](#settings--midi-misses-a-claim-made-while-open)
- [ ] [The audio draft's sibling-backend cache](#the-audio-drafts-sibling-backend-cache)
- [ ] [An Apply past a read-only config](#an-apply-past-a-read-only-config)
- [ ] [A normal CLI exit skips the teardown](#a-normal-cli-exit-skips-the-teardown)
- [ ] [The test host drops an uncaught error](#the-test-host-drops-an-uncaught-error)
- [ ] [An SMS stage lands on player 2](#an-sms-stage-lands-on-player-2)
- [ ] [BlastEm's remaining process-exits](#blastems-remaining-process-exits)

**Engine**
- [ ] [DAW automation of node params](#daw-automation-of-node-params)
- [ ] [P3: cores borrow their pinned ROM](#p3-cores-borrow-their-pinned-rom)
- [ ] [P4: the snapshot registry's double copy](#p4-the-snapshot-registrys-double-copy)
- [ ] [P7: the rest of the capability split](#p7-the-rest-of-the-capability-split)
- [ ] [Allocations left on the block thread](#allocations-left-on-the-block-thread)
- [ ] [Input-drop counters beside the graph's](#input-drop-counters-beside-the-graphs)
- [ ] [The host MIDI-in staging](#the-host-midi-in-staging)
- [ ] [Script packs: isolation, reloads, refusals, memory](#script-packs-isolation-reloads-refusals-memory)
- [ ] [Dead code and the kernel test names](#dead-code-and-the-kernel-test-names)
- [ ] [The tap's mode bound](#the-taps-mode-bound)
- [ ] [A narrow host still arms the tap](#a-narrow-host-still-arms-the-tap)
- [ ] [Threaded engine cases are smoke runs](#threaded-engine-cases-are-smoke-runs)
- [ ] [A plugin with its editor closed has no idle reclaim](#a-plugin-with-its-editor-closed-has-no-idle-reclaim)
- [ ] [The script node Block has no resource accessor](#the-script-node-block-has-no-resource-accessor)
- [ ] [The command ring and the adoption wait](#the-command-ring-and-the-adoption-wait)
- [ ] [Serial capacities past large blocks](#serial-capacities-past-large-blocks)
- [ ] [A feedback source's capacity change](#a-feedback-sources-capacity-change)
- [ ] [A custom reflector in the device decode](#a-custom-reflector-in-the-device-decode)

**Renders and kits**
- [ ] [A plugin kit row](#a-plugin-kit-row)
- [ ] [The kit patcher's latency budget](#the-kit-patchers-latency-budget)
- [ ] [CI sees 5 of 13 render cases](#ci-sees-5-of-13-render-cases)
- [ ] [A GBA render case](#a-gba-render-case)
- [ ] [A sanitizer leg for renders](#a-sanitizer-leg-for-renders)
- [ ] [Render residuals](#render-residuals)
- [ ] [The behavioural render tests run in-process](#the-behavioural-render-tests-run-in-process)
- [ ] [The statics patch's camera half has no test](#the-statics-patchs-camera-half-has-no-test)

**Pico**
- [ ] [Audio on the box](#audio-on-the-box)
- [ ] [Live plan upload](#live-plan-upload)
- [ ] [A controller path on the box](#a-controller-path-on-the-box)
- [ ] [A 32-bit runner in the build](#a-32-bit-runner-in-the-build)
- [ ] [The box's stats print can stall a message](#the-boxs-stats-print-can-stall-a-message)
- [ ] [A SysEx past 3 bytes allocates](#a-sysex-past-3-bytes-allocates)

**Feature gaps and SDL**
- [ ] [Raw LSDj Keyboard sync mode](#raw-lsdj-keyboard-sync-mode)
- [ ] [NES per-mapper expansion sub-channels](#nes-per-mapper-expansion-sub-channels)
- [ ] [The standalone reopens the last project](#the-standalone-reopens-the-last-project)
- [ ] [Live memory-region subscription](#live-memory-region-subscription)
- [ ] [MIDI out past 4 bytes](#midi-out-past-4-bytes)
- [ ] [Mesen .mlb labels in the CLI debugger](#mesen-mlb-labels-in-the-cli-debugger)
- [ ] [SDL host MIDI out at block time](#sdl-host-midi-out-at-block-time)
- [ ] [SDL MIDI-in at frame 0](#sdl-midi-in-at-frame-0)
- [ ] [Per-device MIDI routing](#per-device-midi-routing)
- [ ] [The handheld-perf rebase and the muOS run](#the-handheld-perf-rebase-and-the-muos-run)
- [ ] [The n8-bridge sessions on a real N8](#the-n8-bridge-sessions-on-a-real-n8)
- [ ] [SDL on a real Windows machine](#sdl-on-a-real-windows-machine)
- [ ] [macOS on a real machine](#macos-on-a-real-machine)
- [ ] [Device hotplug and MIDI clock phase lock](#device-hotplug-and-midi-clock-phase-lock)
- [ ] [A really removed audio device](#a-really-removed-audio-device)
- [ ] [SDL's PipeWire node for a wide request](#sdls-pipewire-node-for-a-wide-request)
- [ ] [The PipeWire data loop's RT pin](#the-pipewire-data-loops-rt-pin)
- [ ] [No DAW check records LSDj's MIDI out](#no-daw-check-records-lsdjs-midi-out)
- [ ] [A plain-ROM controller target](#a-plain-rom-controller-target)
- [ ] [DAW MIDI to a cart with no NES system](#daw-midi-to-a-cart-with-no-nes-system)
- [ ] [genmddj's tracker integration and MIDI takeover (spec 2)](#genmddjs-tracker-integration-and-midi-takeover-spec-2)
- [ ] [Mega Drive audio fidelity (spec 3)](#mega-drive-audio-fidelity-spec-3)
- [ ] [The A53's Mega Drive speed](#the-a53s-mega-drive-speed)
- [ ] [The bindings menu's Mega Drive extras](#the-bindings-menus-mega-drive-extras)

## Before merge

What `feature/modularize` owes before it merges to main.

### The Launchpad real-device checklist

Design section 8's ten steps, about ten minutes on the desk with the Pro MK3 and LSDj in a ROM, have never run; they
cover the claim and the scan on a real device, which the ALSA fakes stand in for. ([F3
design](../docs/modular-graph-phase-f3-design.md) section 8.)

### The Launchpad's Windows items on a real device

CI's windows job builds and unit-tests the pack but touches no device, so the Windows items are owed on hardware: an
input's exclusivity (a scan's held edge suspends the shared stream's inputs), the unshared `CreateFile` claim (a second
process refused), and the writer thread under WinMM (a bulk repaint does not freeze the UI frame). The held edge is
untested in place: `suspend_hardware_inputs` is a real lever under `_WIN32` alone (`scanSuspendsInputs` in
[PortFilter.hpp](../packages/midi/native/src/midi/PortFilter.hpp); an ALSA sequencer input and a CoreMIDI endpoint are
shared), and the ALSA legs assert only that it does nothing there. `ControllerLink::disconnect()` under WinMM belongs to
the same run: a claim, a release or the provider's stop runs it on the UI thread, which joins a writer that may be
mid-drain of up to 32 messages from the send ring plus the 16 the UI queue holds (about 170 ms each under WinMM), then
flushes the ring and writes the close bytes itself. A swap cannot dodge the wait, since the old pair's ports must be
closed before the new pair opens; measure it with a full repaint queued and, if it is too long, hand the farewell to the
writer. ([F3 design](../docs/modular-graph-phase-f3-design.md) sections 8 and 12.)

### The overlay strip placement on a real host

F4c's overlay strip (edge E4: its z-order against the focus border and the render badge) has been seen only in the
headless UI host. ([F4c design](../docs/modular-graph-phase-f4c-design.md) section 13.)

### The `RP_NATIVE_USB` box image on hardware

`RP_NATIVE_USB` has never run on hardware: the image (af36f223 at E4) is still unflashed, built under the link trap
only. The Pico-PIO-USB image is the one the rig runs. ([E3 design](../docs/modular-graph-phase-e3-design.md) section
12.8, [E4 design](../docs/modular-graph-phase-e4-design.md) sections 11.7 and 11.8.)

### A `release.yml` run

`release.yml` last ran on 2026-08-07 (v0.7.4), before F2, so its Windows job has never built `RETROPLUG_MESEN_LTO=ON`
with the three Mesen console options off, and no release job has built the branch. ([F2
design](../docs/modular-graph-phase-f2-design.md) section 11.)

## Package split

### A real component view on a host

The `component` view kind and its fixture ship, but no real `component` view runs on a host: only `packages/ui-fixture`,
listed by no product, calls `reactView`. A real `component` view comes with the first package that needs React for one,
proved on a host with the `test:ui` case that lands with it. ([F4c design](../docs/modular-graph-phase-f4c-design.md)
section 11.)

### A UI kit compile

The UI host offers no `kits` group ([uiHost.ts](../packages/retroplug/ui/host/uiHost.ts): the UI composes no audio
driver). A kit compile (`host.kits` in the UI host) is owed when a UI feature needs one. ([F4b
design](../docs/modular-graph-phase-f4b-design.md) section 10, [F4c](../docs/modular-graph-phase-f4c-design.md) section
11.)

### Gamepad axis tokens as action bindings

The gamepad-axis bus dispatches no app action, and the generator refuses a binding name that is not a button
(`GAMEPAD_BUTTON_NAMES`), so gamepad axis tokens as action bindings wait for a package that wants a stick-driven action.
([F4c design](../docs/modular-graph-phase-f4c-design.md) section 11.)

### Downloadable packs

Every pack is built in. The authoring loop, the pack format and downloads, a pack's UI half and unloading a pack (the
`Engine` has `loadScriptPack` and no unload) are what downloadable packs need (the C1 design's section 8). With them
come menus on extensions (a downloaded pack contributing menu rows, which packages in the tree do through `defineUi`)
and third-party controller apps: `controllerApps` registers from the product's package list, so an app from outside the
tree waits for this road. And a provider cannot be downloaded either, since it is reached only through the
generated static table, and which of a pack's facet methods a product mounts is not yet scoped. Whether a downloaded
pack's bytecode is stripped is the format's to say: a pack compiled from source keeps its debug information, about a
quarter more memory than stripped code. ([C1 design](../docs/modular-graph-phase-c1-design.md) sections 8, 12 and 13,
[E1](../docs/modular-graph-phase-e1-design.md) section 10, [E2](../docs/modular-graph-phase-e2-design.md) section 13.8,
[F3](../docs/modular-graph-phase-f3-design.md) section 11, [F5a](../docs/modular-graph-phase-f5a-design.md) section 12.)

### The app loads no script pack

Nothing in the app loads a pack yet: `backend.loadScriptPack` exists on every host, but no non-test caller uses it, and
a render job's Engine has the built-in pack only (`RenderHost` loads none), so loading a pack into more than one Engine
is unbuilt too. ([C1 design](../docs/modular-graph-phase-c1-design.md) section 12.)

### The template gaps a second pack meets

The E2 template gaps remain, and a second pack meets three: an RPC method name is global, so a repeat replaces the first
pack's without a word ([spec/02](02-native-host.md)); a pack cannot add a capability beyond `rp.system.memory/sram/rom`
(the engine refuses an unknown capability); and the test host has no seam for a pack's test provider
([main.cpp](../packages/native/src/main.cpp) calls the N8's and the Launchpad's factories by name). ([E2
design](../docs/modular-graph-phase-e2-design.md) section 13.8.)

### The N8 provider named by plugin.cmake and the test host

`plugin.cmake` links `retroplug-n8-desktop` by name, because the DSP library's own sources include the product's
providers header; and the test host's `main.cpp` names the provider (it includes `FakeN8.hpp` and calls
`rp::n8::createProvider` and `rp_n8_provider`), the same root as the missing test-provider seam. ([E2
design](../docs/modular-graph-phase-e2-design.md) section 13.6.)

### The view reopen key's width

The view reopen key keys on the `roles` array's identity, so any role write reopens every view of that system (edge E5,
one visible case: a blink; [viewSession.ts](../packages/retroplug/ui/views/viewSession.ts)). Still open: whether the
reopen key narrows (an optional `reopenOn?(sys)` per view, not built). ([F4c
design](../docs/modular-graph-phase-f4c-design.md) sections 10.4 and 11.)

### One owner for the `rp.midi` family

`core` owns `rp.midi.route` alone (exempt in the literal scanner through `--node-exempt`), the `midi` package every
other `rp.midi.*` type. Decide one owner before the per-device MIDI routing adds node types. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A config section paired with its UI service

No check pairs a config section's key with the UI service that reads it, so a typo is silent: the generator pairs a
placer with a project section but checks config sections only against the shell's keys and duplicates (F5a's
pairing-check item, one more key). ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### F5a's other leftovers

The F5a design's section 12 lists what no other entry carries: a `ui` module may still import React (no rule); a
per-header device check for header-only packages, and a header-only package's device manifest form; the plugin list
carrying `n8-wire` is held by hand (`order()` cannot hold a native-only package); comment edits move the desktop's
bundle bytes; `Placement.wires` reads `PlaceSpec.launches` only; `retroplug-launchpad-test`'s mid-drain swap case is
load-sensitive; contribution checks run at load (a malformed pack takes the control plane down, and the controller
registry has no unregister); and a project service is tied to `@retroplug/tracker` and the editor's song watch. ([F5a
design](../docs/modular-graph-phase-f5a-design.md) sections 12 and 14.)

## Known bugs

### A tracker marker with no protocol node

The `risa` and `bliptoaster` marker roles have no protocol node, so they count as placed whenever stored: a NES project
storing both (a risa project whose ROM is now BlipToaster) carries both markers, and `resolveTracker`
([trackerRegistry.ts](../packages/retroplug/src/trackerRegistry.ts)) takes the first in product order. ([Known bugs
batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)

### The JACK standalone's control handoff

The DPF JACK standalone's control handoff can run on JACK's thread. A JACK buffer-size or sample-rate change runs
`PluginDSP::deactivate`, the change and then `activate` from DPF's notification callback (`DistrhoPluginJACK.cpp`'s
`jackBufferSizeCallback` / `jackSampleRateCallback` -> `setBufferSize(n, true)`), not on the main thread every other
control path of the plugin runs on, while the editor's `uiIdle` reclaims on that main thread every frame. So
`deactivate` hands the engine back on JACK's thread: `setAudioThreadOwns(false)` clears the ownership bit (every push
then flushes inline on whichever thread makes it), `drainInto` drains the command ring there, `reclaimReleased` takes
the release ring there, and the script host's reclaim runs a pack's GC and free there - a second consumer of the ring,
and two threads in `JS_RunGC` / `freePack` is undefined behaviour. `activate` can likewise grow the plan on that thread
when the new buffer exceeds the plan's maximum (`setMaxBlockFrames` -> `GraphHost::reactivate`, which replans and
publishes) and re-declare the DAW parameters there. The fix: marshal that whole handoff (the flush, the ownership flip,
the drain, the release-ring reclaim, the script GC and free, and activate's replan and parameter re-declaration) to the
control thread, or make a buffer-size or rate change a message that thread applies, so the quiescent engine has one
owner. Rare and manual: a JACK buffer-size change in the standalone; CLAP, VST3 and AU call deactivate and activate on
the host's main thread, and VST2 runs `setBufferSize(n, true)` from its block-size opcode on whichever thread its host
calls that from ([PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp), above `activate`). Only `activate`'s
`setMaxBlockFrames` is a no-op for a block within the plan's maximum, which is why the path went unnoticed; C5's
per-frame reclaim widened its window from an edit to any frame. (Found after C5; no design records it. [C5
design](../docs/modular-graph-phase-c5-design.md) section 10 records the per-frame reclaim that widened its window.)

### A cart-node handover can cut a key

A handover can cut a key the new cart node strikes before the old one is reclaimed: the old node's held notes are
released behind every byte pushed when its destroy runs, the new node's first blocks among them, so a note the new node
starts on a key the old one still held, between the swap and the reclaim, is released with them. Where an idle tick
reclaims that is a frame or so; in a plugin whose editor is closed it runs to the next publish, the editor opening or a
deactivate. ([C5 design](../docs/modular-graph-phase-c5-design.md) section 11.)

### The N8 menu, unverified on a cart

Three behaviours of a cart at its menu are reasoned, not seen. MIDI streamed to a cart at its menu reaches the menu's
own command parser, which takes commands over the same FIFO, so a data pair such as `0x2A 0x73` (`*s`) can ask it to
start an app; it holds for raw MIDI after a connect at the menu and for framed MIDI after a console reset, where the
stale window is re-sent every 100 ms until it re-syncs. The quiet rule assumes the menu writes nothing unprompted on the
back-channel: unsolicited bytes would keep `lastHeardNs` fresh and a menu with stale frames would read as never quiet.
And a heavy stream into a menu can starve the probe: the window's 48-frame re-sync re-stamps the oldest frame's first
send, so past about 190 messages a second (48 per 250 ms) no turn is quiet and a submission is refused. ([C5
design](../docs/modular-graph-phase-c5-design.md) section 11.)

### The stripper bundle outside the repository

`retroplug-ts-stripper-bundle` fails in a build directory outside the repository:
[tools/build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) runs in the binary directory and sets no esbuild
`absWorkingDir`, so its `typescript-5` alias resolves from there (unlike [tools/build-ui.js](../tools/build-ui.js),
which sets it to the repo root). ([B3a design](../docs/modular-graph-phase-b3a-design.md) section 10.)

### A bar or beat quantise counts host bars

A quantise of `bar` or `beat` still counts the host's bars: at a tempo divisor of 2 a cart's phrase lasts two of them.
It may be the behaviour wanted; decide, then fix it or state it in [spec/13](13-subsystems.md#the-controller-role).
([Known bugs batch 2 design](../docs/known-bugs-batch-2-design.md) section 5.)

### MidiStream::send blocks the audio callback

`MidiStream::send` runs every selected hardware output's send inline on the audio callback, so a WinMM bulk SysEx of
about 170 ms (a controller app's LED traffic) blocks the audio thread. The fix is a writer thread per hardware output,
as the Launchpad link has. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A MIDI pick dirties the untitled project

A MIDI pick on the start screen dirties the empty untitled project (`ProjectStore.setSection` marks it dirty
unconditionally), so the close guard asks to save a project holding only the pick; exempt a section-only change to an
empty, pathless project. (Found by the final review that [F5b design](../docs/modular-graph-phase-f5b-design.md) section
14 summarises; recorded only here.)

### Settings > MIDI misses a claim made while open

The row's reserved pair is a `describe()` snapshot, so Settings > MIDI's checklist does not notice a claim made while
its container is open; a cheap, no-probe `state()` read on `MidiDeviceHost` would fix it. (Found by the final review
that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)

### The audio draft's sibling-backend cache

The audio draft's sibling-backend cache is per backend change, not per visit
([audioDraft.ts](../packages/audio/ts/ui/audioDraft.ts) caches by the described backend). (Found by the final review
that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)

### An Apply past a read-only config

`UserConfigStore.setSection` answers false when it leaves a read-only config alone, but the UI host's
`config.setSection` drops that answer (`void`, [uiHost.ts](../packages/retroplug/ui/host/uiHost.ts)) and
`applyAudioDraft` calls `audioDevice.apply` regardless, so an Apply still reaches the device. (Found by the final review
that [F5b design](../docs/modular-graph-phase-f5b-design.md) section 14 summarises; recorded only here.)

### A normal CLI exit skips the teardown

`retroplug-cli`'s override of `tjs.exit` never takes: txiki's `tjs` is frozen, so the set in
[cli/main.cpp](../packages/native/cli/main.cpp) fails (the launcher clears the exception it leaves) and a session's
`tjs.exit` is txiki's own, which ends the process at once. So `g_exit` is never set and the launcher's `Teardown`
(`engine.clearGraph()`, then `rp_stop_providers`) never runs on a normal exit: the plans are not reclaimed while the
providers' links are alive, and the providers are not stopped. The fix needs an exit the launcher sees (a `tjs.exit`
wrapped before txiki freezes `tjs`, or an exit hook) and a look at what each provider's stop does at a process's end.
The same exit also loses an error: one thrown in the same loop turn as a `tjs.exit(0)` is never reported, since the
process ends inside txiki's timers phase, before the launcher looks, and exits 0 with nothing printed.
([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 2.)

### The test host drops an uncaught error

The test host (`retroplug-host`, [src/main.cpp](../packages/native/src/main.cpp)) swallows a top-level throw: it
pumps txiki's loop without looking for a pending exception or a stop request, so an error nothing caught outside the
harness's reach (a module body's throw before its first `test()`, a timer created before it) is dropped, and the file
ends as "tests did not complete (tjs.exit never called)" with no reason, which hides a fixture script's real error (the
renders' fixtures meet it), or with a kept-alive one only when its 30-minute deadline runs out. Its `tjs.exit` override
does not take either. The fix is the CLI's `pumpOnce` (which handles this fault), with the failed `tjs.exit` set's
pending exception cleared as the CLI clears it (`cli/main.cpp`), then a whole `pnpm test:native` run for the strays
it surfaces. ([sdk-n8 port design](../docs/sdk-n8-port-design.md) section 3; [D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### An SMS stage lands on player 2

`MesenSmsSystem::hasCoreBytesIn()` is unconditionally true (`MesenSmsSystem.hpp:80`), so `SystemNode::applyInputs` routes
a raw `bytes` write and `stageMessage` routes a staged UI message into `pushCoreBytes`, and on any SMS or Game Gear ROM
those bits drive port 2's TL, TR and TH, which every game reads as player 2 (`isSmsggdjRom`'s comment describes the
hazard). The Mega Drive closed the same door with its descriptor's `capabilities.claimedBytesIn` and the node's
`coreBytesIn` live knob (`BlastemNode.cpp`): the port accepts levels only while the system's nodes claim its `bytes`
input. The capability is generic (`SystemBase::hasCoreBytesIn()`), so the SMS fix is the descriptor's
`claimedBytesIn: true` plus a `coreBytesIn` knob on `rp.mesen.sms`. It is latent, not live: nothing in the shipped
product stages at an SMS node or wires its `midi` input. ([design](../docs/megadrive-blastem-design.md) section 4;
found by the genmddj sync role's review.)

### BlastEm's remaining process-exits

BlastEm's `fatal_error` logs at FATAL and calls `exit(1)` inside the DAW's process. This branch converted the sites a
cart reaches (`machine_freeze` no longer ends a library build: it logs at WARN, and an unmapped access, an HV counter
write, a Z80 VDP window write or a VDP port read the decoder has no answer for is a dropped write or an open-bus read).
What is left, by the condition that makes each reachable in this build's file set:

- `romdb.c`'s fifteen (50, 887, 891, 911, 915, 924, 938, 942, 946, 950, 954, 1014, 1020, 1388, 1646): the parser of the
  compiled-in `rom.db.c`. A cart cannot add a row (it selects one by hash, and a bad selection is already the mirror's
  `load_failed` refusal), so these need a corrupt generated `rom.db.c` or the null `read_bundled_file` returns on
  out-of-memory at the first boot.
- `i2c.c`'s four (187, 203, 228, 242): an EEPROM cart's own access to an address its ROM DB map does not cover. A ROM
  the DB maps as EEPROM can reach these, and it is the one group a cartridge's code alone can trigger.
- `tern.c:277`: a ternary key path longer than 127 bytes, in the config or ROM DB tree; only a key that long in either
  reaches it.
- `system.c:104, 219`: a split SMD ROM and a short read, both inside the file loader (`load_media`) this host never
  calls, since it hands bytes to `alloc_config_genesis`.
- `genesis.c:2207`: a state that is neither native nor GST, inside `genesis_load_state`, the frontend's path; this host
  deserializes behind its own section walk.
- `genesis.c:3095, 3102, 3129, 3177`: a TMSS model whose ROM cannot be loaded, and memmap chunks past `NUM_MEM_AREAS`
  after the TMSS remap. TMSS is off in the model this build's empty config tree selects (`md1va3`), so no machine it
  boots reaches them; a config that turns TMSS on would.
- `config.c`'s six (67, 596, 598, 630, 636): a malformed, missing or unwritable config file, in `load_config` /
  `save_config`, which nothing in this build calls (`config` stays null, and the ROM DB's defaults are what the core
  uses).
- `paths.c`'s four (91, 95, 538, 542) and `util.c`'s four (944, 948, 1074, 1078): Android's activity and URI lookups,
  compiled out of every desktop build.
- `event_log.c`'s thirteen (67, 379, 486, 491, 648, 654, 671, 674, 685, 696, 702, 730, 745): a log's socket or
  deflate/inflate failure. Nothing in this build starts a log, so they are dead until something does ([the event
  log](../docs/megadrive-blastem-design.md) section 2).
- `gen.c:12`: the JIT code generator's allocation failure, reached only from the x86 backend this build does not
  compile.

The task that converts the last of them deletes this entry, per this file's own rule. Until then it is the record that
a corrupt payload from a path nobody has taken can still exit the DAW.

## Engine

### DAW automation of node params

The plugin's parameters are an output gain and a pool of MIDI CC slots; none maps onto a node param through
`Engine::setParam`, though params make that possible, as its own work. ([C2
design](../docs/modular-graph-phase-c2-design.md) section 9.)

### P3: cores borrow their pinned ROM

Each system holds one more full copy of its ROM: the `rom` resource a node pins for the system's life is on top of the
vector the build hands the core and Mesen's own copies, so a 32 MB GBA ROM is +32 MB per system, per plugin instance.
Phase C's P3, cores borrowing their node's pinned ROM bytes, would remove it; it needs a regenerated SameBoy patch, and
a Master System re-reads its staged file on reset. ([C3 design](../docs/modular-graph-phase-c3-design.md) section 11,
[C4](../docs/modular-graph-phase-c4-design.md) section 12.)

### P4: the snapshot registry's double copy

The read door copies from each core's own tear-free triple into a registry-owned buffer, because the shared `SystemBase`
cannot yet publish straight into the registry
([SnapshotRegistry.hpp:23](../packages/native/src/host/engine/SnapshotRegistry.hpp#L23)). P4 removes the snapshot
registry's double copy: a documented redundancy, not a bug, and a `SystemBase` refactor, not urgent. ([C4
design](../docs/modular-graph-phase-c4-design.md) section 12.)

### P7: the rest of the capability split

C5 built three capabilities (`rp.system.memory/1`, `.sram/1`, `.rom/1`). P7's remainder is the rest of the `SystemBase`
capability split: video, buttons and debug are still `SystemBase` virtuals (`framebuffer()`, `pressButton`,
`getCpuRegisters` / `readCpuByte` / `debugTarget`), and `state/1` is still the system-id RPC (`readState(id)`), the tap
is still `ChannelTap` rather than `rp.system.tap/1`, and there is one node file per package, not one node file per
console (`MesenNode.cpp` holds all three Mesen consoles' nodes). ([Main design](../docs/modular-graph-design.md) section
6.4, [C4](../docs/modular-graph-phase-c4-design.md) section 12, [C5](../docs/modular-graph-phase-c5-design.md) section
11.)

### Allocations left on the block thread

A warmed block allocates nothing; these are the exceptions:
- A Mesen core's 0.5 s state publish allocates inside its streaming serializer, every 24 k frames at 48 kHz, so every
  `[alloc]` case on an Engine-built Mesen core charges a whole interval uncounted first.
- The N8 firmware's SD path still allocates, in the emulated cart (`std::string` and `std::filesystem` in its command
  handlers, a bigger directory listing), and its `_txQueue`'s unbounded growth when a ROM writes `CMD_USB_WR` and no
  host drains it is the other half; bounding it would change the "never drop a byte the device would deliver" contract.
  No block test drives an SD command, so the SD path's allocations are ROM-dependent. The same queue is a `std::vector`
  the debug `drainCoreBytes` RPC swaps empty, so a non-link ROM's next write after a drain grows it.
- A Game Boy's `serialOutLog_` is cleared each block but grows to the largest block's capture while MI.OUT capture is
  armed (off the steady-state path the `[alloc]` cases hold).
- `rp.host.midi-out` appends a `std::vector` per message to the engine's host output (`Engine::midiOut_`, the
  `rp.engine.host-io/3` service): an allocation for every message out; a flat arena, as the staging has, would remove
  the vector per message.
- A retained generation of a script pack is freed only at a reclaim (see the closed-editor entry below).

([B3a design](../docs/modular-graph-phase-b3a-design.md) section 10, [C4](../docs/modular-graph-phase-c4-design.md)
section 12, [C5](../docs/modular-graph-phase-c5-design.md) section 11.)

### Input-drop counters beside the graph's

The cores' input-drop counters are not surfaced beside the graph's: each core's `inputAccepted()` / `inputDropped()` and
the FIFO's `coreTransportStats().droppedBytes` have no host caller, and `rp_process` carries no instance-drop pointer,
so an input-drop service on the node seam is its own change. ([C4 design](../docs/modular-graph-phase-c4-design.md)
section 12.)

### The host MIDI-in staging

`rp.host.midi-in`'s sort merges two frame runs in linear time (what real plugin hosts stage: the DAW's events, then the
automation CCs at frame 0); three or more runs fall back to an in-place insertion sort, quadratic, on the block thread.
The staging has no count cap either (past its reserved room it grows), so a host staging many messages out of order pays
for every one, though the input node's queue keeps 2048. A cap at what the input node carries, or a k-run merge, would
bound it ([Engine.cpp](../packages/native/src/host/engine/Engine.cpp) `Staging::takeSortedInto`). ([B3a
design](../docs/modular-graph-phase-b3a-design.md) sections 3 and 3.4.)

### Script packs: isolation, reloads, refusals, memory

What the script packs leave, from isolation inside a pack to a re-plan's two instances:

- **Isolation inside a pack.** Nodes of one pack share its realm, so one node's code can change what another's reads
  through (an accessor on `Object.prototype` or `Array.prototype`, a global, a definition in `__rp_scriptNodes`).
  Freezing a pack's intrinsics after its load (an SES-style lockdown) would close it, at the price of code that writes
  over an inherited property.
- **A node's state across a reload.** A reloaded pack's nodes restart clean; a `snapshot` / `restore` pair would carry
  it.
- **A failing node refusing the whole document.** A node whose settings check fails refuses every document placing it,
  so no other edit lands while it is placed. A per-node "invalid" state, the document planning without the node, is a
  choice for the authoring work.
- **A pack's memory limit counts garbage.** An allocation past the limit is refused with no collection first, so a pack
  whose reachable heap is above about two thirds of its limit can be faulted for garbage: the limit ends up counting
  garbage as well as reachable bytes.
- **A re-plan holds two instances.** On the desktop's MicroQuickJS host a re-plan holds two instances of a pack at once
  (contexts are 4x `heapBytes` with a 1 MiB floor, and a dead pack is revived), so a re-plan behind a large source can
  still run a pack out until the replaced instance is reclaimed.

([C1 design](../docs/modular-graph-phase-c1-design.md) section 12, [E4](../docs/modular-graph-phase-e4-design.md)
section 11.8.)

### Dead code and the kernel test names

- `routeBlockInto` in [midiRouting.ts](../packages/core/ts/midiRouting.ts) has had no caller since B3b: the routing node
  routes each message with `routeTarget`, the same arithmetic, and `routeBlock`, its pure twin, stays as the reference
  the routing node's test holds every mode to. It can be deleted.
- `packages/native/src/lsdj/OffsetLookup.hpp` is dead (nothing includes it); `src/lsdj/` is otherwise test-reference
  code.
- The vendored `deps/enkiTS` is tracked but neither built nor linked; a cleanup decides whether it goes.
- Some test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`, `dsp-sms-sync` and
  `plugin-controlplane`) where they mean the protocol nodes; B3b kept every test identity it did not have to change.

([B3b design](../docs/modular-graph-phase-b3b-design.md) sections 1 and 7,
[D2](../docs/modular-graph-phase-d2-design.md) sections 9 and 11.)

### The tap's mode bound

The tap's 1-to-3 mode bound is the engine's, not the capability's: `Engine::setGraph` still validates 1 to 3 (the NES
numbering), a number TS owns. `maxMode()` on `ChannelTap`, which declares only `exportMode` / `setExportMode`, is the
later move. ([C4 design](../docs/modular-graph-phase-c4-design.md) section 12.)

### A narrow host still arms the tap

A split routing on a host too narrow for it still arms the tap, paying for the capture, its resamplers and the per-block
discard. Not arming it is a CPU-only change `buildGraph` could make from the lane budget (`tapArmOf` checks one system,
a split and a tappable core). ([C4 design](../docs/modular-graph-phase-c4-design.md) section 10.)

### Threaded engine cases are smoke runs

The engine-level threaded cases are smoke runs, not race detectors. Against a snapshot registry whose claim freed a
buffer the publish wrote, `EngineGraph.test.cpp`'s GBA beside a publishing Game Boy reported nothing in three runs under
each sanitizer, though ad hoc runs at 256-frame blocks caught it and the registry-level case fails on it. The C3 case (a
system joining and leaving beside an audio thread) was never run against it, and alternates one Game Boy with an empty
plan, so no claim grows beside a publishing plan. Why the GBA case stays silent is unproven: ThreadSanitizer sees an
ordering between the `setGraph` path and the block thread (a probe written each block stopped racing at
`GraphHost::reclaim`), but the pairing release was not found, so "an Engine hides the race" is a reading, not a finding,
and until it is understood these are weak race detectors. ([D2 design](../docs/modular-graph-phase-d2-design.md) section
11.)

### A plugin with its editor closed has no idle reclaim

A plugin whose editor is closed has no idle reclaim (DPF gives a plugin no idle callback), so a plan a DAW's `setState`
replaced while the transport plays keeps its nodes, a cart node's held notes, a core and their pinned resources until
the next publish, the editor opening or a deactivate. ([C5 design](../docs/modular-graph-phase-c5-design.md) section
11.)

### The script node Block has no resource accessor

The script node `Block` ([defineNode.ts](../packages/core/ts/defineNode.ts)) has no resource accessor and no link peers,
though a `PortSpec` can declare resource and link ports. (Found building B3a; [section
2.3](../docs/modular-graph-phase-b3a-design.md) is the Block's marshalling.)

### The command ring and the adoption wait

Commits to script nodes never make `setGraph` wait (their prepare is stateless), so the GraphHost command ring (256
slots, 255 usable, shared with params and the engine's commands) bounds edits while no block runs: each edit queues a
plan holding its buffers, and a full ring answers busy; the publisher keeps its model, so the next publish lands the
latest. Coalescing queued, unadopted plans is the alternative, should it bite. The adoption wait is global: one commit
to a type without `RP_TYPE_STATELESS_PREPARE` holds back an edit touching only stateless nodes (a gate per instance is
possible), and every `node.hpp` node without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag
could be derived. ([B3a design](../docs/modular-graph-phase-b3a-design.md) sections 2.4 and 10.)

### Serial capacities past large blocks

A Game Boy clocking its own serial port (a CGB on its fast clock, a captured byte every 8 frames) can send more in a
block past about 12000 frames (6000 in double speed) than its fixed `serialOut` holds; the rest is dropped and counted
(`kSerialOutCapacity` in [SameBoyNode.cpp](../packages/sameboy/native/src/system/sameboy/SameBoyNode.cpp)).
`rp.lsdj.serial-out`'s `midiOut` (`MIDI_OUT` in [ports.ts](../packages/core/ts/ports.ts), 1024 messages and 8192 bytes)
is sized for a decoded MI.OUT stream at its densest up to 8192-frame blocks, while `serialOut` is sized for 65536-frame
blocks; in a larger block the rest is dropped and counted. ([B3a design](../docs/modular-graph-phase-b3a-design.md)
sections 3.2 and 4.1.)

### A feedback source's capacity change

A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front: the block in flight
is lost and `droppedMessages` never counts it, since the carry and its count run only while quiescent
([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). Nothing triggers it today. ([B3a
design](../docs/modular-graph-phase-b3a-design.md) section 2.6.)

### A custom reflector in the device decode

A custom `rfl::Reflector<E>` is not seen by the device decode; `rfl::internal::has_read_reflector<E>` would refuse it in
one line. No node has one. ([E1 design](../docs/modular-graph-phase-e1-design.md) section 12.3.)

## Renders and kits

### A plugin kit row

A plugin kit row is possible and not built: no UI row compiles kits in the plugin. It would call the cart's `kitFormat`,
found through the shell's `kitFormat(id)` registry, with the stores' backend as its job runner, not
`AudioDriver.compileKitFormat`, whose native wait would block the UI frame. Each Engine parks up to 8 normal-priority
offline workers for life once it has run a job, so N instances in a DAW hold 8N threads; the row should lower the
ceiling of the 8 offline workers or retire idle ones (a compile briefly puts 8 busy threads beside the audio thread).
([D2 design](../docs/modular-graph-phase-d2-design.md) section 9.)

### The kit patcher's latency budget

Interactive kit patching misses the kit patcher's latency budget, a knob change heard almost at once: the TypeScript
compile costs about 23 ms typical and 83 ms worst for a 15-slot kit (native was 1.7 and 4.9 ms), nearly all of it
buffers crossing the JS thread between passes. When a patcher UI lands, chain a slot's passes inside one job or keep the
intermediates native behind handles, and recompile only the slot a knob touched. ([D2
design](../docs/modular-graph-phase-d2-design.md) section 9.)

### CI sees 5 of 13 render cases

CI sees 5 of 13 render-baseline cases (mGB as a Super Game Boy and four smsggdj songs); the LSDj and risa cases need
fixtures, and nothing of a NES, a split render or a Game Boy's stems runs there. MIDI-driven renders (an mGB or
BlipToaster render fed a MIDI file) would close it and are not built. ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### A GBA render case

A GBA render case is missing: without a BIOS the repo's GBA ROM renders exact silence. ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### A sanitizer leg for renders

A sanitizer leg for renders is missing: `tools/run-sanitizer.sh` has no render leg, so the pool's thread and address
checks are `[engine-jobs]` built in `build-tsan` / `build-asan` by hand (a script leg would make them routine). ([D3
design](../docs/modular-graph-phase-d3-design.md) section 10.)

### Render residuals

The D3 design's residual list, extended after its final review:
- An auto-detect render's length is quantised to its 100 ms poll, and acts between the library's calls land at the next
  block boundary; block-granular probes or an autonomous recipe would remove both (D3 section 8).
- An interrupt that lands in a pending promise job is also printed as a script error before the loop re-checks
  `interrupted_` (`RenderHost.cpp`, the job loop's `r < 0` branch); only a script working in promise jobs reaches it.
- An interrupt after the script reported `done` keeps its `outputs` under `cancelled` (only `message` is cleared);
  reachable only if the cancel has waited out `kCancelGrace` by the time the worker's post-result JS runs.
- A failed `.part` sync reads as a failed move in `render.ts`'s error ("could not replace X"), pointing at a locked
  target rather than a disk error.
- `foldsCase` trusts a single probe (the first entry with a cased letter), so a broken symlink or a non-simple case swap
  reads a folding filesystem as case-sensitive.
- A battery cart whose live SRAM reads null still boots the sibling `.sav` silently in `ui/host/render.ts` (it sets the
  spec's `sav` only when `readSram` answers); a running battery core should always answer `readSram`, so refusing, as
  the unwritable-snapshot case does, is open.
- Wedged jobs: a script missing its cancel is interrupted after `RenderHost::kCancelGrace` (2 s), but a core wedged in
  one native call hangs the CLI (a second Ctrl-C kills it, possibly leaving its `.part` files) and an instance's
  teardown.
- Cores print straight to the process: Mesen's load log goes to fd 1 while the job's console lines arrive through the
  drain, so in the CLI the two can interleave differently.

([D3 design](../docs/modular-graph-phase-d3-design.md) sections 8 and 10.)

### The behavioural render tests run in-process

Some behavioural render tests (among them `risa-render`, `sms-render`, and the pure tier's `start-truncation` and
`risa-autodetect`) call `runRenderJob` on an exact-blocks engine, and `audio-render` and `render-guard` drive the audio
driver's renders in-process: paths no production caller uses. Their assertions reach engine jobs only through the render
baseline and `engine-run`. ([D3 design](../docs/modular-graph-phase-d3-design.md) section 10.)

### The statics patch's camera half has no test

The SameBoy camera half of `cmake/patches/sameboy-process-statics.patch` has no red-green test: there is no Game Boy
Camera ROM in the repo or the fixtures. ([D3 design](../docs/modular-graph-phase-d3-design.md) section 10.)

## Pico

### Audio on the box

The box has no audio: a codec and its DMA driver, a fixed-cadence block driver and the float tolerance against the
desktop are unbuilt (`pico/host` has only `ElapsedDriver`), and a time-reading plan on the elapsed driver loses the time
its carry bound skips under load. A fixed cadence cannot run from the box's main loop, which blocks for every N8 call (5
ms a FIFO write, up to about 200 ms draining a full ring, 2.5 ms a sniff or health read, 13.7 ms a `[stats]` line), so
blocks move off it; then the staging and the cart link's ring become single-producer / single-consumer (or the N8 calls
non-blocking), and `frameOf` moves onto `BlockDriver`. ([E3 design](../docs/modular-graph-phase-e3-design.md) sections
10 and 12.8.)

### Live plan upload

The loader trusts the blob, so a live plan upload needs a per-node port signature carried in the blob and checked at
load; the same check would replace `loop.sh`'s guard against a stale `retroplug-export-plan` (a blob from an exporter
built before a port changed loads and reads the wrong views). A pack in a plan that is not built in also needs a trust
model for bytecode MicroQuickJS does not verify. No plan file on the SD card and no resources on the device exist yet.
([E3 design](../docs/modular-graph-phase-e3-design.md) sections 10 and 12.8,
[E4](../docs/modular-graph-phase-e4-design.md) section 9.)

### A controller path on the box

The Pico has no controller path: the box's one root port is the N8's, `UartMidi` is input only, and there is no Game Boy
on the box for MI.MAP to drive. The launchpad core alone needs at least 61440 bytes (budget 76800), past the box's 64
KiB arena, and the bridge with MI.MAP at least 144960 (budget 184320). The pack declares no `script` member: its
scenarios prove the core runs on MicroQuickJS, and making it a device pack (bytecode at both widths, a `heapBytes`) is
the Pico controller's work. ([F3 design](../docs/modular-graph-phase-f3-design.md) sections 6, 11 and 12.)

### A 32-bit runner in the build

No 32-bit runner is in the build: the 32-bit bytecode runs nowhere in CI, so the 3/2 sizing of a 64-bit device build is
a heuristic, and the box's boot line and the device check's policy case are the authority. ([E4
design](../docs/modular-graph-phase-e4-design.md) sections 11.6 and 11.8.)

### The box's stats print can stall a message

The box's `[stats]` print can still stall a message for ~13.7 ms, as E2's did; a ring console would cost about 100 us at
every message's worst, so it is recorded, not fixed. ([E3 design](../docs/modular-graph-phase-e3-design.md) section
12.8.)

### A SysEx past 3 bytes allocates

A SysEx past 3 bytes allocates a view per message (dying with its block), and at 32 bits a non-whole number read
allocates, so a SysEx stream makes the box collect in the MIDI path; the policy's worst block fits with 4 KB spare. ([E4
design](../docs/modular-graph-phase-e4-design.md) section 11.8.)

## Feature gaps and SDL

Feature gaps a user could reasonably expect, and the SDL standalone's unbuilt or unverified pieces. None block shipping.

### Raw LSDj Keyboard sync mode

`lsdj-sync` runs every mode but raw Keyboard (`keyboard`, LsdjSyncMode 4): MidiSync, MidiSyncArduinoboy, MidiMap,
KeyboardMidi and MidiPassthrough in [lsdjSyncNode.ts](../packages/lsdj/ts/lsdjSyncNode.ts), MidiOut (ArduinoboyMaster)
and MasterSync in [lsdjSerialOutNode.ts](../packages/lsdj/ts/lsdjSerialOutNode.ts), the role's second node, after its
Game Boy. Raw Keyboard sends nothing because it needs a per-block **`keys` feed**: `rp.lsdj.sync` has no `keys` input
and no host stages any. A `keys` input on the node and a host I/O node that feeds it unblock Raw LSDj Keyboard sync.
([04-roles-protocol-nodes.md](04-roles-protocol-nodes.md#not-yet-built--deferred).)

### NES per-mapper expansion sub-channels

Per-console channel output ships end to end ([10-multichannel-audio-out.md](10-multichannel-audio-out.md), steps 1–7).
The open piece is the per-mapper expansion voices (VRC6 pulse and saw, VRC7's six FM channels, N163, MMC5, FDS, S5B),
which live inside each mapper's audio class before they sum into the chip's `AudioChannel` delta, so they need a
per-mapper audio tap deeper than the `NesSoundMixer` edit; VRC7's emu2413 core is the one large tap. Test material is
not the obstacle: `resources/roms/bliptoaster-{vrc6,vrc7,n163,mmc5,s5b}.nes` are committed, and `reaper:params-vrc7`
drives one. ([10-multichannel-audio-out.md](10-multichannel-audio-out.md) section 3b.)

### The standalone reopens the last project

In a DAW the host chunk is authoritative. The standalone starts empty unless `RETROPLUG_AUTOLOAD_PROJECT` seeds it;
reopening the last project on launch from disk is not built.
([05-data-persistence.md](05-data-persistence.md#not-yet-built--deferred).)

### Live memory-region subscription

There is no live "watch RAM" streaming path: reads are one-shot through the `SnapshotRegistry` read door, and the
consumers that want live memory poll it, the LSDj overlay per frame and the HD player
([hdView.ts](../packages/lsdj/ts/ui/hdView.ts)) on a frame divider, because a 128 KiB WRAM copy plus a song decode is
too costly every frame. A cost question, not a capability one: an arming seam would let those two read a few hundred
bytes instead of copying the whole image. ([02-native-host.md](02-native-host.md#not-yet-built--deferred).)

### MIDI out past 4 bytes

MIDI in takes every size (`PluginDSP.cpp:331` reads `dataExt` when a message outgrows the inline `data[4]`), so a DAW
can upload an N163 wave the way the CLI does. The out path drops anything past 4 bytes: `PluginDSP.cpp:369` skips events
over `MidiEvent::kDataSize`. Lifting it is not one line: the plugin never sets `dataExt`, and DPF's VST3 writer reads it
for a size over `kDataSize`, so removing the guard is a null dereference. It also buys little: VST2 drops over 4 bytes,
CLAP drops over 3, VST3 drops SysEx regardless; only the JACK standalone gains. ([F3
design](../docs/modular-graph-phase-f3-design.md) section 11.)

### Mesen .mlb labels in the CLI debugger

The debug facet is built out ([09-cli-debugging.md](09-cli-debugging.md)); the one remaining item is Mesen's native
`.mlb` label files, which need a new parser (Mesen's C# one isn't vendored). A CLI-only nicety.

### SDL host MIDI out at block time

The SDL standalone sends host MIDI at block time: its RtMidi out takes no timestamp, so the audio callback sends each
block's `Engine::midiOut` as the block renders, its frames dropped, where the plugin hands the DAW each message at its
frame. Scheduling it is the standalone's own work. ([LSDj serial-out design](../docs/lsdj-serial-out-design.md) section
11.)

### SDL MIDI-in at frame 0

MIDI-in frame offsets: every SDL hardware MIDI message still stages at frame 0, up to a block of jitter. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### Per-device MIDI routing

Per-device MIDI routing, the user's next want: each port its own source and destination, `rp.host.midi-in` / `-out` per
port (so `rp.engine.host-io` takes a new version), `buildGraph`'s routing keyed by saved name, and the menus.
`MidiStream` merges every input into one ring today, so a staged message carries no port, and a per-port node needs a
node target in the `midi` package, whose only manifest item is a provider. The `midi` section is ready: each entry is an
object with room for a routing field, so adding one needs no version bump. The `rp.midi.*` node family and `MidiStream`
are the layer that follow-up extends. ([F5b design](../docs/modular-graph-phase-f5b-design.md) sections 12 and 14.)

### The handheld-perf rebase and the muOS run

The `feature/handheld-perf` rebase onto F5b: its render-ahead (4b9e39ea8) is `AudioHost`'s now, so its `main.cpp` hunk
reduces to the muOS launcher flags. Then the muOS device run that proves render-ahead (xRT, underruns, fps) at the
quantum the server locks, confirming the PortAudio [PipeWire] line. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) sections 12 and 14.)

### The n8-bridge sessions on a real N8

The n8-bridge sessions through the CLI's real RtMidi facet have not run on a real N8; only the headless host's
fake-backed facet is tested. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### SDL on a real Windows machine

CI builds and unit-tests the SDL packages on Windows but lists no real device: WinMM's port names for `portMatch`'s
table (rows unverified; two identical devices resolve to nothing; a refused exclusive input stays refused until a real
apply), where WASAPI Auto lands for PortAudio now that AutoConvert is set, and the send wait the output-set swap bounds
(a `MidiPort` destructor cannot wait for an in-flight WinMM callback). ([F5b
design](../docs/modular-graph-phase-f5b-design.md) sections 8, 12 and 14.)

### macOS on a real machine

CI compiles both audio backends on macOS and runs the unit tests; CoreMIDI and CoreAudio on a real machine, the
framework build and `sdl:smoke`'s legs are owed. ([F5b design](../docs/modular-graph-phase-f5b-design.md) sections 12
and 14.)

### Device hotplug and MIDI clock phase lock

There is no hotplug: a PortAudio device list is fixed at `Pa_Initialize`, and MIDI ports are read when asked. MIDI clock
phase lock is unbuilt too. ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### A really removed audio device

`lost()` on a really removed device is unproved for both backends (the leg would be a `pw-cli destroy` of the open sink
mid-run, then the muted line), and a watchdog trip stays muted with no backoff retry. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12, R10.)

### SDL's PipeWire node for a wide request

SDL's PipeWire node has 2 output ports for a wide request while the host renders the requested width (R11, the cause not
established). ([F5b design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### The PipeWire data loop's RT pin

The PipeWire data loop is still RT-pinned through rtkit's overrides: `AudioHost` applies `SCHED_FIFO` and a core pin to
any non-null device's callback thread; tune it only on ALSA, per the release list. ([F5b
design](../docs/modular-graph-phase-f5b-design.md) section 12.)

### No DAW check records LSDj's MIDI out

No DAW check records LSDj's host MIDI out (the plugin's `writeMidiEvent`); a Reaper leg that records the plugin's MIDI
out would close it. ([LSDj serial-out design](../docs/lsdj-serial-out-design.md) section 11.)

### A plain-ROM controller target

No row says a plain-ROM target has nothing to drive: a controller aimed at a system whose graph places no cart sync node
shows no `Cart: <app>` row at all; an info row would say why. ([Known bugs batch 2
design](../docs/known-bugs-batch-2-design.md) section 5.)

### DAW MIDI to a cart with no NES system

No project-level fallback forwards DAW MIDI to the real cart without a NES system: a card job addresses a node, and a
project with no system places none, so driving a MIDI cart with no NES in the project has no UI path. ([C5
design](../docs/modular-graph-phase-c5-design.md) section 10.)

### genmddj's tracker integration and MIDI takeover (spec 2)

genmddj is identified and clocked (spec 1), and its app half stops there. Spec 2 is what makes it a tracker and an
instrument ([design](../docs/megadrive-blastem-design.md) section 1's table and section 7): an SRAM codec for the
cart's own save format, so the Songs menu can list, load, import and export over `@retroplug/tracker`'s `SongCatalog`
(and so the end-to-end case can seed a `.sav` rather than poke IN24 into work RAM, which it does today because the codec
is this spec's); the Songs and live-load path the other trackers have; and the 10-voice MIDI takeover, the DAW's MIDI
played on the cart over the emulated port-2 bridge (design section 3's last bullet: the device answers each TR edge the
ROM drives, at its cycle, with the next bit of a queued message). One thing the bridge must do first is split the port's
door deliberately: once a system claims port 2, `pushCoreBytes` takes every byte, so a staged UI message and a routed
MIDI run too long for the framed door are levels of IN24 clocks just as the sync node's words are. It has to tell them
apart (the Mega Drive node overriding `stageMessage` to refuse, or a separate input), not read the claim as the sync's.
Nothing in the shipped product stages at a Mega Drive node or wires its `midi` input today, so it is latent.

### Mega Drive audio fidelity (spec 3)

Spec 1 ships BlastEm's chips and keeps its output path's character: the YM2612 as BlastEm runs it, BlastEm's 3390 Hz
one-pole, and blip_buf's band-limited steps at the host rate. Spec 3 ([design](../docs/megadrive-blastem-design.md)
sections 0, 2 and 7) is the fidelity pass: Nuked-OPN2 as an FM option (the design's speed estimate with it is about
0.7x realtime on the A53, which is why it is an option and not the default), the YM2612 against YM3438 (discrete against
integrated) models, the output filter's choice, and per-channel stems, where the sink is one stereo mix today. Two open
questions ride with it: the LFO's step period, where BlastEm steps every N+1 samples and Nuked and MAME every N (same
table), so rate 7 is 69 Hz against 83 Hz and Sega's documented rates match N+1, and spec 1 keeps BlastEm's behaviour;
and the sink's treble shelf, blip's kernel taking -0.35 dB at 3 kHz to -3.4 dB at 16 kHz for the PSG at 48 kHz (less
for the YM, which already interpolates), which a hardware recording settles along with the filter.

### The A53's Mega Drive speed

The design's estimate is weak: about 1.2x realtime, about 0.7x with Nuked, extrapolated from x86 through Mesen's
x86-to-device ratio ([design](../docs/megadrive-blastem-design.md) section 7). No A53 ran it, and this branch could not
try (the board was unavailable), so it is still an estimate, and the stop grid is why it may be optimistic: the grid
costs about 15 to 19 percent of a block on x86 (hvPsg 22.7 to 19.7x realtime, the per-VINT DMA image 19.2 to 16.3,
Release, 8192-frame blocks), and the device's interpreter has smaller caches for the generated 68k core. What this
branch measured on x86 is in [spec/08](08-profiling.md): 26 to 28x for the three chip-register images, 15.6 to 18.7x for
the four timing images, and 17.7x for a real genmddj song clocked through port 2. The start is the spike's arm64 builds
and its unrun `device-bench.sh` in `.superpowers/mdspike/arm/deploy/`: running them is this entry.

### The bindings menu's Mega Drive extras

The Mega Drive's 6-button pad has four buttons the bindings editor cannot reach: X, Y and Z (genmddj uses them for its
mixer) and Mode. The wire has them (`BUTTON_VALUE` names X 8, Y 9, Z 10 and Mode 11, and the console's native table reads
all four), so a hand-edited `bindings/*.json` binds them and reaches the pad; the default profile deliberately binds none,
so no recorded input matrix moves. The editor's rows are the bare eight (`GB_BUTTONS` in `menuDefs.ts`), and the UI has
no per-platform label machinery: a console's buttons have to come from its descriptor, since the shell names no platform.
Closing this is a descriptor field naming a pad's buttons and labels, the editor reading the focused system's console,
and a row per button. ([design](../docs/megadrive-blastem-design.md) section 1.)

## Deferred

Intentionally not planned; listed so the intent isn't lost. These are decisions, not work, so they have no checklist
line.

- **About panel**: dropped (it offered little); the one menu item still marked deferred in
  [menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts).
- **LV2**: deferred indefinitely; its out-of-process DSP/UI split does not fit RetroPlug.
- **The `ui?` render descriptor and a third-party extension model**: a role's own settings UI (the field is still on
  `RoleType`, unconsumed), and registering role types, ROM providers and behaviours from outside the tree, which
  [Downloadable packs](#downloadable-packs) would carry.
- **Savestate slots and a sav inspector**: never built; the state-snapshot machinery and the pure-TS sav codec exist, a
  multi-slot feature and a React sav view do not.
- **A web / Emscripten port**: design only ([modular-graph-design.md](../docs/modular-graph-design.md) section 11).
- **`MemoryType` stays a closed wire enum, F2's item**, of nine regions
  ([MemoryType.hpp](../packages/system/native/src/system/MemoryType.hpp)): a tenth changes the wire, the debug RPCs and
  the TS mirror together, and it is not a build seam ([F2 design](../docs/modular-graph-phase-f2-design.md) section 10).
- **One Mesen library for every product**, built with the root's options: a product that wants fewer consoles, or an
  embedder who wants SNES back, flips a cache variable (`tools/build-mesen-all-consoles.sh` proves the ON build); a
  library per console set is a later question the compile-out filter's table is written to make cheap ([F2
  design](../docs/modular-graph-phase-f2-design.md) section 10).
- **A standing note**: `deferredProject` in [systemsStore.ts](../packages/retroplug/src/systemsStore.ts) is the
  sibling-`.rplg` load-handoff concept, an intentional domain term, not incomplete work.

# Phase F5b: the SDL standalone's audio and MIDI as packages

**Date:** 2026-11-11 · **Branch:** `feature/modularize` **Status:** designed `793c655fd`; prototyped as six stages on the
unpushed `proto/f5b-audio-midi` (each reviewed and fixed; the as-built record is section 13); executed on
`feature/modularize` as the plan `3c860286e` and six task commits `605c8b6a3`..`27f99be3d`, then the final review's
follow-up (`15f65bfa8`, `61462c6ed`, `374998d56`, `bc97b50ac` and the section-14 commit). F5b is built (section 14). The
design was written over `f0f56c3ff` (the F5a tip). The
user's decisions are the `### F5` entries of `.superpowers/f-split/decisions.md` (the 2026-11-11 ones); the user approved
the design section by section as presented (packages and product wiring; the audio interface and `AudioHost`; the MIDI
interface, `MidiStream` and its users; config sections, settings and startup; build, proof, edges and process), which
this document writes out with the verification the presentation did not have. Where the code did not match what an
approved section assumed, or a section left a real choice open, the draft's report
(`.superpowers/f5bpro/draft-report.md`, items 1 to 22) put it to the user and the controller; every answer (the last
`### F5` entry of `decisions.md`, "F5b draft-report items") is folded into the text and rows 28 and 29 of the decisions
table, and the text cites an answer as "report item N".

**After the prototype.** Sections 1 to 12 are corrected in place to what the prototype built (the stages in section 10
are the six that ran; a departure is stated where the text it replaces stood), and section 13 is the record: the stage
commits, what each review found and each fix pass changed, every departure from the design as committed in one list, the
verification, the edges as built, and what acceptance must run beyond the kit. The prototype's history was rewritten by
its fix passes, so the shas in section 13 are the final ones; the stage commits are the units the plan re-implements.

F5b is the second of the two phases that close the package split's open list. F5a
([modular-graph-phase-f5a-design.md](modular-graph-phase-f5a-design.md)) made the shell free of the Launchpad and gave
the plugin its own product list; its section 12 named this phase first: the SDL standalone's audio and MIDI as packages
behind generic backend interfaces, after `feature/handheld-perf`'s render-ahead and `feature/sdl-standalone`'s `MidiIo`
ring fix landed or were decided. The survey behind it is `.superpowers/f-split/f5-survey.md` part A, taken at
`787313f39`; the release-readiness list it feeds is `docs/sdl-release-readiness.md` on `feature/sdl-standalone`.

The phase is a boundary refactor with several visible behaviour changes for one population, the users of the SDL
standalone (section 8): no audio change in the engine, no graph document change, no plugin change, and no baseline
re-recorded except the recorded menu matrix's three audio/MIDI cases and the pieces section 8 names.

## 1. Goal and scope

**Goal.** `packages/native/sdl/main.cpp` (1945 lines) stops owning the audio device and the MIDI ports. Both become
packages behind interfaces a second implementation can fill: PortAudio, SDL2 audio and a null backend for audio; RtMidi
and an in-process fake for MIDI. The MIDI stream, today's `MidiIo`, is written once over the interface and serves the
SDL app and the CLI; the Launchpad pack drops its own smaller port seam and its hand-kept copy of the port filters. What
the device, driver and ports are called stops being PortAudio's or RtMidi's vocabulary and becomes names a saved file
can carry. Audio settings become a stamped section of `config.json`, MIDI selection becomes a section of the project.

**Scope (one phase, six stages, by mechanism).** The branch merge (S0, section 3); the `midi` package, the Launchpad
pack onto it and the CLI's facet as a provider (S1, section 4); the `audio` packages, `AudioHost` and `AUDIO_BACKENDS`
(S2, section 5); the `main.cpp` split (S2b, section 5.5: a stage of its own, because the first prototype pass left it
undone and the review wanted it a pure move); the TypeScript half: config sections, the `controlSurface` move, the
`audio` and `midi` sections, rows and startup (S3, section 6); the SDL-audio leg of `sdl:pipewire` and the docs (S4,
section 7). It runs as one prototype in six stages (section 10).

**Not in F5b**, and where it goes (section 12):

- **Per-device MIDI routing into the graph** (each port its own source and destination, host MIDI I/O nodes per port,
  `buildGraph` routing, menus): the user wants it, built on this phase's schema (entries are objects so it adds a
  field).
- **Merging `feature/handheld-perf`.** `4b9e39ea8` (render-ahead), `39136feea` (tile redraw) and `d64424b95` (muOS idle)
  stay unmerged and rebase onto F5b; render-ahead's idea is ported into `AudioHost` from `4b9e39ea8`'s code, with no
  muOS device run as its proof here.
- **MIDI-in frame offsets** (a message still stages at frame 0), **device hotplug** (a port or device list is read when
  asked, not watched), **MIDI clock phase lock**, **Windows GUI subsystem, macOS bundle, release packaging** and the
  other items of `docs/sdl-release-readiness.md` that are not audio or MIDI plumbing.
- **The plugin and the DPF standalone**, which get audio and MIDI from DPF and are untouched; the plugin lists none of
  the new packages.
- **Windows, macOS, arm64 and CI** for every new package, target and script: first run on push (section 11).

**What stays declared, said plainly.** `packages/retroplug/package.json` gains `@retroplug/audio` and `@retroplug/midi`
(the generated modules import their app and UI modules by name, as for the Launchpad); `audio-portaudio` and `audio-sdl`
have no TS, and are not declared by the shell, as `n8-wire` is not.

**What the tree has** (checked against `f0f56c3ff`; every figure was counted or opened).

- **The audio region of `sdl/main.cpp` is about 700 lines in nine groups.** PortAudio enumeration and the two JS hooks
  (`:476-632`: `availableHostApis`, `outputDevicesForHostApi`, `autoOutputHostApiIndex`, which prefers `paPipeWire`,
  `jsGetAudioConfig`, `jsSetAudioConfig`); the transport and MIDI hooks (`:634-702`); the RT tuning (`:808-830`:
  `tuneAudioThread` pins the last core and sets `SCHED_FIFO` 80, `pinUiThreadAwayFromAudioCore`); `renderAudioBlock`
  (`:837-910`); the PortAudio callback with its once-per-process `static tuned` latch (`:912-921`); the sleep-paced
  fallback pump (`:927-936`); `startAudio` / `stopAudio` (`:939-947`, the `Pa_StartStream` / `Pa_StopStream` returns
  ignored); the log quieting (`:1024-1089`, run before `Pa_Initialize`); `openAudio` (`:1091-1166`); `audio.json` and
  `midi.json` (`:1168-1252`); `reconfigureAudio` and `reconfigureMidi` (`:1254-1292`). `AppState` is `:187-333`.
- **The device is opened before the control plane exists.** `setupSdl` (`:1384-1443`) calls `quietenAudioLogs`,
  `Pa_Initialize`, `loadAudioConfig` and `openAudio` (`:1437-1441`); `bootControlPlane` (`:1311-1342`) then sets the
  engine's sample rate from the obtained rate at `:1335`, "before the bundle composes / autoloads". `main` runs
  `setupSdl` (`:1665`), `bootControlPlane`, the autoload (`:1670`), `setupUi` (`:1674`), `restoreControlSurface`
  (`:1680`), `loadMidiConfig` and `midi.open` (`:1686-1687`), `rp_restore_providers` (`:1690`), `setHostOutputs`
  (`:1700`) and `startAudio` (`:1706`). Section 6.6 is built on that order.
- **MIDI hot-swap needs audio stopped, and three callers do it.** `reconfigureMidi` (`:1284-1292`), `onReserveMidi`
  (`:959-969`, only when `reservationChanges`) and `onSuspendInputs` (`:971-984`, only where `scanSuspendsInputs`) each
  `stopAudio` / `startAudio` around a `MidiIo` reopen, because `send()` runs on the audio thread and a PortAudio stream
  stop is the only quiesce the file has. On PipeWire a stream stop is `pw_stream_disconnect`, so each one drops the
  stream's links (`docs/sdl-release-readiness.md` section 1, still unticked on that branch).
- **`MidiIo`** is `host/input/MidiIo.{hpp,cpp}` (297 + 297 lines): pure helpers in the header (`midiSystemUsable` `:22`,
  `scanSuspendsInputs` `:31`, `midiPortAllowedBy` `:58`, `isHardwareMidiPort` `:89`, `hardwarePortIndices` `:97`,
  `matchPortIndex` `:110`, `kAllInputs` `:123`, `inputPortsToOpen` `:132`, `extractRealtime` `:153`), the class
  (`:170`), a 256-slot ring (`:284`) whose producer side is the race the merged branch fixes. `listInputs` /
  `listOutputs` open a probe object each call (`MidiIo.cpp:95-125`); there is no hotplug. The macOS CoreMIDI guard is
  `:18-56`.
- **The Launchpad pack has a smaller seam of its own**: `IMidiPort` and `MidiPortOpener` (`MidiPort.hpp`, 33 lines), the
  only RtMidi translation unit `RtMidiPort.cpp` (146 lines: its own `midiSystemUsable`,
  `rtMidiPortOpener("RetroPlug Launchpad")`, `listMidiPorts`), and `MidiPortFilter.hpp` (89 lines: the second copy of
  the allow list and of `isHardwareMidiPort`, which `retroplug-midi-test` holds equal to `MidiIo.hpp`'s over one table,
  `MidiPortSelect.test.cpp :237-290`). `ControllerLink`, `MidiPortPool`, `LaunchpadProvider` and the tests take the
  opener (`ControllerLink.hpp:57`, `MidiPortPool.hpp:42`, `LaunchpadProvider.hpp:23`, `test/FakePorts.hpp:115-126`).
- **Three targets link RtMidi and one links PortAudio.** `rtmidi` is added at root scope (`cmake/rtmidi.cmake`) since
  F3; `retroplug-sdl` links it and `portaudio` directly (`packages/native/CMakeLists.txt:344-345`), `retroplug-cli` gets
  `MidiIo.cpp`, `MidiRpcService.cpp` and `rtmidi` from `deps.cmake:19-27` under `RETROPLUG_N8_BRIDGE`, and PortAudio's
  options and `add_subdirectory(deps/portaudio)` sit in `deps.cmake:29-61`, which is configured after the packages.
  `retroplug-launchpad-desktop` links `rtmidi` PRIVATE, and is linked by every product that names `launchpad` in
  `PROVIDERS` (host, CLI, SDL), so the CLI's `RETROPLUG_N8_BRIDGE=OFF` does not keep it free of RtMidi today (the option
  is deleted, report item 7).
- **Persistence is native-owned and unversioned.** `audio.json` (`AudioCfgJson`, `:1187-1198`: `sampleRate`,
  `blockSize`, `outChannels`, `hostApi` as a `PaHostApiTypeId`, `outputDevice`, and the MIDI Clock tempo `clockBpm`) and
  `midi.json` (`MidiCfgJson`, `:1229-1232`: one `input`, one `output`) are read before the JS runtime exists and written
  by `writeTextFile`, which truncates in place. `config.json` is TS's, versioned (`USER_CONFIG_SCHEMA` 3,
  `userConfigSerialization.ts:15`), read through a strict schema that strips unknown root keys (`userConfig.ts:98-109`),
  and written through `UserConfigStore.update()`'s re-read (`userConfigStore.ts:211-216`).
- **The UI reaches the seams through eight optional globals** (`ui/host/nativeHooks.ts:53-60`): `__rp_getAudioConfig` /
  `__rp_setAudioConfig`, `__rp_getMidiConfig` / `__rp_setMidiInput` / `__rp_setMidiOutput`, and the transport trio. The
  rows live in `ui/screens/menu/menuDefs.ts` (`:137-290`, `:1349-1350`, the MIDI Clock row at `:160-199` and `:1490`),
  `audioDraft.ts` (137 lines) and `midiDevices.ts` (60 lines), and show only where `native.__rp_isStandalone` and the
  hook exist.
- **The tests that cover these today.** `retroplug-midi-test` (24 cases, pure helpers and the twin tables; one more, the
  ring, arrives with S0); `retroplug-launchpad-test` (53 cases over the fake rig); `test-ui/audio-device`,
  `audio-driver`, `midi-settings`, `midi-clock`, `ui-services`; the recorded menu matrix's three cases that set `midi` /
  `audio` (`test/menu/recorded-cases.ts:458-478`) and `test/menu/leaves.test.ts:1220-1242`; and the three harness
  scripts (section 9). Nothing tests `MidiIo.cpp`'s open and reopen paths, and nothing tests `openAudio`.

## 2. The decisions

| # | Decision | Chosen | Alternatives not taken |
| --- | --- | --- | --- |
| 1 | Branches | Merge `feature/sdl-standalone` (the `MidiIo` ring fix `e059a7862` + `1304e33a3`, `MidiIoRing.test.cpp`, the release-readiness doc) into `feature/modularize` as F5b's first step. `feature/handheld-perf` (`4b9e39ea8` render-ahead, `39136feea`, `d64424b95`) stays unmerged and rebases onto F5b later; render-ahead is designed into the new audio layer from `4b9e39ea8`'s code, without the muOS device run as its proof here (user, B) | Land both first; decide neither |
| 2 | Audio scope | PortAudio, SDL2 audio and null, every listed backend compiled in and chosen at RUNTIME; the Driver row lists e.g. "PortAudio: PipeWire" and "SDL: pipewire" (user; supersedes a build-time choice, one real backend per binary, which was C) | One backend per binary chosen at build time (C); PortAudio only |
| 3 | MIDI scope | One `midi` package owns the backend interface, RtMidi and an in-process fake, `MidiIo` rewritten over the interface with one copy of the port filters, and the CLI's `MidiRpcService`; the Launchpad pack depends on it and drops `MidiPort.hpp`, `RtMidiPort.{hpp,cpp}` and `MidiPortFilter.hpp` (user, A). The real-Launchpad check stays manual | Audio only; MIDI interface without the rewrite |
| 4 | Persistence: audio | Audio settings move to TS's `config.json`, stamped, as a package-contributed section; `audio.json` is ignored (reset once, no import: the SDL app never shipped) (user) | Native keeps the file and gains a stamp |
| 5 | Persistence: MIDI | MIDI port selection is PROJECT-ONLY (the `midi` package's project section); no global default, a new project opens with the virtual ports only; the stored names infer the matching device on another machine; `midi.json` is ignored (user: "more explicit - if a user moves to a different machine and they have a complex setup with multiple midi devices, they will have to change the settings anyway") | A machine-wide selection; both |
| 6 | The clock tempo | Moves to the project (`midi.clockBpm`); the transport's play state is not persisted, as today (user) | Stays in the audio settings |
| 7 | Selection shape | Lists, merged into the one host MIDI in and mirrored to every selected output; entries are objects (`{ name }`) so per-device routing can add a field later with no section version bump (user, A). FOLLOW-UP the user wants: per-device MIDI routing into the graph, built on this schema | One input and one output; entries as bare strings |
| 8 | Packaging | A contract package plus one package per backend (approach 1): `audio`, `audio-portaudio`, `audio-sdl`, `midi`; `controlSurface` moves into the Launchpad pack as a config section (user) | One `audio` package holding all backends |
| 9 | Product wiring | A new manifest item kind `{ target, audioBackend }`, modelled on `provider`; a product names its backends in preference order, `AUDIO_BACKENDS portaudio sdl`; a product that lists the package without naming it drops the target, so the CLI, the headless host and the Node addon link neither PortAudio nor SDL audio (approved section 1; the product names backend ids and the manifest item carries an `"id"`, report item 3; `retroplug-midi` and `retroplug-audio` stay out of the manifest `native` list, report item 4) | A backend list in CMake only |
| 10 | `retroplug-midi` is one library, `retroplug-midi-facet` a provider with an rpc | The CLI mounts `MidiRpcService` by naming `midi` in `PROVIDERS`; the SDL app uses `MidiStream` directly and does not. RtMidi is the only real MIDI backend, so there is no `MIDI_BACKENDS` list: callers get the RtMidi one from the package and tests inject the fake | A `MIDI_BACKENDS` list |
| 11 | Audio interface | `AudioBackend` / `AudioStream`, a saved choice is `(backend, driver, device)` by name and never a library's numeric id; Auto = the first backend in the product's `AUDIO_BACKENDS` order whose auto driver opens, PortAudio's auto keeping today's PipeWire preference; SDL2 runs one audio driver at a time, so its backend lists real devices only for the active driver and offers "Default" for the others (stated, not flipped under a running stream). `AudioHost::describe()` enumerates only the backend IN FORCE (enumerating a backend is its library's first use); a sibling is asked for on an explicit step (5.2, 6.5) | A numeric driver id; flipping drivers to enumerate |
| 12 | `AudioHost` | Lives in `retroplug-audio`, never names the Engine; owns open / start / stop / reconfigure (once), the fallback to `null` with a status TS shows, a lost device and a no-pull watchdog as the same fallback, render-ahead from `4b9e39ea8`, the real-time tuning per thread, `RETROPLUG_SDL_AUDIO_STATS`, and keeps today's log line form | Each backend owns its own loop |
| 13 | Render-ahead | A backend-neutral feature of `AudioHost` (`renderAheadFrames > 0`: a render thread fills a ring in `blockFrames` blocks, the stream opens with `blockFrames 0`, the pull only copies; Linux RT priorities callback 85 above renderer 80, core pinning); set by `--render-ahead` / `RETROPLUG_SDL_RENDER_AHEAD`, not a setting | A setting; merge the branch as is |
| 14 | MIDI interface | `MidiBackend` / `MidiPort` in `rp::midi`, one exception type for a refused open, `supportsVirtual()` false on Windows (said, not found out by a catch), the port filters once, `systemMidi()` (RtMidi behind the CoreMIDI guard), `FakeMidiBackend` | The Launchpad's opener seam grown in place |
| 15 | `MidiStream` | Today's `MidiIo` over a `MidiBackend&`: exact names (TS matches), `setInputs(names, all)` / `setOutputs(names)`, the reservation and the scan suspension over lists, many producers serialised by a mutex into the ring with one consumer, a drop count, `send` to the virtual output and every selected hardware output | Per-port rings |
| 16 | No audio stop around a reopen | Inputs need nothing (destroying a port cancels its callback; the audio thread only reads the ring); outputs are an immutable set the audio thread reads through one pointer, a change publishes the new set, waits out the send in flight (one send, none when audio is not running; the handshake is `seq_cst` throughout, a bounded wait that yields and then sleeps), then frees the old ports. Settings > MIDI, a project switch and the Launchpad's reservation stop costing an audio stop and restart. MIDI-in frame offsets stay 0 | A pause handshake with the callback |
| 17 | Config sections | `definePackage({ configSections })` with F5a's section shape (`key`, `schema`, `defaults`, optional `version` + `migrations`, `merge`, refuse-newer per section); a section is a non-core ROOT key of `config.json` (core keys stay the shell's: `schemaVersion`, the bindings, `defaultZoom`, `sramAutoSave`), a key with no contributor, and a contributed block this session did not set, is kept verbatim (6.1), writes keep today's re-read; a generic `config` host group (`section(key)` / `setSection(key, patch)`) replaces `ConfigHost.controlSurface`; the Launchpad's `controlSurface` is its section under the same root key, so existing bytes do not move | Per-package files |
| 18 | The `audio` section | `{ backend: "auto" \| "portaudio" \| "sdl" \| "null", driver: "", device: "", sampleRate: 48000, blockSize: 512, outChannels: 2 }`, today's defaults; render-ahead stays a flag, not a setting, and `--block-size` / `--render-ahead` override the saved choice for that run (report item 8) | Backend-specific fields |
| 19 | The `midi` section | `{ inputs: [{ name }], allInputs: false, outputs: [{ name }], clockBpm: 120 }`, key `midi`, distinct from the shell's `midiRouting`; a fresh project has no ports | A global default selection |
| 20 | Name matching | `midi/ts/portMatch.ts`, pure: exact first, then TIERS with the volatile parts stripped (ALSA's `NN:NN` numbers, then RtMidi's trailing WinMM index; the design's `N- ` prefix rule was dropped for want of a real listing), each of which must find exactly one port; otherwise the entry stays saved, shows "<name> (missing)" and opens nothing; tested against a table of real ALSA / WinMM / CoreMIDI names | Match in native |
| 21 | Reaching native | `core/host.ts` gains `audioDevice` (`describe`, `apply`, `status`) and `midiDevice` (`describe`, `apply`, the transport and clock calls); the shell's UI host offers them only where the SDL hooks exist, so the plugin and the DPF standalone hide the rows as today; as built, four hooks replace `__rp_getAudioConfig` / `__rp_setAudioConfig` (`__rp_audioDescribe`, `__rp_audioBackendDrivers`, `__rp_audioApply`, `__rp_audioStatus`) and two replace the MIDI trio (`__rp_midiDescribe`, `__rp_midiApply(sel, mode)`) | Keep the eight globals |
| 22 | Menus | Settings > Audio: one Driver row ("Auto (PortAudio: PipeWire)", "PortAudio: PipeWire", ... for the backend in force, and a "More drivers..." step that asks for the next sibling backend's, once per visit and not at all under a `--audio-backend` pin), Output Device, Rate, Block, Out Channels, Apply (the draft model stays), a status row when muted. Settings > MIDI ("saved with this project"): an inputs checklist plus "All devices", an outputs checklist, the missing entries, a Rescan row, applied at once and dirtying the project. The MIDI Clock submenu moves into `midi`, its tempo saved in the project | Keep the rows in the shell |
| 23 | Startup | The rendezvous widens: it waits for TS's first `audioDevice.apply` from the `audio` section (or the 500 ms bound), which RECORDS the request; native opens the device afterwards at its own old point, after `midi.open()` and the providers' restore, with the recorded request or the defaults (Auto). The virtual MIDI ports open at once, the hardware ports when a project's section is applied; a project switch re-applies with no audio stop (cores are built at the default rate and re-rated once at `prepare`, report item 1; `sdl:controller` is rewritten for it, report item 15) | Keep opening audio in `setupSdl` |
| 24 | Product lists | `desktop` gains `audio`, `audio-portaudio`, `audio-sdl`, `midi` (`midi` before `launchpad`, the audio packages after `core`); `plugin` lists none: a project's `midi` block passes through it verbatim | The plugin lists them |
| 25 | The shell loses | `MidiIo.*`, `MidiRpcService.*`, `registerMidiRpc`; `deps.cmake`'s PortAudio and RtMidi wiring; `retroplug-sdl`'s direct `portaudio` / `rtmidi` links; `userConfig.ts`'s `controlSurface` schema and `ConfigHost.controlSurface`. `retroplug-sdl` keeps `renderAudioBlock`, `MidiClockSync` and the engine handoff, and `main.cpp` splits into `app.hpp`, `window`, `input`, `hooks`, `audioLoop` and `main`, in the namespace `retroplug::sdl` (stage S2b) | Leave `main.cpp` whole |
| 26 | Build and proof | `AUDIO_BACKENDS` joins `retroplug_product` with its configure refusals (4 product-level, 6 manifest-level as built); `portaudio` stays added at root scope (moved out of `deps.cmake`) and is linked only by `retroplug-audio-portaudio`; `retroplug-audio-sdl` links the SDL2 the root already requires; RtMidi is linked only by `retroplug-midi`; `retroplug-sdl` names both real backends so CI's `all` compiles both on all four platforms; `--audio-backend <id>` / `RETROPLUG_SDL_AUDIO_BACKEND` overrides the saved choice for one run | |
| 27 | Process | An Opus-reviewed prototype `proto/f5b-audio-midi` in six stages S0, S1, S2, S2b, S3 and S4; then the plan (DeepSeek), per-task implementers (Qwen, each task's tree held to the prototype's), acceptance, the Opus final review, a follow-up. Owed, manual: the real Launchpad, the muOS device run of render-ahead after the rebase, Windows and macOS | |
| 28 | The draft-report decisions that are the USER's | Item 1: keep the approved startup (audio opens after TS's apply, cores built at the default rate then re-rated once) plus an engine-graph case holding a re-rate to 44.1 kHz bit-equal to a fresh build for a GB and a NES core; move the rendezvous before the autoload only if that case fails. Item 6: honour the CLI contract, `midiOpen("")` (and `n8-bridge` with no `--midi-in`) opens EVERY hardware input again, with a `test:native` case (a CLI edge). Item 8: `--block-size` and `--render-ahead` override the saved choice for that run, like `--audio-backend`. Item 10: PortAudio's Auto prefers WASAPI on Windows (an edge; the Windows agent verifies) | |
| 29 | The draft-report decisions that are the CONTROLLER's | 2: reconfigure is stop, release, open, prepare(`info()`), start (adopts the obtained rate). 3: ids in `AUDIO_BACKENDS` plus an `"id"` on the manifest item. 4: `retroplug-midi` / `retroplug-audio` stay out of the manifest `native` list, linked by name (the native boundary check must accept it). 5: the Launchpad holds its own RtMidi backend instance under `RetroPlug Launchpad`. 7: `RETROPLUG_N8_BRIDGE` deleted. 9: `AudioHost` adds a no-pull watchdog. 11: the shell keeps the Settings containers; placements `settings.audio` and `instance.clock`. 12: re-resolve at project load and on opening Settings > MIDI, plus a Rescan row (as built: on EDGES, never on a render, 6.5). 13: `sdl:smoke` pins `null` and drops the stale `SDL_AUDIODRIVER` export. 14: the binary is `retroplug-audio-host-test`. 15: `sdl:controller` rewritten as section 9 says (told to the user). 16: accept (E10). 18: as drafted. 19: as approved, a retire list only on a Windows stall. 20: noted. 22: `find_package(ALSA)`, linked on Linux. `config.json`'s verbatim-keep and the `controlSurface` move land in one commit | |
| 30 | Decisions taken DURING the prototype: the user's | (a) the Zoom L6 is NEVER opened by any harness, not even PortAudio's capability probe: the baseline ran without the kit's `sdl` leg and without `sdl:controller` / `sdl:pipewire`, and from S2 on `sdl:smoke` and `sdl:controller` pin the `null` backend. (b) `sdl:pipewire` (the PortAudio legs and the new SDL-audio legs) runs in F5b under the `/dev/snd` guard on its private PipeWire server, its ALSA-fallback leg pointed at a private ALSA config whose default is the null PCM, with an `strace` of the first run proving nothing under `/dev/snd` but `seq` is touched, and joins the kit as a standing leg | Run `sdl:pipewire` unguarded; leave it owed |
| 31 | Decisions taken during the prototype: the controller's, S0 and S1 | (a) The guard: an `LD_PRELOAD` shim refusing every `/dev/snd` node but `seq` and `timer`, on top of the null pin (first `.superpowers/verify/nosnd.so`; tracked as `tools/sdl-audio-guard/` after the last review, 13). (b) Every S1 review finding is a fix pass amending S1 (the `seq_cst` handshake, the bounded wait, the single-sender assert, a re-pick that reopens, the midi targets under `/W4 /WX`, `check:boundaries` learning library targets, the proof gaps), and the kit gains a `tsanmidi` leg. (c) The `rp.midi.` node-type family is shared: `core` owns `rp.midi.route` (exempted in the literal scanner), the `midi` package owns every other `rp.midi.*` type | Decide at the per-device routing follow-up |
| 32 | Decisions taken during the prototype: the controller's, S2 and S2b | (a) Every S2 review finding folded into S2, the S1 findings by autosquash, and `main.cpp`'s split made its own stage S2b after the review. (b) A watchdog trip stays muted until the next apply (E6): the status row and Apply, live while muted, are the retry, with no backoff. (c) The interim Settings > Audio hooks keep TS's contract until S3 replaces both sides (`choiceForDriverLabel`, which S3 deleted with the label mapping). (d) `sdl:pipewire`'s PortAudio legs pin `--audio-backend portaudio` from S2 | Backoff retry after a trip |
| 33 | Decisions taken during the prototype: the controller's, S3 | (a) The Driver row never asks a sibling backend from a menu builder: the explicit "More drivers..." step, once per visit, hidden under a pin, and the native hook refuses under a pin. (b) `--audio-backend` replaces only the backend and keeps the saved driver and device when the saved section names the same backend. (c) The `audio` apply runs after the control surface's claim (the old order kept, 6.6). (d) `UserConfigStore.setSection` computes over the re-read file. (e) A contributed block is written back verbatim unless this session set it. (f) The guard logs each refusal and each audio-library dlopen, and the kit gates the null-pinned legs on an empty log (leg `nosnd`) | |
| 34 | Decisions taken during the prototype: the controller's, the whole-prototype review | (a) The MIDI re-resolve is edge-triggered through a new `MenuBuilder.onOpen`, not requested by a builder. (b) The native audio apply only RECORDS the request in the rendezvous and the device opens at the old point (after `midi.open()` and the providers' restore): no new edge. (c) The guard is tracked source, built by each harness into its scratch directory and always in front of `LD_PRELOAD` (unless the same guard is already there). (d) `sdl:pipewire` gains an Auto leg with no flag, its ALSA-fallback leg's text says what it holds, skipped legs are counted. (e) S1's and S3's commit messages reworded to describe the feature, not the review | |
| 35 | Decisions taken during the prototype: bookkeeping | (a) Fix passes rewrite history, so the readiness ticks and this document name a stage, not a hash, and the shas in 13 are the final ones. (b) `check-doc-links` reports a dead link inside a `*-plan.md` record as a WARNING, rather than editing the record. (c) R10's `lost()` leg on a really removed device is not built (owed, not a possibly-flaky kit leg). (d) `docs/launchpad-plan.md` is left as it was | |

## 3. Stage 0: the SDL branch merge

**What it is.** `git merge feature/sdl-standalone` into `feature/modularize`, before anything else, so that the ring fix
and its test exist when S1 rewrites the code around them. The branch is five commits over `cfeb80e00`
(`git log --stat cfeb80e00..feature/sdl-standalone`): `b9105a004` (the release-readiness checklist and a pointer from
`docs/sdl-standalone.md`), `e059a7862` (`MidiIo`'s ring producers serialised on a `pushMutex_`; `MidiIoRing.test.cpp`,
98 lines, one case: two producers, every message intact, once, in order; `retroplug-midi-test` now compiles `MidiIo.cpp`
and links `rtmidi`), `71d7b8ff4` and `d51c4a4ef` (doc ticks) and `1304e33a3` (review follow-ups: `kCapacity` made
public, `main.cpp` reserves `midiScratch` to it, the test and `run-plugin-tests.mjs`'s comment reworded).

**What changes, by file** (the merge's own diff, no hand edits beyond the conflicts):

- `packages/native/src/host/input/MidiIo.{hpp,cpp}`: `pushRing` takes `pushMutex_` around the tail, the sequence number
  and the slot write (`MidiIo.cpp:206-220` on the branch); `poll()` stays lock-free; `kCapacity` is public.
- `packages/native/test/midi/MidiIoRing.test.cpp`: new.
- `packages/native/cmake/tests.cmake:482-489`: `retroplug-midi-test` gains `MidiIoRing.test.cpp`, `MidiIo.cpp` and
  `LIBS rtmidi`. **Conflict** with this branch's own edit of the same block, which added the pack's include root
  (`INCLUDES ... ${CMAKE_SOURCE_DIR}/packages/launchpad/native/src`, for the twin tables). Resolution: keep both; the
  block moves in S1 anyway.
- `packages/retroplug/scripts/run-plugin-tests.mjs:6-10`: a comment. **Conflict** with F5a's reworded header; keep this
  branch's text and add the ring clause.
- `packages/native/sdl/main.cpp`: one hunk, `midiScratch.reserve(retroplug::MidiIo::kCapacity)` after `midi.open`; the
  branch's context line is `app.n8Host.restore()`, which this branch replaced with `rp_restore_providers`
  (`main.cpp:1690`), so the hunk is placed by hand beside it. It is the one non-mechanical edit of the stage, and it
  moves into `AudioHost`'s setup in S2 (the scratch is `MidiStream`'s consumer buffer).
- `docs/sdl-release-readiness.md` (new here) and `docs/sdl-standalone.md`: the checklist the phase ticks (section 7).

**Proof.** `retroplug-midi-test` built and run (25 cases: the 24 and the ring), and built in `build-tsan/` and run under
`TSAN_OPTIONS=halt_on_error=1` (the branch's own proof: the plain build aborted or segfaulted three runs of three
without the fix); `pnpm test:plugin`; `pnpm sdl:smoke`; a configure and `all` build. No oracle moves. The merge commit
is the stage's commit; a trial merge (`git merge-tree`, run by the controller) conflicts in exactly
`packages/native/cmake/tests.cmake` and `packages/retroplug/scripts/run-plugin-tests.mjs` and nowhere else, so the stage
needs no review beyond reading the diff.

**As built.** The prototype's `925fd40d9`, replayed on `feature/modularize` as `605c8b6a3` (section 14); the two
conflicts exactly as predicted and resolved as stated; `retroplug-midi-test` ran 25
cases, plain and under TSan three runs of three with `halt_on_error=1`. The Opus review read the merge against
`cfeb80e00..feature/sdl-standalone` file by file and found it exact (five files byte-identical, nothing dropped, nothing
doubled). The stage was never touched by a fix pass.

## 4. Stage 1: the `midi` package

### 4.1 What it is

`packages/midi` (`@retroplug/midi`; dependencies `node-sdk` for the native half's `graph/rp_provider.h` and the byte
string, and `core` and `ui` for the TS half, one list the TS and native boundary checks both read; the native half alone
needs `node-sdk`, report item 20). Two native targets:

- **`retroplug-midi`**: the interface, the RtMidi backend, `systemMidi()`, the fake backend, `MidiStream` and the port
  filters. Links `rtmidi` PRIVATE and `Threads::Threads`.
- **`retroplug-midi-facet`**: `MidiRpcService`, as a provider with an rpc (manifest form in 4.5). Links `retroplug-midi`
  and `retroplug-node-sdk` PUBLIC (the provider header and the RPC's byte string are the facet's alone, so the library
  names no SDK).

`retroplug-midi` is NOT a manifest `native` item but an unlisted library target (report item 4). The packages.cmake rule
links every non-provider target of a listed package into every product that lists it, which would link RtMidi into the
host, the render-host test, the Node addon, `retroplug-ui-test` and every test binary that names the desktop list; it is
unlisted, as `retroplug-launchpad-harness` is, and linked by name by the products that use it, so a product that lists
`midi` without naming it in `PROVIDERS` links no RtMidi.

```cpp
namespace rp::midi {
using Receiver = std::function<void(const std::uint8_t*, std::size_t)>;  // on the backend's own thread

class MidiPort {                       // one endpoint, one direction, owned by whoever opened it
public:
    virtual ~MidiPort();               // cancels the callback: when it returns no receiver call is running or will run
    virtual void send(const std::uint8_t* data, std::size_t n) = 0;       // an input's send() is a no-op
};

class MidiBackend {
public:
    virtual ~MidiBackend();
    virtual std::string id() const;                                        // "rtmidi", "fake"
    virtual bool usable();                                                 // false when the system refuses (CoreMIDI)
    virtual std::vector<std::string> listInputs();                         // every port the system names
    virtual std::vector<std::string> listOutputs();
    virtual std::string clientName() const;                                // the one name its ports are published under
    virtual std::unique_ptr<MidiBackend> withClientName(std::string);      // this system again under another name, or null
    virtual std::unique_ptr<MidiPort> openInput(const std::string& name, Receiver);   // throws std::runtime_error
    virtual std::unique_ptr<MidiPort> openOutput(const std::string& name);            // throws std::runtime_error
    virtual bool supportsVirtual() const;                                  // false on Windows
    virtual std::unique_ptr<MidiPort> openVirtualInput(Receiver);          // "<clientName> In"; throws where unsupported
    virtual std::unique_ptr<MidiPort> openVirtualOutput();                 // "<clientName> Out"
};

MidiBackend& systemMidi();             // RtMidi, behind the macOS CoreMIDI guard (midiSystemUsable)
std::unique_ptr<MidiBackend> rtMidiBackend(std::string clientName);   // another RtMidi system under its own name
class FakeMidiBackend;                 // tests: scripted ports, recorded sends, deliveries on a thread or inline
}
```

This is the interface as built (`MidiBackend.hpp`; `systemMidi()` and `rtMidiBackend()` are declared in
`RtMidiBackend.hpp`, so the contract header names no MIDI library). It differs from the approved sketch in one thing,
which the S2 review made load-bearing: a backend OWNS its client name (`clientName()`, `withClientName()`), and a
virtual-port open takes none, so the ports a stream opens and the ports it publishes cannot be named differently, and a
CLI session that wants the name "RetroPlug N8" is given another backend, not a parameter. Four things the design left to
the prototype, each verified against the code and none a decision: **(a)** the listing and the filters: `listInputs` /
`listOutputs` return every port the system names, and the one copy of `isHardwareMidiPort`, `midiPortAllowed` and
`hardwarePortIndices` filters them in the callers (`MidiStream`, and the Launchpad's lister), so the fake can be tested
against the filter; the RtMidi backend's `openInput` / `openOutput` additionally refuse a name the allow list hides, as
`rtMidiPortOpener` does today (`RtMidiPort.cpp:112-115`, "MIDI port not allowed"). **(b)** The macOS guard is one
function in `systemMidi()`'s translation unit, as `midiSystemUsable` is today in both copies (`MidiIo.cpp:33-56`,
`RtMidiPort.cpp:27-42`); the Launchpad's copy goes. **(c)** `supportsVirtual()` replaces a catch: RtMidi's WinMM
`openVirtualPort` does not throw, it raises a WARNING (`deps/rtmidi/RtMidi.cpp:2903-2908`, `3125-3130`) that prints and
returns, so today on Windows `MidiIo::open` prints two warnings and then logs "virtual ports 'RetroPlug In' / 'RetroPlug
Out' open" (`MidiIo.cpp:88`) for ports that do not exist; the stream now logs what is true (edge E10, section 8).
**(d)** The Launchpad opens its ports under the client name `RetroPlug Launchpad` and the stream under `RetroPlug`,
which the interface as approved did not carry (report item 5): the Launchpad provider holds its own RtMidi backend instance
constructed with the client name `RetroPlug Launchpad`, while `systemMidi()` is the one under `RetroPlug`, so
on-the-wire names and the pack's "a second instance's link ports are not devices" rule are unchanged. The CLI's facet
does the same per session through `withClientName` (4.4).

### 4.2 `MidiStream`

`MidiStream` is `MidiIo` rewritten over a `MidiBackend&` (not a copy of the file: its RtMidi calls, probe objects and
three `reopen` paths go; its policy stays). The public surface as built (the approved one, plus the mode, the log sink
and the opened lists), with the call sites it serves:

```cpp
enum class Apply { Diff, Reopen };          // namespace rp::midi, beside the class

class MidiStream {
public:
    struct Message { std::uint64_t seq = 0; std::vector<std::uint8_t> bytes; };
    static constexpr std::size_t kCapacity = 256;               // the ring; one poll() returns at most this many

    explicit MidiStream(MidiBackend& backend);
    ~MidiStream();                                              // close()

    using LogSink = std::function<void(const std::string&)>;    // every log line the harnesses grep goes through one sink
    void setLogSink(LogSink sink);                              // default: the "[retroplug-sdl] " voice on stderr

    bool open();                                                // virtual "<clientName> In/Out" where supported
    void close();
    bool isOpen() const;

    // The selection: EXACT names (TS matches, 6.3). `all` opens every hardware input and `names` is then ignored. `mode`
    // is how the change treats the ports it names: Diff (the default) re-opens only what moved, which is what a project
    // load does; Reopen lets every named port go and opens it again, the remedy for an unplugged device (Settings >
    // MIDI's picker and the Rescan row ask for it). A name not present is skipped and re-applied by the next call.
    void setInputs(const std::vector<std::string>& names, bool all, Apply mode = Apply::Diff);
    void setOutputs(const std::vector<std::string>& names, Apply mode = Apply::Diff);

    // The control surface's claim and a scan's held edge: the same rules as MidiIo, over lists. Neither stops audio.
    void setReserved(const std::string& input, const std::string& output);
    const std::string& reservedInput() const;   const std::string& reservedOutput() const;
    void setHardwareInputsSuspended(bool suspended);
    bool hardwareInputsSuspended() const;

    std::vector<std::string> listInputs() const;                // hardware, filtered (the pickers' list)
    std::vector<std::string> listOutputs() const;
    bool supportsVirtual() const;
    const std::vector<std::string>& openedInputs() const;       // what actually OPENED (the rows' "(missing)" source)
    const std::vector<std::string>& openedOutputs() const;

    void poll(std::vector<Message>& out);                       // audio thread; lock-free; clears out first
    void send(const std::uint8_t* data, std::size_t n);         // audio thread, its ONLY caller: virtual + every output
    std::uint64_t droppedCount() const;
};
```

What is kept from `MidiIo`, and where it lives:

- **The policy** of `inputPortsToOpen` (`MidiIo.hpp:132`) over lists: none when the list is empty and `all` is false,
  every hardware port for `all`, else each name that matches exactly; a `reserved` port skipped in every case and a
  `suspended` stream opening no input at all.
- **The outputs**: each selected name that is present opens; one equal to the reserved output logs "MIDI out: '<name>'
  is held by the control surface" and opens nothing (`MidiIo.cpp:158-163`, a line `sdl:controller` greps, section 9).
- **The receive ring** with the branch's fix: producers (the virtual input and one per hardware input, each on its own
  callback thread) serialise on a mutex, `poll()` takes none; a full ring drops the incoming message and counts it.
  `kCapacity` is public so the consumer's scratch can be reserved to it (S0's hunk).
- **The log lines** the harnesses grep, kept byte for byte in the `[retroplug-sdl]` voice the stream uses today:
  `MIDI in: connected hardware port '<name>'`, `MIDI in: no hardware device selected (Settings > MIDI)`,
  `MIDI out: connected hardware port '<name>'`, `MIDI in: hardware inputs suspended for a control-surface scan` /
  `resumed`, `MIDI: virtual ports '<c> In' / '<c> Out' open` (only where virtual ports are open), and the `MIDI in:` /
  `MIDI out:` byte dumps under `RETROPLUG_MIDI_LOG`. The name `retroplug-sdl` in the prefix stays: the CLI prints it too
  today.
- **The mode.** `Apply::Diff` / `Reopen` is the S2 review's I7: a re-apply that opened a port before the old handle was
  released lost it on WinMM, where an output is exclusive, and S3's project switch re-applies at every load. A port
  leaving the selection is unpublished, the send in flight waited out, and only then closed, so a reopen asks for the
  name after its old handle is gone (it has a gap, the price of an exclusive re-ask; an ADDED port opens while the set it
  joins is published, so a swap loses no send). `setReserved` and the scan edge diff and never reopen.
- **Gone**: `reservationChanges`, `suspensionChanges`, `inputsFor`, `outputFor` (`MidiIo.hpp:224, 237`,
  `MidiIo.cpp:197-215`): they existed to tell the host whether it must stop audio, and it never must. `onReserveMidi`
  and `onSuspendInputs` become the two lines `setReserved(...)` and `setHardwareInputsSuspended(...)` plus their log
  lines.
- **The default**: an empty `setInputs({}, false)` is None, which is what `MidiIo` has meant since the input default
  changed (`MidiIo.hpp:196-205`); the CLI facet maps its `""` to `all` (report item 6, 4.4).

### 4.3 No audio stop around a reopen

The audio thread touches a stream in two places: `poll` (reads the ring) and `send` (writes to the output ports). Inputs
need nothing: a hardware input is opened and closed by the control thread, destroying its `MidiPort` cancels the
callback before it returns, and the receiver captures only the stream (which outlives it) and a generation-free push
into the ring. Outputs are the hazard, because `send` calls a port the control thread may be destroying.

The outputs are an immutable `OutputSet` (the virtual output's pointer and the hardware ports it NAMES; the control
thread owns the ports) reached through one atomic pointer. A change builds the new set, publishes it (an exchange),
waits for the send in flight, then frees the old one. The wait is an epoch handshake: `send` brackets its use of the set
with a counter the audio thread increments to an odd value on entry and an even one on exit (two atomic increments, no
lock and no allocation on the audio path); the control thread reads the counter after publishing and, if it read an odd
value, waits until the counter has CHANGED. Waiting until it is merely even is unbounded under back-to-back sends (the
review's probe: 556 ms on a 32-thread box, never returning when pinned to one core); waiting for a change is the one
send in flight.

**All four operations on the two locations are `seq_cst`**: the entry increment and the pointer load in `send`, the
exchange and the epoch load in `retire`. The shape is store-buffering, each side writes one location and then reads the
other, and with relaxed or acquire/release orders AArch64 (the handheld and Raspberry Pi builds) may let the sender read
the OLD set while the control thread reads the epoch as even, so the old set would be freed under a send. No test can
hold that: ThreadSanitizer checks happens-before, not weak-memory reorderings, and x86's locked increment is a full
fence, so the comment in `MidiStream.cpp` says why. The wait yields a few times and then sleeps in 100 us steps, so a
170 ms WinMM bulk SysEx is not a spinning UI thread. `send()` is the AUDIO THREAD's alone: the entry asserts it in a
Debug build (`NDEBUG` is defined in `build/` and in both sanitizer trees, so the rule is held by the argument and a case,
not by a run), and `AudioHost` must never let two threads render at once, a watchdog takeover included.

A block runs at most one `send` per pending message, sequentially, so the wait is at most the one send in flight: none
when audio is not running (the counter is even). A port send that blocks (a WinMM bulk SysEx blocks about 170 ms, which
is why the Launchpad writes on its own thread) makes the wait that long, and `MidiStream::send` runs every selected
hardware output's send INLINE on the audio thread, so a SysEx the engine emits to a selected WinMM output blocks the
audio callback itself that long (as `MidiIo` always did; the Launchpad's own link is not in the stream). A retire list
drained from the frame loop is the alternative that never blocks the UI (report item 19: built as approved; it is added
only if the Windows run shows a stall, section 12).

`reconfigureMidi`, `onReserveMidi` and `onSuspendInputs` therefore stop calling `stopAudio` / `startAudio`; the
PipeWire-link drop in `docs/sdl-release-readiness.md` section 1 loses two of its three callers here (the third is
`reconfigureAudio`, a real device change). A project switch re-applies the selection the same way, with no stop.

### 4.4 The users

- **The SDL app**: one `MidiStream` over `systemMidi()` in `AppState` (replacing `retroplug::MidiIo midi`, `:226`), the
  hooks `__rp_midiDescribe()` and `__rp_midiApply({ inputs, allInputs, outputs }, mode)` (6.4), the audio loop's `poll` /
  `send` unchanged (`renderAudioBlock`, `:848-899`), `extractRealtime` and `MidiClockSync` unchanged. Until S3 the app
  kept `midi.json` through a one-name shim (`applyMidiSelection`: `""` is None, `"*"` is `all`, a name is a one-entry
  list), so S1 moved no startup order and no harness line; S3 deleted the shim with the file.
- **The CLI**: `MidiRpcService` over its own `MidiStream`, as `midi`'s provider facet, mounted by naming `midi` in the
  CLI's `PROVIDERS` (`retroplug-cli` joins `n8 launchpad midi`), which retires `#ifdef RETROPLUG_N8_BRIDGE` in
  `cli/main.cpp:29-31, 241-247` in favour of the generated `rp_register_package_rpc`. **`retroplug-host` names `midi` too**
  (this design said the CLI only): that is what links the facet's target into the headless host, which mounts the facet
  over a `FakeMidiBackend` of its own (the two controllers the Launchpad rig lists, an `onSend` hook that answers one
  reply on every open input, so a `test:native` case can count which inputs a stream opened); it is started by hand, as
  the N8 and Launchpad providers are, so no production provider carries a fake. The facet keeps `midiOpen`'s `clientName`
  by owning one stream per session over `backend.withClientName(client)` when the name is not the backend's own, so
  `n8-bridge` ("RetroPlug N8"), `n8-sync` and `launchpad-probe --loopback` still publish the ports a DAW routes to (the
  S2 review's I8). The RPC method names, parameters
  and results do not change (`midiListInputs`, `midiListOutputs`, `midiOpen`, `midiSelectOutput`, `midiPoll`,
  `midiSend`, `midiClose`), so `realBackend.ts:241-247`, the TS sessions and the SDK do not. `midiOpen(client, "")` is
  the one behaviour change: it opens EVERY hardware input again (`""` means `all`, a named input opens that port, the
  launchpad probe's `"(no hardware input)"` is an unmatched name and opens none), as the contract text says and as
  `n8-bridge` with no `--midi-in` expects (report item 6, the user's; today `""` reads none,
  `MidiRpcService.cpp:15-18`); a `test:native` case holds it. The CLI's `ProviderHost` still has no shared stream to
  keep a claimed pair out of (`cli/main.cpp:171-219`), so the Launchpad's reservation does not reach the CLI's
  `MidiStream`, as today.
- **The Launchpad pack**: `ControllerLink` and `MidiPortPool` take a `MidiBackend&` instead of a `MidiPortOpener`
  (`ControllerLink.hpp:57`, `MidiPortPool.hpp:42`, `createProvider`, `LaunchpadProvider.hpp:23`); the provider passes
  `systemMidi()` and its lister passes `listInputs()` / `listOutputs()` through the one filter; the tests use
  `FakeMidiBackend` instead of `FakePorts.hpp`'s own opener. `PortClaim`, the link's fixed-slot rings, the generation
  tag, the writer thread, the close bytes and the facet's twelve methods are unchanged.

### 4.5 What changes, by file

```
packages/midi/package.json
    new: @retroplug/midi; dependencies node-sdk, core, ui; retroplug.app, .ui, and
    retroplug.native = [ { "target": "retroplug-midi-facet", "device": false, "provider": "rp_midi_provider",
    "rpc": { "header": "midi/facet/MidiRpcService.hpp", "register": "rp::midi::registerRpc" } } ]
packages/midi/native/CMakeLists.txt
    new: retroplug-midi (package library), retroplug-midi-facet, the tests
packages/midi/native/src/midi/MidiBackend.hpp
    new: the interface above, MidiPort, the one exception rule
packages/midi/native/src/midi/PortFilter.hpp
    new: midiPortAllowedBy, midiPortAllowed, isHardwareMidiPort, hardwarePortIndices,
    matchPortIndex, kAllInputs, inputPortsToOpen, scanSuspendsInputs, extractRealtime
    (MidiIo.hpp:58-153, one copy)
packages/midi/native/src/midi/RtMidiBackend.{hpp,cpp}
    new: systemMidi(), rtMidiBackend(clientName), the CoreMIDI guard, virtual ports, SysEx on; the header keeps
    the contract header free of any library name
packages/midi/native/src/midi/MidiStream.{hpp,cpp}
    new: MidiIo's policy over the interface, the ring, the output set
packages/midi/native/test-support/midi/FakeMidiBackend.hpp
    new: header-only test support (4.6), reached through the INTERFACE target retroplug-midi-test-support
packages/midi/native/src/midi/facet/
    new: MidiRpcService.{hpp,cpp} (moved), MidiProvider.{hpp,cpp} (rp_midi_provider), registerRpc
packages/midi/native/test/
    MidiPortSelect.test.cpp (moved, the twin cases deleted), MidiStream.test.cpp
packages/native/src/host/input/MidiIo.{hpp,cpp}, MidiRpcService.*
    deleted (the policy and the facet move into packages/midi)
packages/native/test/midi/
    deleted (moved); packages/native/cmake/tests.cmake:482-489 loses the block
packages/launchpad/package.json
    dependencies gains "@retroplug/midi"
packages/launchpad/native/CMakeLists.txt
    links retroplug-midi instead of rtmidi; the test links the fake
packages/launchpad/native/src/launchpad/desktop/
    MidiPort.hpp, RtMidiPort.{hpp,cpp}, MidiPortFilter.hpp deleted; ControllerLink, MidiPortPool,
    LaunchpadProvider, LaunchpadRpc (MidiPortPool.cpp:44-48, ControllerLink.cpp:27-40) use midi/PortFilter
packages/launchpad/native/test/FakePorts.hpp
    the device model (Rig: answering ports, answersOn, refuse, events, sendDelayMs) rebuilt over
    FakeMidiBackend's hooks; still shared with packages/native/src/main.cpp:48, :183-203 (the headless host)
packages/native/sdl/main.cpp
    MidiIo -> MidiStream; reconfigureMidi deleted; loadMidiConfig / saveMidiConfig kept as a
    midi.json shim until S3; onReserveMidi / onSuspendInputs stop audio no more
packages/native/CMakeLists.txt
    retroplug-sdl links retroplug-midi, loses MidiIo.cpp and rtmidi (:336, :344);
    retroplug-cli and retroplug-host: PROVIDERS n8 launchpad midi (:153, :268)
packages/native/cmake/deps.cmake
    the RETROPLUG_N8_BRIDGE block (:19-27) deleted (report item 7)
packages/native/cli/main.cpp
    the #ifdef block (:29-31, :241-247) deleted
root package.json
    retroplug.products.desktop gains "midi" before "launchpad" (the audio packages land in S2)
packages/retroplug/scripts/run-plugin-tests.mjs
    the retroplug-midi-test entry follows the target (its name is kept)
```

The manifest's provider entry follows `launchpad`'s (`packages/launchpad/package.json`, the `native` array): a desktop
entry, `device: false`, a `provider` C function and an `rpc` header and register function, and **no `entry`** (the
reader refuses both: `cmake/packages.cmake:259-263`). `rp_midi_provider` is an `extern "C" const rp_provider*`: `create`
builds the service (a `MidiStream` over `systemMidi()`, opening nothing until `midiOpen`), `restore` is a no-op,
`rpc_service` returns the `MidiRpcService`, `destroy` deletes it, `wake` is null.

### 4.6 `FakeMidiBackend`

It carries what `FakePorts.hpp`'s `Rig` carries today (`FakePorts.hpp:25-58`), because the Launchpad's tests and the
headless host's scripted Pro MK3 are built on it, not only a recorder: the listed ports (the machine's list, independent
of what opens: "opening a port no list names still works, so a claim of a vanished port is the lister's refusal"), a
`refuse` set, an event log (`open in:X`, `close out:Y`), what every output received (kept outside the port so it
survives the port's destruction), the receiver of every open input, `deliver(name, bytes)`, a send hook through which a
scripted device answers an inquiry (the `answering` and `answersOn` maps), a per-send delay (`sendDelayMs`, with the
`sendsStarted` / `sendsFinished` counters the writer-thread case waits on), and the delivery mode: inline on the
caller's thread, or on a thread of its own, which is what the MidiStream's cases and the TSan leg need. It is
header-only test support under `packages/midi/native/test-support/`, an include root the INTERFACE target
`retroplug-midi-test-support` carries to the binaries that script a MIDI system (the package's test, the Launchpad's
test, the headless host) and to no product (the shape `retroplug-launchpad-harness` has: "a header a host, and only a
host, compiles"; the first prototype pass left it on the library's public include root, where `retroplug-sdl` and the
shipped CLI could include it). It also models an `exclusivePorts` set (a second open of an open name refuses, as WinMM
does), which is what lets the reopen case fail, and its `withClientName` returns a copy listing the same scripted
machine, dropping `onSend` and `answering`, so a renamed facet session cannot talk to the scripted Pro MK3. `deliver()`
runs an inline receiver under the fake's non-recursive mutex, so a receiver that sends synchronously would
self-deadlock; threaded delivery is the mode for that. `FakePorts.hpp` shrinks to the device model over it.

### 4.7 Proof

**RED first.** (1) `MidiStream`'s cases are written against the interface and the fake before the stream exists (a
compile failure is the first red); the output-swap case then runs against a stream whose `setOutputs` frees the old port
at once, and must fail under TSan and by a use-after-free counter on the fake, then pass with the epoch handshake. (2)
The ring's producers case moves with its test and runs against the old code to prove the harness bites (the branch's own
RED: abort or segfault without the mutex). (3) The Launchpad's 53 cases are re-pointed at the fake before the pack's
sources change, so the first edit that breaks one is visible; the twin-table cases (`MidiPortSelect.test.cpp:237-290`)
are deleted with the copy they guarded, and a `rg` for `launchpad/desktop/MidiPortFilter` over the tree must find
nothing.

**New cases, in `retroplug-midi-test`** (`packages/midi/native/test`): lists and "all" (an empty list opens nothing,
`all` opens every hardware port and no virtual one twice, a name that is not present opens nothing and logs);
reservation (a reserved input is skipped under `all` and under a list naming it, a reserved output logs "held by the
control surface"); suspension (a suspended stream opens no input and resumes to the list); the ring (the branch's case,
moved, plus a drop count case); an output swap while a fake "audio thread" sends in a loop (every send lands on exactly
one live port, no send reaches a freed one, run in `build-tsan/`); `supportsVirtual()` false (no virtual ports, no
warning line); a refused open is one `std::runtime_error` and leaves the rest of the list opened; the allow list hides a
port from the list and from the open. **`retroplug-launchpad-test`** over the fake: all 53 cases, unchanged in intent;
the `[provider]` and facet cases pass the fake through `createProvider`. **`test:native`**: the E11 case (the user's
item 6: the facet's `""` is every input, RED against today's None; a named input opens one; run under the port filter so
no real device opens), `launchpad-facet` and `launchpad-devices` over the headless host's scripted device (rebuilt over
the fake); `n8-bridge` sessions through the CLI's facet. **Held**: `sdl:smoke`, `sdl:controller` after its harness edits
(section 9), the parity, kit and render baselines, the registration golden (`midi` has no node type), `check:boundaries`
and the native boundary check with the new package, and the device images' hashes (nothing portable moves).

**As built.** The review's additions are cases of their own: the log lines asserted byte for byte through the stream's
sink (three case names had claimed them), a threaded-delivery case that swaps the INPUT SELECTION under a delivery
thread (one port kept, one let go) and holds that the port the destructor joined owes nothing (every queued message
drained, and nothing reaching a closed port), the exclusive-port reopen case (RED: the port lost, `opened in=1 out=0`;
green after Diff/Reopen), the back-to-back-sends bound (one in-flight send waited out, never the run of them), a re-pick
that reopens, and a case that sends three messages from ONE thread on the path the single-caller assert guards (an
assertion that fires cannot itself be a case: what holds two senders apart is the Debug assert plus `AudioHost`
guaranteeing one renderer). `retroplug-midi-test` ends at 48 cases (24 at the base; 47 at the prototype's tip, plus the
follow-up's renamed-session case), and a standing `tsanmidi` kit leg runs it
under ThreadSanitizer (`tools/run-sanitizer.sh`'s midi leg beside the Launchpad's). The midi targets joined
`_rp_werror_targets` (they had moved out of targets Windows judged at `/W4 /WX`) with `_CRT_SECURE_NO_WARNINGS`, and
`check:boundaries` learned a dependency's own library targets (a textual `retroplug_package_library` read, with `-`, `.`
and `+` as name characters so `retroplug-audio` is not "named" by `retroplug-audio-sdl`), which kept the standing
warnings at 0. `n8-bridge` sessions through the CLI's real RtMidi facet are not held by a test (only the host's
fake-backed facet is): owed by hand on a real N8.

## 5. Stage 2: the `audio` packages, `AudioHost` and `AUDIO_BACKENDS`

### 5.1 The packages and the interface

`packages/audio` (`@retroplug/audio`; dependencies `core` and `ui`. This design listed `node-sdk` too (report item 20),
but `AudioHost` never names the Engine, so no header of the native half includes the SDK and `check:boundaries` flagged
the declaration as unused; a package with no native contract declares none): native target `retroplug-audio` (the
interface, the `null` backend, `AudioHost`), unlisted in the manifest as `retroplug-midi` is, plus, in S3, the TS
section and rows. `packages/audio-portaudio` and `packages/audio-sdl` (each depends on
`audio`): one native target each (`retroplug-audio-portaudio` links `portaudio` and, on Linux, ALSA;
`retroplug-audio-sdl` links `PkgConfig::SDL2`), no TS. Interface, namespace `rp::audio`, as approved:

```cpp
struct Driver     { std::string id, label; };                  // "pipewire" / "PipeWire"
struct Devices    { std::vector<std::string> names; std::string defaultName; };
struct Request    { std::string driver, device; int sampleRate, blockFrames, channels; };
                       // "" = auto / default; blockFrames 0 = the backend's quantum
struct StreamInfo { std::string driver, device; int sampleRate, blockFrames, channels; };
using Pull = void (*)(void* ctx, float* interleaved, int frames, bool underflow);

class AudioStream {                  // one open device
public:
    virtual ~AudioStream();
    virtual bool start(std::string& error) = 0;   // false with the backend's own words: an open that cannot start is
                                                  // an open failure to the host, not a live-but-silent device
    virtual void stop() = 0;         // returns once no callback can run
    virtual StreamInfo info() const = 0;
    virtual bool lost() const = 0;   // the device went away: PortAudio's finished stream, SDL's removed device
};

class AudioBackend {
public:
    virtual ~AudioBackend();
    virtual std::string id() const = 0;      // "portaudio", "sdl", "null"
    virtual std::string label() const = 0;   // "PortAudio", "SDL", "Null"
    virtual std::vector<Driver> drivers() = 0;
    virtual std::string autoDriver() = 0;    // the driver "" resolves to, or "" when none can open
    virtual Devices devices(const std::string& driver) = 0;
    virtual std::unique_ptr<AudioStream> open(const Request&, Pull, void* ctx, std::string& error) = 0;  // NOT started
    virtual std::string driverLabel(const std::string& driver);   // "PipeWire" for "pipewire" (the log line, the row)
};
bool devicesListableFor(const std::string& active, const std::string& requested);   // SDL's one-driver rule, pure
```

Backend ids `portaudio`, `sdl`, `null`. A saved choice is `(backend, driver, device)` by name, never a library's numeric
id (the old `audio.json` stored a `PaHostApiTypeId`, `AudioCfgJson::hostApi`, `:1192`; `sdl:pipewire` still writes
`"hostApi":18`). **Auto** is the first backend in the product's `AUDIO_BACKENDS` order whose `autoDriver()` opens;
PortAudio's `autoDriver()` keeps today's rule, PipeWire when its host API has an output device, else the host of the
platform default output (`autoOutputHostApiIndex`, `main.cpp:523-531`). That leaves Windows on MME (the first host in
`deps/portaudio/src/os/win/pa_win_hostapis.c`, the release-readiness item), so on Windows the PortAudio backend's
`autoDriver()` prefers WASAPI (report item 10, the user's; an edge, E14, verified by the Windows agent), then falls back
to the platform default's host. SDL2 runs one audio driver at a time (`SDL_AudioInit` / `SDL_AudioQuit`), so the `sdl`
backend's `devices(driver)` lists real devices only for the driver currently initialised and returns an empty list,
which the UI shows as "Default", for the others; opening a request that names a different driver switches the driver,
after `AudioHost` has stopped and closed the old stream. The sentence the approved section gives is "stated rather than
flipping drivers under a running stream", and this is that. The first prototype pass did not keep it: its `devices(driver)`
switched the active driver, `describe()` called it for every driver, and `SDL_AudioQuit` closes every device the process
has open, so opening Settings > Audio killed a running SDL stream (the S2 review's C2). As built, `devices(d)` answers the
real list only when `d` is the driver in force (`devicesListableFor`, pure and in the contract header so a unit case
holds it without SDL), only `open()` may quit and re-initialise a driver, and `ensureDriver` refuses a switch while a
device of ours is open. The header states three rules a third-party backend keeps, on the class: the factory runs at
startup for every backend a product names, so a constructor must not touch its library (`drivers`, `devices` and
`autoDriver` are the first use); `drivers` and `devices` never disturb a running stream; `open` returns a stream that is
not started.

### 5.2 `AudioHost`

```cpp
struct AudioClient {                 // the product's side; the engine is behind it and AudioHost never names it
    virtual ~AudioClient() = default;
    virtual void prepare(double sampleRate, int maxBlockFrames, int channels) = 0;  // re-rate, shape, size (quiescent)
    virtual void render(float* interleaved, int frames) = 0;                         // today's renderAudioBlock
    virtual void release() = 0;                                                      // take the engine back
};
struct AudioStatus { std::string backend, driver, device; int sampleRate = 0, blockFrames = 0, channels = 0;
                     std::string error; };                    // error empty when open; "Muted: <reason>" otherwise
class AudioHost {
public:
    AudioHost(BackendList preference, AudioClient& client);   // `null` appended last
    bool apply(const AudioConfig& cfg);     // the whole sequence, once (below); true when a real stream is running
    void stop();                            // the teardown path
    bool poll();                            // the UI frame: a lost or stalled stream falls back; true if the status moved
    AudioStatus status() const;
    bool live() const;                      // a real device is open and started
    AudioDescription describe() const;      // the backend IN FORCE with its drivers and devices, the status, the overrides
    std::vector<AudioDriverDescription> backendDrivers(const std::string& backendId) const;   // a sibling, on request
    void setLogSink(LogSink);               // the bytes the harnesses grep, held by a case
};
AudioConfig applyRunOverrides(AudioConfig, const std::string& backendOverride, int blockSizeOverride);  // 6.2, E12
```

`AudioConfig` is the `audio` section's fields (`backend`, `driver`, `device`, `sampleRate`, `blockSize`, `outChannels`)
plus `renderAheadFrames`, which the flag sets and the section does not carry. `describe()` enumerates only the backend IN
FORCE: enumerating a backend IS the first use of its library (`Pa_Initialize` probes every ALSA card; `SDL_InitSubSystem`
brings a driver up), so a null-pinned run, and the start menu before any apply, touch no other library (the Zoom L6 rule
the user made strict, section 13). A sibling is reached only through `backendDrivers(id)`, on an explicit user step
(6.5). `applyRunOverrides` is where `--audio-backend` and `--block-size` meet the saved choice (E12): the flag replaces
only the BACKEND, and the saved driver and device survive when the saved section names the same backend (they are that
backend's own vocabulary), and are cleared only when it names another. `AudioHost` owns, in the order of what it
replaces:

- **open / start / stop / reconfigure.** The sequence is stop, release, open, `prepare(info())`, start (report item 2).
  It is today's `reconfigureAudio` (`main.cpp:1254-1281`) with the open moved before the re-rate: stop the stream;
  `client.release()` (`setAudioThreadOwns(false)` and the invoker drain); open (not started); `client.prepare(...)` with
  the OBTAINED rate, channels and block (`engine.setSampleRate`, `setHostOutputs`, `setMaxBlockFrames`, which today
  straddle the open); start. `prepare` hands the engine back to the audio thread at its end (the interface has three
  methods and the sequence's "hand the engine back" sits between `prepare` and `start`; without it the engine stayed
  ownerless and the idle-reclaim probe failed). With render-ahead the ring, its four counters and the renderer are set up
  BEFORE `start`, because a real device's callback can run the moment `start` returns (the S2 review's C3: the first
  prototype ordering raced a pull against the ring's reallocation, five TSan reports). Written once here and not in each
  backend. The order differs from the
  approved sketch (prepare before open) on purpose: today's code re-rates the engine with the REQUESTED rate before
  opening and records the obtained one only in `AppState::sampleRate` (`:1268`, `:1093`, `:1148`), so a device that
  adopts another rate plays an engine rendering at the first; the interface allows the new order because `open` does not
  start.
- **fallback.** A failed `open` logs the backend's error and falls to the `null` backend (the sleep-paced pump of
  `audioPumpLoop`, `:927-936`, which stays what `sdl:smoke` runs on), recording a **status**
  (`{ backend, driver, device, rate, block, channels, error }`, with the bare reason in `error` and "Muted: " added by TS)
  that TS shows, so Settings says "Muted: <reason>" instead of silence. A stream that opens and will not start is an open
  failure (`AudioStream::start(error)`: it is closed, the null device opens with the reason, and the engine is handed
  back). A backend switch with the same driver, device and rate is a real change (`sameChoice` compares the backend,
  with "" read as "auto"). Whatever stream runs is what `prepare` shapes the engine for: render-ahead is decided before
  the request (null never renders ahead) and the fallback asks the null device for the section's block, so a run that
  falls to null cannot pull blocks over the engine's maximum.
  Today a failure prints once ("`Pa_OpenStream(...) failed: ... (muted)`", `:1143-1146`) and a persisted request that
  the device rejects mutes on every launch with no way to see why (release-readiness section 1).
- **a lost device.** Today's freeze (the emulator advances only inside the callback) becomes the same fallback: the UI
  frame calls `poll()`, which checks `stream.lost()` and reopens on `null`, with the status naming the reason. The
  detection is each backend's (PortAudio's finished stream, SDL's device status; neither was verified against a really
  removed device, R10), and `AudioHost` adds a backend-neutral watchdog (report item 9): a started stream that has not
  pulled for a bound counts as lost, so a backend that never reports still falls back. The clock starts at `start()`
  (a stream that never pulls at all is caught) and is stamped at pull EXIT (a long render is one block's work, not a
  stall); the bound is max(250 ms, 4 x the duration of the longer of the render block and the device's own quantum),
  because a fixed bound tripped on a legitimate 8192-frame quantum at 44.1 kHz. The watchdog reads one counter the pull
  stamps (a relaxed atomic), costs the audio path nothing else, and is off for `null` and while stopped. A trip falls to
  `null` with a status and STAYS muted until the next apply (E6): there is no backoff retry, and Settings > Audio keeps
  Apply live while a status error is set, which is the retry.
- **render-ahead** (decision 13), ported from `4b9e39ea8`: `renderAheadFrames > 0` runs a render thread filling an
  interleaved ring in `blockFrames` blocks; the stream opens with `blockFrames 0` and the pull only copies, padding a
  short ring with silence and counting an underrun once the ring has primed. The ring holds at least two blocks
  (`max(renderAhead, 2 * block)`, `4b9e39ea8`'s `ringFrames`). The Linux real-time treatment moves here: the callback
  thread at `SCHED_FIFO` 85 above the renderer's 80 and both on the audio core (`tuneAudioThread(priority)`), applied
  per thread rather than once per process (a `thread_local` latch): the `static std::atomic<bool> tuned` latch (`:916`)
  means a re-opened stream's new thread is never tuned (release-readiness section 1). The first prototype pass dropped the
  tuning altogether and left the UI pinned off a core nothing used (the S2 review's I1); `tuneAudioThread` runs on a real
  device's thread only, not on the null pump or on a test thread that pulls by hand. Set by `--render-ahead <frames>` /
  `RETROPLUG_SDL_RENDER_AHEAD`; when `feature/handheld-perf` rebases its `main.cpp` hunk reduces to the muOS launcher
  flags.
- **stats.** `RETROPLUG_SDL_AUDIO_STATS` and the five-second load line of `4b9e39ea8` (`AudioStats`) move here, backend
  neutral; `underflow` is the pull's flag, which SDL never sets (it has none). Under render-ahead the RENDERER alone
  reports and the pull adds its underruns to an atomic the renderer drains (two threads sharing the accumulator raced and
  mixed two streams in one line, which was the figure that diagnosed the muOS bug).
- **the log line** keeps today's form,
  `[retroplug-sdl] audio: <Backend> [<driver>] '<device>' <rate> Hz, <frames> frames, <ch> ch`, with the backend's
  `label()` and the driver's LABEL (`PipeWire`, not `pipewire`): PortAudio's line is then byte-identical to
  `openAudio`'s (`:1150-1152`), so `sdl:pipewire`'s `sed` over `audio: PortAudio \[\(.*\)\] '\(.*\)' [0-9]* Hz` and its
  `grep "audio: PortAudio .*512 frames, 8 ch"` hold. The warnings `openAudio` prints that those scripts and users read
  stay too, from the PortAudio backend: "output device '<d>' not found in the chosen driver, using its default"
  (`:1121-1123`) and the wide-output line "'<d>' takes N ch - pairs beyond the first M channel(s) are unconnected until
  you patch them" (`:1156-1163`).

`AudioHost` is real-time code only in `pull`: it takes no lock, allocates nothing and calls `client.render`; every other
call is [control]. `renderAudioBlock` (`:837-910`) becomes `AudioClient::render`'s body in `sdl/audioLoop.cpp`,
unchanged except that it no longer interleaves into a callback's buffer it did not size: `AudioHost` hands it the pull's
buffer.

### 5.3 The backends

- **`audio-portaudio`**: today's code, moved: the host-API and device lookup (`availableHostApis`,
  `outputDevicesForHostApi`, `defaultOutputName`, `findOutputDeviceByName`, `autoOutputHostApiIndex`, `:476-545`), the
  low-latency suggested latency (`Pa_GetDeviceInfo(dev)->defaultLowOutputLatency`, `:1138`), adopting the obtained rate
  (`Pa_GetStreamInfo`, `:1148`), the wide-output warning, and the log quieting (`alsaLogHandler`, `jackLogHandler`,
  `paLogHandler`, the `dlopen` of `libjack.so.0` and the private `PaUtil_SetDebugPrintFunction`, `:1024-1089`) run
  before `Pa_Initialize` at first use of the backend. A driver's `id` comes from a fixed table keyed by
  `PaHostApiTypeId` (`alsa`, `pipewire`, `jack`, `wasapi`, `mme`, `directsound`, `coreaudio`, ...), falling back to the
  lower-cased display name for a fork's own host API, and its label is PortAudio's: the first pass derived ids from
  display names ("windows-wasapi", "jack-audio-connection-kit"), and a saved `config.json` choice must not move when a
  fork renames a host API. The backend terminates what it initialised (`Pa_Terminate` in the destructor, behind a
  file-scope state) and `lost()` counts a negative `Pa_IsStreamActive` too. The first-prototype lines that changed and
  are reported, not restored: an open failure prints `audio: <the backend's error>` (it was `PortAudio:
  Pa_OpenStream(...)`) and `Pa_Initialize failed` names the backend; no harness greps either, and the
  `RETROPLUG_DEBUG_AUDIO` hint is kept. The backend calls `snd_lib_error_set_handler` directly (`:1072`), which today
  links only because `rtmidi` brings `libasound` along; the target therefore names ALSA itself: `find_package(ALSA)` and
  a link to `ALSA::ALSA` on Linux (report item 22). `deps/portaudio`'s options and `add_subdirectory` move to
  `cmake/portaudio.cmake` at root scope (5.4).
- **`audio-sdl`**: `SDL_InitSubSystem(SDL_INIT_AUDIO)` at first use (`setupSdl` initialises video and the game
  controller only, `:1390`), `SDL_OpenAudioDevice` with a float32 pull callback, 2, 4, 6 or 8 channels, the frequency
  allowed to change and adopted (as PortAudio's is) and the channel count exact, `SDL_GetNumAudioDrivers` /
  `SDL_GetAudioDriver` for drivers, `SDL_GetNumAudioDevices` / `SDL_GetAudioDeviceName` for devices, and
  `SDL_GetDefaultAudioInfo` (SDL 2.24 and later; the tree has 2.30.0, the macOS CI framework 2.32.10) for the default's
  name. SDL has no underflow flag and no "unspecified" block size; both are prototype questions (report item 18),
  answered: `drivers()` hides the `dummy` and `disk` test drivers, `blockFrames 0` asks for 512, and the obtained size
  is reported in `StreamInfo`. A default request reports SDL's default device NAME (or ""), not "Default", which `open`
  could not reopen; a stale device name warns and falls back to the default exactly as PortAudio's does (not a mute),
  so the SDL leg of `sdl:pipewire` can hold the same selection rules; a driver switch is refused while a device of ours
  is open; and a wide request that lands on its PipeWire driver warns at open that the graph node shows 2 output ports
  (R11), which PortAudio's declared-layout stream does not. A failed subsystem init is logged once, not on every call.

### 5.4 The build: `AUDIO_BACKENDS`

`AUDIO_BACKENDS` joins `retroplug_product` beside `PROVIDERS` (`cmake/packages.cmake:1540-1579`, the signature and
`cmake_parse_arguments` at `:1579`). A new manifest item
`{ "target": "retroplug-audio-portaudio", "audioBackend": "rp_portaudio_backend", "device": false }` is read beside the
provider branch of `_retroplug_read_manifest` (`:230-263`), into `RP_PACKAGE_<name>_AUDIO_BACKENDS` and
`_AUDIO_BACKEND_TARGETS`. What the product does, following the provider rule (`:1666-1677`):

- **A product that lists the package without naming it drops the target** (the provider rule's loop over
  `_providerTargets`), so the CLI, the headless host, `retroplug-render-host-test`, `retroplug-ui-test` and the Node
  addon, which list the desktop packages and name no backend, link neither PortAudio nor SDL audio.
- **Configure refusals**, each a `message(FATAL_ERROR ...)` with its own words and its own fixture: `AUDIO_BACKENDS`
  names a backend no manifest declares; names one twice; names one whose package the product does not list; and a
  manifest item that is an `audioBackend` and an `entry` or a `provider` at once (the reader's refusal, in the family of
  `EntryWithProvider` and `ProviderTargetWithEntry`, `packages/native/cmake/tests.cmake:188, 200`). As built the list is
  longer than the design's four plus one: four product-level cases in `test/packages/product-lists` (unknown id,
  duplicate id, package not listed, and a `DEVICE` product naming a backend, which cannot be isolated from the later
  "a device product cannot carry a desktop package" guard, so its fixture's reason check is what bites) and six
  manifest-level fixtures in `test/packages/refused` (`AudioBackendWithEntry`, `-WithProvider`, `-OnDevice`,
  `-WithoutDevice`, `-WithoutId`, `-DuplicateId`), with two positive controls (`audio-header-named` and
  `audio-header-none`, whose generated headers the configure reads back).
- **A generated `rp_product_audio_backends.hpp`** in the product's own include directory beside
  `rp_product_providers.hpp` (`retroplug_providers_header`, `:2039-2120`): it declares each named backend's factory (a
  C++ function, `std::unique_ptr<rp::audio::AudioBackend> rp_portaudio_backend();`, not `extern "C"`: the interface
  holds strings and a `unique_ptr`) and defines `rp::audio::BackendList rp_product_audio_backends()` returning them in
  the order the product names them. A product that names none gets an empty list and `AudioHost` has only `null`.

What `AUDIO_BACKENDS` is given (the ids `portaudio sdl`) and what the manifest key holds is settled (report item 3): the
product names ids, and the manifest item carries an `"id"` member beside `target` and `audioBackend`, which is what the
refusals and `--audio-backend` map through.

`retroplug-sdl` names `AUDIO_BACKENDS portaudio sdl` (`packages/native/CMakeLists.txt:347`), so CI's `all` compiles both
on all four platforms; `portaudio` and SDL audio are linked by no other product. **At root scope**: PortAudio's cache
options (`PA_USE_SNDIO`, `PA_USE_PULSEAUDIO`, `PA_USE_OSS` off, `PA_ALSA_DYNAMIC` / `PA_PIPEWIRE_DYNAMIC` /
`PA_JACK_DYNAMIC` on, `unset(PA_USE_JACK CACHE)`) and `add_subdirectory(deps/portaudio ... EXCLUDE_FROM_ALL)` with
`retroplug_silence_dep_warnings(portaudio)` move from `deps.cmake:29-61` into a new `cmake/portaudio.cmake` included
beside `cmake/rtmidi.cmake` (root `CMakeLists.txt:226-227`), because a package's `native/CMakeLists.txt` is added when
`cmake/packages.cmake` is included (`:303`) and `retroplug-audio-portaudio` must find the target then. The include
directory `deps/portaudio/include` goes with it (`packages/native/CMakeLists.txt:359-360` names it for `portaudio.h`,
which the target does not export for a subdirectory build). `retroplug-audio-sdl` links the `PkgConfig::SDL2` the root
already defines on every platform (`CMakeLists.txt:155-192`).

### 5.5 Splitting `main.cpp`

`main.cpp` (1945 lines) becomes five translation units and a header, in `packages/native/sdl/` (the product's own
sources, no package; the file list is `retroplug-sdl`'s in `packages/native/CMakeLists.txt:333-337`). This was stage S2b:
the first S2 pass did not do it (a botched six-way split would have cost the stage's verified state), so it became its
own commit after the S2 review, so that the move reviews as one diff:

| Unit | Takes | From |
| --- | --- | --- |
| `app.hpp` | `AppState` (without the audio members that move to `AudioHost`), `g_app`, the shared declarations | `:187-338` |
| `window.cpp` | LVGL glue, `setupSdl` (video and game controller only), `setupUi`, `present`, the size latches, the screenshot | `:340-362, :1294-1310, :1343-1536, :1928-1945` |
| `input.cpp` | key translation, `feedKey`, `mapSdlKey`, `pumpGamepad`, `handleEvents` | `:735-806, :1539-1625` |
| `hooks.cpp` | `callGlobal`, the window hooks, file dialog glue, transport and clock hooks, the new audio and MIDI hooks (6.4), `installWindowHooks` | `:366-470, :634-733` |
| `audioLoop.cpp` | `MidiClockSync` (kept), `renderAudioBlock` as the `AudioClient`, `setHostOutputs`, the control-surface calls, `restoreControlSurface` | `:121-185, :388-392, :837-910, :949-1011` |
| `main.cpp` | `main`, the arguments, the 25 `RETROPLUG_` test hooks, the frame loop | `:1626-1927` |

`MidiClockSync` stays in the product, as approved ("keeps ... the clock-follow"): `docs/sdl-release-readiness.md`
section 5 also asks for it in `src/host` with a Catch2 test, which this phase does not do and section 12 leaves. The
test scaffolding in `main()` stays (the same section's other item).

**As built** (`1932627c6`; `ede472ed7` on `feature/modularize`). The move is verbatim: of the old `main.cpp`'s 1683
non-blank lines, 1676 appear unchanged in exactly one of the six files, and the other seven are reshaped deliberately -
the two generated product includes, the out-of-line destructor and its two body lines, and the namespace braces. What was
added is only what
the split needs: `app.hpp`'s `#pragma once` and include block, one `#include "app.hpp"` per unit, banners, the
cross-unit declarations and `extern AppState* g_app;`. Five facts differ from the table. (1) `main.cpp` keeps the
largest piece (the man page, the boot, `main()` and the frame loop), so its history stays with it; the other five are
new files. (2) `bootControlPlane` (the table left `:1311-1342` unassigned) and `readReady` went to `main.cpp`. (3)
`MidiClockSync` is in `app.hpp`, not `audioLoop.cpp`, because `AppState` holds one. (4) `restoreControlSurface` is
`waitForUiStartup`, since it now waits on the audio apply too. (5) Every cross-unit function has external linkage and a
declaration in `app.hpp`, inside the namespace `retroplug::sdl` (an anonymous namespace per unit holds the private
helpers; `app.hpp` includes neither generated product header, which `main.cpp` alone does; `~AppState` is out of line).
External linkage is the answer to MSVC's C4505 (an unused static under `/W4 /WX`), and the namespace is what keeps the
generic names (`present`, `setupUi`, `handleEvents`, `callGlobal`) off the global scope: it is in S2b, though the previous
history carried it in S3, because a fixup against the final tree would have been an empty patch and the move was done at
a stop of its own. That the five units compile is held by a real build, not by `-fsyntax-only`: the stage's tree was
configured and `all` built warning-free at `ede472ed7` (report item 5's finding that a syntax-only pass over translated
units proves nothing), and `sdl:controller` then ran the linked `retroplug-sdl` it produced.

### 5.6 What changes, by file

```
packages/audio/package.json
    new: @retroplug/audio; retroplug.native = { "target": "retroplug-audio" } (a library: see 4.1 and
    report item 4); app and ui arrive in S3
packages/audio/native/
    new: audio/AudioBackend.hpp, AudioHost.{hpp,cpp}, NullBackend.cpp, test-support/audio/FakeAudioBackend.hpp
    (the INTERFACE target retroplug-audio-test-support), test/AudioHost.test.cpp (retroplug-audio-host-test)
packages/audio-portaudio/
    new: package.json (audioBackend item), native/ (the PortAudio side of main.cpp :476-545, :1024-1166)
packages/audio-sdl/
    new: package.json (audioBackend item), native/ (the SDL backend)
cmake/portaudio.cmake
    new: PortAudio's options and add_subdirectory, from deps.cmake:29-61
cmake/packages.cmake
    the audioBackend item, AUDIO_BACKENDS, the generated header, the refusals
packages/native/cmake/deps.cmake
    loses the PortAudio block (and, with S1, the N8_BRIDGE block)
packages/native/CMakeLists.txt
    retroplug-sdl: sources become the five units (app.hpp is a header); PROVIDERS stays; adds AUDIO_BACKENDS portaudio sdl;
    links retroplug-audio and retroplug-midi; loses the direct portaudio link (:345) and its include (:359-360)
packages/native/sdl/*.cpp, app.hpp
    the split above; reconfigureAudio, openAudio, startAudio, stopAudio, audioPumpLoop, paCallback,
    quietenAudioLogs, AudioCfgJson and the PortAudio enumeration are gone from the product
root package.json
    retroplug.products.desktop: core, audio, audio-portaudio, audio-sdl, midi, launchpad, system, ...
    (the plugin list is untouched)
packages/native/test/packages/refused/
    AudioBackendWithEntry, -WithProvider, -OnDevice, -WithoutDevice, -WithoutId, -DuplicateId (.json)
    (+ their lines in tests.cmake:183-211)
packages/native/test/packages/product-lists/
    the product-level cases: unknown, duplicate, not listed, on DEVICE
tools/run-sdl-smoke.sh
    pins RETROPLUG_SDL_AUDIO_BACKEND=null (section 9)
packages/native/test/engine-graph/EngineGraph.test.cpp
    the re-rate equality case (`[engine-graph][c4]`), 338 cases in the binary (337 at the base)
```

### 5.7 Proof

**RED first.** (1) The four `AUDIO_BACKENDS` refusals and the manifest-level ones, each as a fixture that must be
refused with its own words before the mechanism exists (the configure runs them, `tests.cmake:213-225`, and fails on a
fixture that is read); a positive control: a product naming `portaudio sdl` generates a header declaring both factories
in that order, and one naming none generates an empty list. (2) `AudioHost` over `FakeAudioBackend`, written before the
host exists, and an **engine-graph case** in `retroplug-engine-graph-test` (the user's item 1, written before the
startup moves): a GB core and a NES core built at the default rate and re-rated to 44.1 kHz render bit for bit what a
core built at 44.1 kHz renders over the same blocks (if it fails, the rendezvous moves before the autoload instead):
**reconfigure's call order** (the fake records `stop`, `release`, `open`, `prepare` with the OBTAINED rate, `start`, and
the test holds the sequence, including a fake that adopts a different rate than requested, which `prepare` must see,
including that a no-change `apply` makes none of them and that a client never sees `render` between `release` and
`start`); **the watchdog** (a fake stream that starts and never pulls falls to `null` with a status naming the stall;
one that pulls steadily never does; neither fires while stopped; the bound scales with an 8192-frame quantum); **open
failure -> null with a status** (the fake's `open` fails with an error string; the host falls to `null`,
`status().error` carries it, a later `apply` with a working fake recovers); **lost device -> null** (the fake's `lost()`
flips; `poll()` moves to `null` and reports a status change); **render-ahead output bit-identical to in-callback across
ragged pull sizes** (a deterministic client whose `render` writes a counter ramp and a state-dependent tail: pulls of 1,
7, 64, 255, 512 and 1000 frames from a render-ahead host and from a plain one must produce the same sample stream, and
the underrun counter stays zero once primed). The ring case also runs in `build-tsan/` (a standing `tsanaudio` kit leg).
(3) The PortAudio and SDL backends compile in CI's `all` on all four platforms; no unit test opens a real device. (4)
The product tree: `retroplug-sdl`'s `nm -D` (or the platform's equivalent) must carry no `Pa_` symbol when it names only
`sdl`, and the CLI must carry none either way (`readelf -d` shows no `libportaudio` / `libSDL2` audio dependency beyond
what it already has).

**As built.** (1) The re-rate equality case PASSES on the real engine: a GB core and a NES core built at 48 kHz and
re-rated to 44.1 kHz before their first block render sample for sample what the same document built at 44.1 kHz renders
over 40 blocks of 1024 frames, each lane non-silent (the first version checked the mix, so a silent NES would have
passed), so the rendezvous stays where the user's item 1 put it. (2) `retroplug-audio-host-test` ends at 20 cases: the
reconfigure order with the obtained rate, the no-change apply, render only between start and stop, a failed open and a
later recovery, a lost device, the watchdog (stalled, steady, stopped, never-pulling, scaled), a stream that opens and will
not start, a backend switch with the same fields, render-ahead bit-identical across ragged pulls and wrapping its ring
nine times with `underruns() == 0` (the first version's pulls never wrapped the ring), a re-apply under a device whose
`start()` pulls on its own thread (RED under TSan: five reports, then clean for ten runs), a render-ahead run that falls
to null, the stats as one thread's accumulator, the log lines byte for byte through the sink, `describe()` enumerating
only the backend in force, and the run overrides. (3) The PortAudio and SDL backends are compiled, and no unit case
opens a real device. (4) is `tools/check-audio-linkage.sh` (section 7), a standing kit leg: the factory symbols
(`rp_portaudio_backend`, `rp_sdl_backend`) are what a product that names no backend must lack, and an empty symbol
table fails rather than passing vacuously; SDL2's presence proves nothing, since a product with no backend may still
need SDL2 for its window.

**Held:** `sdl:smoke` (on `null`, section 9), `sdl:pipewire` with its greps unchanged and its configuration written as a
`config.json` section (section 7), every baseline of section 9, the registration golden (no node type moved), the device
images, `reaper:params` (the plugin is untouched), and `check:boundaries` / the native boundary check judging the three
new audio packages (`audio` links no SDK and no third-party library; `audio-portaudio` and `audio-sdl` link `audio` and a
third-party library each). The audio targets joined `_rp_werror_targets` with the midi ones.

## 6. Stage 3: config sections, settings and startup (TypeScript)

### 6.1 Config sections

F5a gave a project a contribution shape (`ProjectSection`, `core/ts/definePackage.ts:41-67`: `key`, `schema`,
`defaults`, optional `version` and `migrations`, `merge`, refuse-newer per section, and the generator's duplicate and
shell-key refusals at `scripts/lib/packages.mjs:880-915`). F5b gives `config.json` the same:
`definePackage({ configSections })`, the same section type, the same checks, a second registry (`CONFIG_SECTIONS` in the
generated `packages.ts`, in product order). A config section is a non-core ROOT key of `config.json`; the core keys stay
the shell's and are the generator's refusal list (`schemaVersion`, `activeKeyboardBindings`, `activeGamepadBindings`,
`defaultZoom`, `sramAutoSave`, plus `render` and `useNativeFileDialogs`, which `UserConfig` also owns,
`userConfig.ts:85-92`).

What the shell's side does, and why the present code cannot just be reused:

- **A key no package contributes is kept verbatim.** `userConfigSchema` strips unknown root keys
  (`userConfig.ts:98-109`) and `serializeUserConfig` writes a fixed field list (`userConfigSerialization.ts:63-73`), so
  today the plugin (which lists none of the new packages) would erase the standalone's `audio` block, and a build
  without the Launchpad would erase `controlSurface`, on its next write of the shared file. The rule is F5a's E5 at root
  scope: `parseUserConfigResult` carries the raw non-core keys beside the typed value, `serializeUserConfig` writes them
  after the core fields (contributors' keys in product order, then unknown keys in the order read), and `commit()`'s
  equality test compares those bytes. As built the model is `ConfigModel { core, sections, raw, written }`, and the rule
  is wider than "no contributor": a contributed block this session did not set is also written back as the file carried
  it (`raw` beside `written`), because the file is shared by builds of different ages and an unstamped additive field a
  newer build wrote is exactly what an older build would silently drop (a `bufferCount` in `audio`, a routing field in a
  `midi` entry). A write of a SHELL field (`setDefaultZoom` and the like, through `update()`) carries the model through
  (`({ ...model, core: fn(model.core) })`); the first fix pass rebuilt the sections from their parsed values there, so
  the standalone, which writes `config.json` on every zoom, binding or file-dialog change, dropped the very fields the
  rule protects (the whole-prototype review's I1; the committed case is a store WITH an `audio` contributor and a
  shell-field write). **An absent key is not invented**: readers get the contributor's defaults and writers see them as
  the `merge` base, but only `setSection` (or a fresh first-run file) puts a block in the file, so a pre-S3 file
  round-trips byte-identical and the plugin's write adds no `audio` block. F5a's project sections fill an absent key
  with `defaults()`; a config section does not, and a pack author who copies the project section's behaviour must be
  told (spec/06's "Adding a package").
- **Writes keep today's re-read** (`UserConfigStore.update`, `userConfigStore.ts:211-216`): a section write is computed
  over the file as it is on disk, because two processes may share it (the plugin and a standalone). As built that
  includes the stamp check, the refusal warning and the `merge` itself, which run INSIDE the apply callback over the
  re-read block (`model.sections[key]`); the first pass ran them over the in-memory copy and then overlaid the result on
  the re-read model, which reverted another process's change to the same block and overwrote a block a NEWER build had
  stamped (the S3 review's C2; both RED cases are committed).
- **A section stamped newer than its contributor is refused** at section scope: readers get its defaults, `setSection`
  refuses the patch with one warning, and the raw block is written back byte-identical (F5a's rule,
  `projectConfig.ts: 179-199`). The file-level `schemaVersion` is unchanged (additive keys need no migration, so
  `USER_CONFIG_SCHEMA` stays 3).
- **A contributor that throws** is isolated with one warning per package and key, as F5a's sections are.

The generic `config` host group in `core/ts/host.ts` (replacing `ConfigHost.controlSurface` / `setControlSurface`,
`:237-238`) mirrors the `project` group:

```ts
export interface ConfigHost {
  /** The section as its contributor parses it (raw JSON for a root key no package contributes). */
  section<T = Record<string, unknown>>(key: string): T;
  /** Apply a patch through the contributor's `merge`, written through a re-read of the file. Throws when it
   *  refuses the patch; does nothing for a section stamped newer than its contributor. */
  setSection(key: string, patch: Readonly<Record<string, unknown>>): void;
}
```

`UserConfigStore.controlSurface()` / `setControlSurface()` (`:102-110`) become `section` / `setSection`;
`ControlSurfaceSettings` leaves `core/host.ts` (`:93-98`) for the Launchpad package, whose `launchpadDevices.ts` is its
only client (`:164, 191, 211, 222, 253, 271-282, 434`) and whose app module contributes the section under the same root
key `controlSurface` with the same four fields, so an existing `config.json`'s bytes do not move. The generator's
`SHELL` list for config keys is the one place that must name the core keys, as `SHELL_SECTION_KEYS` does for a project's
(`packages.mjs:900`). As built `CORE_CONFIG_KEYS` is derived from the schema's shape plus `schemaVersion` (the first pass
hand-kept three lists that nothing compared), and a test holds the generator's `SHELL_CONFIG_KEYS` against it.
`ConfigHost`'s doc comment states the re-read, the verbatim-keep and the absent-key rules, because it is the contract a
third-party section copies. `ProjectHost` gained `revision()`, the project-load signal the MIDI service re-resolves on
(a load that leaves the section equal still needs a re-resolve: another machine's ports).

### 6.2 The `audio` and `midi` sections

**`audio`** (config section, `audio` package):

```ts
{ backend: "auto" | "portaudio" | "sdl" | "null", driver: "", device: "", sampleRate: 48000, blockSize: 512,
  outChannels: 2 }
```

Today's defaults (`AppState`'s `reqSampleRate` / `reqBlockSize` / `reqOutChannels`, `:300-303`). `backend: "auto"` is
the first backend in the product's order that opens; `driver` and `device` are names in the backend's own vocabulary (""
= its default). The schema is clamped like `AudioCfgJson`'s reader (rate and block positive, channels 2, 4, 6 or 8).
Render-ahead is a flag, not a setting (decision 13). `--block-size` and `--render-ahead` override the saved choice for
that run (report item 8, the user's), so a launcher's 4096 wins over the section's 512 without being saved; today a
saved `audio.json` wins (E12). As built the section is additive-only (no stamp), the backend a closed set, and the
override is `AudioHost`'s `applyRunOverrides` (5.2): `--audio-backend` replaces the backend and keeps the saved driver
and device when the section names the same backend, which is what lets `sdl:pipewire` pin a backend and still express a
device pick by name (the first pass cleared them). The Settings rows say when a flag is in force: "This run: backend
portaudio, block 4096 (a flag overrides the saved choice)", since a muOS launcher's `--block-size 4096` now always wins
and a Settings block pick there never takes effect.

**`midi`** (project section, `midi` package):

```ts
{ inputs: [{ name: string }], allInputs: false, outputs: [{ name: string }], clockBpm: 120 }
```

Key `midi`, distinct from the shell's `midiRouting` (the graph's routing, unchanged; `CORE_SETTING_KEYS`,
`projectConfig.ts:33`). The entries are objects so per-device routing can add a field later without a version bump; the
section declares no `version` (additive only, so nothing is stamped), and its parse is hand-written and tolerant: an
entry keeps its unknown fields (the per-device routing room is real, not a promise: the first pass rebuilt every entry
as `{ name }` and the checklist's toggles did the same) and `merge` is shallow. A fresh project has no ports (the
virtual ones only), which is today's default (`MidiIo.hpp:196-205`). `clockBpm` is clamped to 20..999
(`MidiClockSync::kMinBpm` / `kMaxBpm`, `main.cpp:139-140`, the range the estimator accepts). The desktop's fresh project
gains a `midi` block; the plugin's does not (F5a's E8 at one more key), and a block the standalone wrote survives the
plugin verbatim (E9).

### 6.3 Name matching

`midi/ts/portMatch.ts`, pure and platform-free: `resolvePort(saved: string, present: string[]): string | null`. Exact
first. Then, as TIERS tried in order, each of which must find exactly ONE present port (two candidates stop there, since
a coarser rule would only find more; none moves to the next tier): ALSA's trailing `NN:NN` client:port numbers stripped
(`"LPProMK3 MIDI:1 24:0"` against `"... 28:0"`, which move as other clients come and go; the harness's own fakes carry
them, `tools/run-sdl-controller.sh:82-87`), keeping the rest of the name so a two-port interface's "MIDI 1" and "MIDI 2"
stay apart; then RtMidi's trailing WinMM index (`deps/rtmidi/RtMidi.cpp`'s `getPortName` appends `" <index>"` to EVERY
Windows name, so `"MIDISPORT 2x2 Port A 1"` renumbers to `"... 2"` when another device appears). Every tier folds runs
of spaces and CoreMIDI's repeated device name. One strip could not serve both rules, which is what two review rounds
found: the design's `N- ` prefix rule does not match what RtMidi gives (it was dropped for want of a real listing), and
a single trailing-index strip applied after the ALSA one folded a two-port ALSA interface onto one name, so a client
renumber resolved to nothing, on the platform the user runs. The Launchpad's `rematchPorts` shares `resolvePort` (the
repo had two matchers that disagreed). Two candidates, or none, resolves to nothing. An entry that resolves to nothing
stays in the section, shows "<name> (missing)" in the rows, and opens nothing. Tested against a table of real ALSA,
WinMM and CoreMIDI names: the table is data in `packages/midi/ts/test/` and is extended from a real machine of each kind
(the Windows and macOS rows are UNVERIFIED and the user's agents', owed: section 11). Matching lives in TS so the stored
name is the only thing native sees as a selection: `midiDevice.apply` is handed EXACT port names.

### 6.4 Reaching native

`core/host.ts` gains two contracts (the group names are the approved ones; the member shapes are the sketch's):

```ts
export interface AudioDevice {
  describe(): AudioDescription;     // the backend IN FORCE: drivers, devices, defaults, auto choice; the status; overrides
  backendDrivers(backend: string): AudioDriverDescription[];   // a sibling, on an explicit step: its library's first use
  apply(cfg: AudioConfig): AudioStatus;   // the whole reconfigure; returns what was adopted (or why it is muted)
  status(): AudioStatus;            // cheap read for the status row and the UI frame (includes `live`)
}
export interface MidiDevice {
  describe(): MidiDescription;      // { inputs, outputs, supportsVirtual, reserved: { input, output } }
  apply(sel: { inputs: string[]; allInputs: boolean; outputs: string[] }, mode?: "diff" | "reopen"): void;
  transport(): TransportState;  setTransport(playing: boolean): void;  setClockBpm(bpm: number): void;
}
```

As built the native side binds six globals replacing eight, with the names this draft chose except for one addition:
`__rp_audioDescribe` (its result carries `overrides`, what this run's flags pin), `__rp_audioBackendDrivers(id)` (a
sibling backend's drivers and devices, `[]` while a run pins a backend), `__rp_audioApply({ backend, driver, device,
sampleRate, blockSize, outChannels })`, `__rp_audioStatus` (with `live`), `__rp_midiDescribe` (with `openedInputs` /
`openedOutputs`) and `__rp_midiApply(sel, mode)`, `mode` being `Apply`'s "diff" (the default) or "reopen". They return
plain JS objects and take an object argument, so the TypeScript types them directly. `AudioDeviceHost` has
`backendDrivers(backend)` as a fourth member and `MidiDeviceHost.apply(sel, mode?)` a second argument; both are OPTIONAL
groups on `UiHost` (`audioDevice?`, `midiDevice?`), which says "only where the SDL hooks exist" without a member that
returns null. A hook call that omits a field reads it as "" (`readString` once turned a missing `driver` into the string
"undefined" and muted the run), and `apply` validates the channel count (2, 4, 6, 8) and rate and block as positive. The
C++ label-to-choice helper the first S2 pass kept (`choiceForDriverLabel`, with the interim hooks that kept TS's old
contract until S3) went with the hooks it served: the section carries ids, so nothing maps a label back. The
native side keeps the transport trio (`__rp_getTransport`,
`__rp_setTransport`, `__rp_setClockBpm`) that the MIDI Clock submenu and the smoke's `RETROPLUG_SDL_TEST_TRANSPORT` use
(`main.cpp:1723-1724`), minus `setClockBpm`'s `saveAudioConfig` (`:663`: the tempo is the project's now). The shell's UI
host (`ui/host/uiHost.ts:40-77`) builds `audioDevice` and `midiDevice` only where the globals exist, so the plugin and
the DPF standalone hide the rows as they do today (`isStandalone() && hasAudioConfig()`, `menuDefs.ts:1349-1350`); the
headless and test hosts fake the globals (`test/menu/recorded-cases.ts:584-587`, `test/menu/leaves.test.ts:1224-1225`
change with them).

`audio.describe()` returns every backend's id and label, and for the backend in force its drivers (id, label), per
driver the device names and the default's name, and the auto choice (which driver "auto" resolves to right now, for the
row "Auto (PortAudio: PipeWire)"), and the status; a sibling's drivers come from `backendDrivers`. `apply` validates
(channels 2, 4, 6, 8; rate and block positive) and returns the status. `midi.describe()` returns the hardware port lists
(filtered), whether virtual ports exist, and the reserved pair, so the checklist can mark a port the control surface
holds.

### 6.5 Menus

The rows move out of the shell, with the files they live in: `audioDraft.ts` and the audio rows of `menuDefs.ts`
(`:137-199`, `audioSettingsChildren`) into `audio`'s `ts/ui/`; `midiDevices.ts`, the device pickers (`:241-290`,
`deviceCyclerNames`, `inputCyclerNames`, `midiSettingsChildren`) and the MIDI Clock submenu (`midiClockChildren`,
`midiClockRow`, `transport.ts`) into `midi`'s `ts/ui/`. The shell keeps the Audio and MIDI submenu containers and the
clock slot (report item 11): it has the placement `settings.midi` and none for audio or the clock
(`ui/ts/contribution.ts:160-171`), so it adds `settings.audio` and `instance.clock`, and each package fills its own.

- **Settings > Audio**: one Driver row whose entries name the backend ("Auto (PortAudio: PipeWire)", "PortAudio:
  PipeWire", "PortAudio: ALSA", ...), Output Device, Rate, Block (with the latency label), Out Channels, Apply (the
  draft model stays: the rows edit a pending value and Apply commits it, `audioDraft.ts:2-9`), and a status row when
  muted ("Muted: <reason>", an `infoRow`). **As built the Driver row lists the backend in force and the choices that
  need no new library, and a "More drivers..." action (`keepOpen`) fetches the next sibling backend's drivers.** The
  design had the row list every backend ("SDL: pipewire" beside "PortAudio: PipeWire") from one description, but
  enumerating a backend initialises its library, and a menu builder runs on every render (the start menu renders every
  frame while no project is loaded), so the first S3 pass brought PortAudio and SDL audio up at launch with `null`
  pinned: only the `LD_PRELOAD` guard kept the sound card closed, and the `strace` that had been offered as proof cannot
  see a refused open (S3 review C1). So: no builder asks a sibling; the step is the user's, once per visit (the cache
  drops when the backend in force moves, and "asked" is tracked apart from the answer, so a sibling that answers `[]`
  is not fetched again and the row cannot stall on it); the row is hidden under a `--audio-backend` pin and the native
  hook returns `[]` under one, so a pinned run cannot initialise another backend whatever the TS does; a saved choice
  of a sibling not yet fetched still gets a row labelled from its ids, so the row never shows "Auto" over a saved "SDL:
  pipewire". **Apply stays enabled while the status carries an error** (re-applying the saved section is the retry a
  failed open, a lost device or a watchdog trip has), and the audio service polls `status()` so the Muted row appears
  without an unrelated repaint. A "This run: ..." row names what this run's flags pin over the saved choice.
- **Settings > MIDI**, headed "saved with this project": an inputs checklist plus "All devices", an outputs checklist,
  and the entries that do not resolve as "<name> (missing)", and a "Rescan" row that re-resolves the saved names against
  a fresh port list (a Reopen apply). The menu model has no checkbox kind (`MenuItemKind`,
  `packages/ui/ts/menu/menuTree.ts:14`), so a checklist is a column of `action` rows with `keepOpen`, labelled `[x]` /
  `[ ]`. A change calls `project.setSection("midi", ...)`, which marks the project dirty and so raises the close guard,
  and applies at once through the service below. The Launchpad's scan rows still land under `settings.midi`. As built
  the rows' semantics are the S3 review's: a row is checked when some saved entry RESOLVES to the present port
  (`resolvePort`) and the stream opened it, not when the name is exact; a tap on a checked row removes every entry
  resolving to it and turns All devices off (so the tap always does something real); a tap on an unchecked row adds it,
  and an unchecked SAVED row retries with a reopen instead of removing the entry; a port the control surface holds shows
  "<name> (control surface)", unchecked, and is never offered or "(missing)"; a "(missing)" row is an entry that opened
  nothing, read from the stream's `openedInputs` / `openedOutputs` rather than inferred from a listing taken at another
  moment.
- **The MIDI Clock submenu** moves, its tempo (`Tempo: N BPM`) now a write to `midi.clockBpm`; the transport's play
  state is still not persisted (it starts stopped at every launch, `main.cpp:121-185`'s comment).

A **UI service** in `midi`'s `ts/ui` (`defineUi({ services })`, `UiService` in `ui/ts/contribution.ts:244-250`) watches
`project.section("midi")`: its `start(host)` runs at every mount (it must be idempotent), `poll` compares the section
with the last applied selection and, on a difference, resolves each saved name against `midiDevice.describe()` (6.3) and
calls `midiDevice.apply(...)`; it also applies `clockBpm` through `setClockBpm`. A project switch changes the section
and so re-applies, with no audio stop (4.3). There is no hotplug, so the service re-resolves (a list probed again) at
every project load and whenever the Settings > MIDI submenu is opened, and the submenu has a "Rescan" row that does the
same on demand (report item 12).

**As built the re-resolve is edge-triggered, and never a render.** The first S3 fix for "also on opening the submenu"
had the menu builder request a re-apply, and the apply repainted, so the service re-applied on EVERY frame whenever a
menu was on screen (the start menu is, until a project loads): sixty frames made sixty applies and 120 port listings,
each an RtMidi probe object (on ALSA a sequencer client created and destroyed), `/dev/snd/seq` opened 250 times in a
null-pinned run against 28 before, and on Windows a refused input retried and logged at frame rate (the
whole-prototype review's C1). Now: `MenuBuilder.onOpen(host)` is a member of the UI contract (and `MenuItem.onOpen` /
`submenu()`'s optional `onOpen`), run by `Menu.tsx`'s `activate` when a submenu OPENS (never on a render or a close),
and the shell's container rows take their open hooks from `menuPlacementOpen(placement, host)`, so the shell still names
no package. The service re-reads and diffs on the mount, a section change, a project load (`project.revision()`), the
container opening and a fresh listing that is not the one it applied against, and its `poll` answers `true` (repaint)
only when what the rows show moved (listing, opened ports, the project's names, the clock and transport the rows
print). The rows render from the listing the service last read, never ask a device, and read one only when no service has
run yet (a menu built in a test, the first frame of a mount, keyed by device). The committed case runs the
build/poll/rerender cycle for 60 frames and holds `applies=1, repaints=1, describes<=3`; the null-pinned `strace` run
is back to 22 sequencer opens.

The first apply of BOTH services lands on their first POLL, not in `start`: services start in product order (`audio`,
`midi`, `launchpad`), so a hardware port opened in `start` would be closed again by the Launchpad's claim (refused
outright on Windows), and the rendezvous pumps UI frames so that those first polls land while the host still holds
everything (6.6).

### 6.6 Startup

The SDL app's rendezvous holds audio and MIDI until TS's `restored()` or 500 ms (`restoreControlSurface`,
`main.cpp:1000-1011`, which today waits only when the product started the Launchpad provider). It widens to a second
condition: TS's first `audioDevice.apply`, issued from the `audio` section by the audio service's first poll (the
rendezvous pumps UI frames, so those polls run). `setupSdl` no longer opens a device (`:1437-1441` goes): it initialises
video and the game controller, and the audio backends initialise their libraries at first use. The virtual MIDI ports
open at once (nothing in the file chooses them); the hardware ports open when a project's section is applied, which is
after the autoload (`:1670`) and the UI mount; the control surface's claim, reserved from TS's `restored()`, still
precedes any hardware open of that port, which is what `sdl:controller` leg 2 holds (section 9).

**As built the audio device does NOT open inside the rendezvous.** The design said "native opens audio after TS's first
apply", and the first S3 pass did exactly that, so the device opened and started on the audio service's first poll,
before `app.midi.open()` and before `rp_restore_providers` (the N8's reconnect ran with the audio thread live, unproved
and unlisted). The whole-prototype review's I3 held that the app's order is claim, MIDI, providers' restore, audio last,
and no new edge was wanted, so the native `__rp_audioApply` only RECORDS the request (`AppState`'s `req*` fields) and
sets `audioApplied`, the word the rendezvous waits on; `main.cpp` opens the device at its one old point, after
`app.midi.open()` and `rp_restore_providers`, with the recorded request (or the defaults when TS never applied), and
`audioDeviceReady` then makes a UI apply live, as before. `waitForUiStartup` (the renamed `restoreControlSurface`)
waits on the control surface's `restored()` where the product started the Launchpad provider and on `audioApplied`, and
prints "audio: the UI did not apply its settings; starting on the defaults" at the bound. `sdl:smoke`'s order leg holds
the order off the log ("the MIDI stream opens before the audio device"), and the startup line for a pinned run says
"on the null device (pinned by --audio-backend null)", not that no device chose it. A TS apply the host receives in the
rendezvous is therefore never followed by a second open with a shim's config, and the old sentence "audio last" stays
true.

Two consequences the order carries, both found in verification: the engine is constructed before any device is open, so
`bootControlPlane`'s `engine.setSampleRate(a.sampleRate)` (`:1335`) reads the default 48000 rather than an obtained
rate, and a configuration that differs gets one in-place re-rate at `prepare` (`Engine::setSampleRate`,
`Engine.cpp:682-690`; `reconfigureAudio` already relies on it): The user's decision (report item 1) keeps this order and
holds it with the engine-graph case of 5.7 (a re-rate bit-equal to a fresh build, GB and NES), moving the rendezvous
before the autoload only if that case fails. It did not fail: it passes on the real engine (5.7 as built). The
rendezvous waits on TS, so a product whose TS never applies (a broken bundle) costs the 500 ms and then starts on
defaults, as today's does for the restore.

### 6.7 What changes, by file

```
packages/audio/package.json, ts/
    app module: definePackage({ configSections: [audio] }); ts/configSection.ts; ts/ui: audioDraft.ts, the rows and
    the service; ts/testSupport/fakeDevice.ts (the hooks over an owned description, no library initialised)
packages/midi/ts/
    app: projectSections [midi]; configSection.ts; portMatch.ts; ts/ui: rows, clock, the service; testSupport/fakeDevice.ts
packages/core/ts/definePackage.ts
    + configSections (the same ProjectSection type)
packages/core/ts/host.ts
    ConfigHost -> section/setSection; + AudioDevice, MidiDevice; ControlSurfaceSettings leaves
packages/launchpad/ts/
    ControlSurfaceSettings + its configSection (key controlSurface);
    ui/launchpadDevices.ts reads host.config.section("controlSurface")
packages/retroplug/scripts/lib/packages.mjs
    CONFIG_SECTIONS registry + refusals (core key, duplicate); the UI generated modules take the
    new placements
packages/retroplug/src/configSections.ts (new)
    CORE_CONFIG_KEYS, the entry and contributor lookups, parse/serialize/readable per section, the F5a stamp rules
packages/retroplug/src/userConfig.ts
    controlSurface schema deleted; ConfigModel { core, sections, raw, written }; raw non-core keys carried
packages/retroplug/src/userConfigSerialization.ts
    writes contributors' sections, then unknown keys, verbatim
packages/retroplug/src/userConfigStore.ts
    controlSurface()/setControlSurface() -> section()/setSection()
packages/retroplug/ui/host/{uiHost,nativeHooks,menuContributions}.ts
    the config group; audioDevice/midiDevice (optional); the eight globals -> the six new ones; menuPlacementOpen
packages/ui/ts/{contribution.ts,menu/}
    PLACEMENTS gains instance.clock and settings.audio; MenuBuilder.onOpen, MenuItem.onOpen
packages/retroplug/ui/screens/menu/
    audioDraft.ts, midiDevices.ts, transport.ts, the audio/MIDI/clock rows move out
packages/retroplug/package.json
    + @retroplug/audio, @retroplug/midi
test-ui/
    audio-device, audio-driver, midi-settings, midi-clock: moved with the rows
test/menu/{recorded-cases,leaves}.ts, golden
    the three seam cases re-recorded (twice: S3, then the fix pass), with RP_MENU_MATRIX_UPDATE=<case>[,...]; the
    matrix's other cases held
```

### 6.8 Proof

**RED first.** (1) The config-section rules, as one test file over a fixture product: a section under a core key is
refused by the generator; a duplicate key (one package twice, two packages) is refused; a section stamped newer than its
contributor is read as defaults, refused by `setSection` with one warning and written back byte-identical; a root key
with no contributor survives `parseUserConfigResult` and `serializeUserConfig` unchanged (the RED run is the existing
strict schema, which strips it: `userConfig.ts:98-109`); the plugin product (no `audio`, no `controlSurface`) reads a
file carrying both and writes it back with both intact. (2) `portMatch`'s table: exact, ALSA renumbering (`24:0` against
`28:0`), the WinMM index prefix, CoreMIDI, two candidates (no match), none (no match), spacing; RED against a matcher
that returns the first candidate. (3) The `midi` service over a fake `midiDevice`: a section change applies once (not
every poll), a missing name opens nothing and shows "(missing)", a project switch re-applies, `clockBpm` reaches the
transport call, and a throw in `apply` is isolated. (4) The `AudioDevice` rows: the draft model and the Apply commit,
the Driver labels against a described list ("PortAudio: PipeWire" beside "SDL: pipewire" once the sibling is fetched),
the "More drivers..." step, the status row, and a host with no hook hiding the submenu. (5) The `controlSurface` move:
`launchpad-devices.test.ts` and `menu/launchpad.test.ts` re-pointed at `host.config.section`, with `config.json`'s
`controlSurface` bytes compared before and after the move on a recorded file (E8). **Held:** the buildGraph and role
goldens' documents (no model moves; the tests that deep-equal a fresh project's settings gain `midi`:
`test/project/config.test.ts`, `sections.test.ts`, `tolerance.test.ts`, `test/graph/plugin-product.test.ts`), the
recorded menu matrix except its three audio/MIDI cases, the parity, kit and render baselines, `check:boundaries` with
`audio` and `midi` (rule 9's lists need no entry: the shell imports no new package by hand), the literal scanner over
`src`, `cli` and `ui`.

**As built.** The REDs were run, as design asked: `resolvePort` returning the first candidate fails "two candidates
resolve to nothing; so does none" (6 of 7 pass); the S2b config files against a probe expecting the raw sections fail
`cannot read property 'sections' of undefined` (the read half) and `got undefined` for the plugin erasing a standalone's
`audio` block (the write half); the generator over a fixture whose app declares a section under `sramAutoSave` succeeded
and emitted no registry; and the seam change itself failed typecheck (`__rp_getAudioConfig` does not exist on `RpHooks`),
which records every re-pointed test as a named edge, not a silently rewritten one. Each review finding then got a RED of
its own run on the pre-fix tree (the reviews' probes, committed as the cases): the section write over the re-read file
(two cases), the checklist's four mappings, the re-resolve edges, additive fields, a muted device's retry, the override
keeping driver and device, `portMatch`'s Windows and two-port ALSA rows, the 60-frame cycle (`applies=60 repaints=60
describes=120`, then 1, 1, at most 3), the shell-field write (`expected {"backend":"portaudio","bufferCount":4}, got
{"backend":"portaudio","driver":""}`), and the "More drivers..." stall. The tests that moved with the rows are
`test-ui/audio-device`, `audio-driver`, `midi-settings` and `midi-clock`. New files: `test/config/config-sections.test.ts`,
`packages/midi/ts/test/{portMatch,midiService,configSection}.test.ts` and `packages/audio/ts/test/{configSection,
audioDraft}.test.ts`, the generator's three cases, and the two packages' `testSupport/fakeDevice.ts` (hooks over an
owned description, so no test can initialise a library: the Zoom L6 rule). The fixture's driver list for the audio
rows describes only the in-force backend and counts `backendDrivers` calls, with a case that builds the start and
instance menus and asserts ZERO calls (the first fake handed back drivers for every backend, which is why no test could
see the launch-time initialisation). The recorded matrix's three seam cases were re-recorded twice and the final record is
the oracle; the re-record also gained the MIDI Clock rows in `standalone-instance` and `standalone-scan`, because the
fixture now fakes the transport: a fixture change, not a behaviour one. `reaper:params` (VST3 2725 before and after,
CLAP 644, under the guard) ran at the tip, not at S3, where the plugin's bundles had already changed (the config
serializer, the generated registries).

## 7. Stage 4: the SDL-audio leg and the docs

**`sdl:pipewire` and the SDL-audio mode.** `tools/run-sdl-pipewire.sh` writes its configurations as `audio.json`
(`CFG="$HOME/.config/retroplug/audio.json"`, `:53`, `:108`, and the `writecfg` lines `:126-154` and `:192`); after F5b
it writes a `config.json` with a stamped `audio` block (`{"schemaVersion":3,"audio":{...}}`: `backend`, `driver`,
`device`, `sampleRate`, `blockSize`, `outChannels`) and drives the PortAudio checks with `--audio-backend portaudio` (or
`RETROPLUG_SDL_AUDIO_BACKEND`), which overrides the saved BACKEND for the run and keeps a saved driver and device when
the section names the same backend (E12; the first pass cleared them, which made the explicit-pick, stale-pick and
wide-output legs unexpressible). The one leg that pins nothing is the AUTO leg, which runs first with `backend: "auto"`
and no flag, the state a user's `config.json` is in, and expects PortAudio's PipeWire on the session default (the first
version pinned every leg, so nothing exercised `AudioHost`'s Auto resolution or PortAudio's PipeWire preference, which is
what every user gets). Its greps do not change: the `sed` over
`audio: PortAudio [..] '..' N Hz` (`:117`, `:201`), `grep "not found in the chosen driver"` (`:144`), the wide-output
`audio: PortAudio .*512 frames, 8 ch` (`:206`), and `pw-link -o | grep -c '^retroplug-sdl:'` (`:198`: the PortAudio
stream's node name). The explicit-driver case's `"hostApi":18` becomes `"driver":"pipewire"`. A **new mode of the same
script**, `--audio-backend sdl` (the leg the approved section adds): the default sink is followed, an explicit device
pick beats it, a stale pick falls back with a warning, 8 channels, and a live-menu leg, all against the same private
PipeWire server with the three null sinks. The live-menu leg is the C2 proof: the new `RETROPLUG_SDL_TEST_MENU=<frame>`
hook drives the real menu from the SDL queue to Settings > Audio while a stream runs, and the leg holds the stream open
across the build (the same node, exactly one open line). The selection, wide and menu legs start wireplumber on a private
dbus, because SDL's PipeWire driver needs the session graph, and are counted as SKIPPED (the summary prints the count)
when it is missing; the SDL stream's node is read from the graph with the test sinks and their monitor ports excluded (the
alphabetically-first output node could be `rp-decoy`'s monitor, and a sink's port count could then stand in for the
stream's). Its greps are the SDL backend's own log line (the same form
with `SDL [pipewire]`), and its node-name prefix is read from the run, not assumed, because SDL names its PipeWire node
from its app-name hints (`setupSdl` sets `SDL_HINT_APP_NAME` to `RetroPlug`, `:1388`). SDL's multichannel layouts (4 is
quad, 6 is 5.1, 8 is 7.1) differ from PortAudio's "declared layout" stream, so "pair 1 linked" is verified in the leg,
not assumed (risk, section 11), and what it found is R11: the SDL stream's node has 2 output ports even for an 8-channel
request, so the leg asserts the API's 8 channels and pair 1 linked, not 8 graph ports. The ALSA-fallback leg holds a
weaker claim than before: not "an ALSA device opened" (the first F5 script required it) but the CLEAN degradation, because
in this container PortAudio's ALSA backend cannot enumerate the private config's null PCM (its format probe rejects a
virtual PCM) and the guard refuses the card control nodes, so the host reports "no default output device" and falls to
`null` with a status, exit 0, no `/dev/snd/pcm*` node attempted and the private config's default the null PCM; on a
machine where PortAudio's ALSA can list that PCM the leg passes on the open instead and says which happened. Not in CI
(the runners have no PipeWire); the devcontainer ships one. The run goes through the device guard (decision 30): 25
checks pass and 0 are skipped at the prototype (27 after the follow-up's stale-driver leg, which holds that a saved
driver this run does not have falls back to Auto), the guard having refused every card CONTROL node PortAudio's ALSA
probe asked for (about a thousand paths a run, no PCM node) and logged about 300 audio-library dlopens.

**The guard** is `tools/sdl-audio-guard/` (`nosnd.c`, `build-guard.sh`, `guard-env.sh`): an `LD_PRELOAD` shim refusing,
before the syscall, every `/dev/snd` node but `seq` and `timer`, every video node (`/dev/video*`, `/dev/media*`,
`/dev/v4l-subdev*`, which a session manager's v4l2 and libcamera monitors open), every serial node (`/dev/ttyACM*`,
`/dev/ttyUSB*`, `/dev/ttyDbgProbe*`, `/dev/serial/*`: a Pico box or a probe), `/dev/hidraw*` and `/dev/bus/usb/*`. It is
tracked source built by each harness into its own scratch directory and put in front of `LD_PRELOAD` (unless the same
guard is already there, as the kit's copy is, so audio-library dlopens are not counted twice); `sdl:smoke`,
`sdl:controller`, `sdl:pipewire` and `tools/reaper-env.sh` all source it, so a bare `pnpm sdl:controller` and a bare
`pnpm reaper:*` are guarded. `guard-env.sh` then SELF-TESTS it: a probe open of `/dev/snd/nosnd-selftest` must be
refused and logged, else the harness exits non-zero, because ld.so silently ignores a preload it cannot load and a guard
that never ran is the failure that looks like safety. `NOSND_LOG` records each refusal and each audio-library dlopen that
was not already loaded, and a null-pinned run's log is EMPTY, which the kit's `nosnd` leg gates. (An `strace` of the
first run proved nothing: the guard refuses in the libc wrapper, so a refused open never reaches the syscall.) The first
version of the harness required an untracked `.superpowers/verify/nosnd.so`, loaded the guard only when `LD_PRELOAD` was
empty, and was documented as the repo's: that is what the last fix pass removed. The shim is Linux-only, and
`guard-env.sh` skips it with a notice elsewhere.

`sdl:pipewire`'s private wireplumber starts from a copy of `/usr/share/wireplumber` with its bluetooth context dropped
and `main.lua.d/90-enable-all.lua`'s `alsa_monitor`, `v4l2_monitor` and `libcamera_monitor` disabled: unmodified, its
monitors open the capture card on `/dev/video0` and `/dev/video1` and mirror the MIDI interfaces' sequencer ports. The
script checks the copied file for an active `enable()` and the daemon's log for a loaded monitor, and the guard would
refuse the open anyway (it shows up as a refusal in the run's log, which fails the summary).

**`tools/check-audio-linkage.sh`** is the 5.7(4) check, a kit leg (`audiolink`): `readelf -d` or `otool -L` and `nm -g`
show no `Pa_*` symbol, no backend factory and no audio-library dependency in `retroplug-cli`, `retroplug-host` and the
Node addon; `retroplug-sdl` keeps its backends static.

**`sdl:smoke`** pins `RETROPLUG_SDL_AUDIO_BACKEND=null` and names no audio driver (section 9); **`sdl:controller`** is
rewritten around the project section (section 9). The smoke gained legs for what the stages added: the Settings > Audio
seam through the real hooks, E12 (`--block-size` over a saved section), E7 (a project tempo the hook does not set), the
startup order, and `RETROPLUG_SDL_TEST_MENU`.

**The docs**: `AGENTS.md` (the SDL and CLI sentences, the providers paragraph gains `midi`, a paragraph for the audio
backends and `AUDIO_BACKENDS`, "config sections", the `test:plugin` count, which gains `retroplug-audio-host-test`,
thirteen to fourteen, and a `retroplug-midi-test` description over the stream: report item 14); `spec/00` to `spec/06`
where a figure moved (`spec/05` for the `audio` root section and the `midi` project section and the two ignored files;
`spec/06` for the new product wiring, the new tests, the harness changes and the root-scope PortAudio); `spec/07` (the
F5b entry, the per-device-routing follow-up and the `feature/handheld-perf` rebase note, F5b removed from the open
list); `docs/sdl-standalone.md` (the persistence, audio-backend and MIDI-selection paragraphs, `:64-76`, `:104-123`,
`:195-246`, which still describe `audio.json`, `midi.json` and a PortAudio-only host); and the release-readiness ticks
(`docs/sdl-release-readiness.md`, each naming the design's stage, not a commit hash: the fix passes rewrote the history,
so a hash would have been wrong before it shipped): the `Pa_StopStream` lock, the dead device, the bad persisted config,
the per-process RT tuning, the unversioned `audio.json` / `midi.json`, and the `main.cpp` split. The first S4 docs said
things the tree no longer did (the providers sentence, `reserve_midi` keeping a pair out of a `MidiIo` that no longer
exists, "never silence" for a backend that will not open, `audioCb`, `RETROPLUG_N8_BRIDGE`, the muOS launcher's claim that
a Settings pick still overrides its block); the last fix pass corrected each. `check:docs` stays green (relative links
only to tracked files), with one warning: `check-doc-links` now reports a dead link inside a `*-plan.md` record as a
warning rather than editing the record.

## 8. Behaviour edges

What a user, a script or a test sees change. Each is listed with what was found; E1 to E9 are the approved list.

- **E1. `audio.json` and `midi.json` are ignored; settings reset once.** Nothing reads them after S3; they are not
  deleted, not imported (the SDL app never shipped; `docs/sdl-release-readiness.md` section 4 called them unversioned
  and non-atomic, and `midi.json` already changed meaning silently once, `""` from All Devices to None). The harnesses
  write them today: `tools/run-sdl-pipewire.sh:53, :108` (`audio.json`) and
  `tools/run-sdl-controller.sh:140, :194, :225` (`midi.json`); both are rewritten in this phase (sections 7 and 9).
- **E2. MIDI ports are per project; a fresh project has none.** A project saved before F5b carries no `midi` block and
  reads the default (virtual ports only, which is also what an unconfigured install had: `MidiIo.hpp:196-205`). A user
  who had picked a hardware input in Settings > MIDI re-picks it once per project. The selection travels with the file,
  matched by name on another machine (6.3).
- **E3. Inputs and outputs are multi-select.** The old single `input` / `output` become lists, merged into the one host
  MIDI in and mirrored to every selected output. A hardware input is still never opened unless chosen (the default stays
  None; `all` is the explicit every-device choice).
- **E4. The Driver row names the backend, and SDL audio can be picked.** "Auto (PortAudio: PipeWire)", "PortAudio:
  ALSA", and, after the explicit "More drivers..." step (6.5: a menu build never initialises a backend), "SDL:
  pipewire". PortAudio's log line is byte-identical to today's (5.2); SDL's is the same form.
- **E5. A MIDI change or a Launchpad claim no longer stops audio.** `reconfigureMidi`, `onReserveMidi` and
  `onSuspendInputs` lose their `stopAudio` / `startAudio` (4.3). What `sdl:controller` reads that this touches: the
  claim still logs `control surface: reserved MIDI input '<port>', output '<port>'` (`main.cpp:959-969`, grepped at
  `run-sdl-controller.sh:148, :160, :209, :231`), the stream's lines are unchanged, and no script greps an audio stop,
  but leg 2 compares the claim's line with `] MIDI: virtual ports` (`:165-169`), and under decision 23 the virtual ports
  now open BEFORE the claim, so that comparison cannot stay; section 9 gives its replacement (report item 15), which
  is as built: the claim precedes `] running ` and the claimed port is NEVER opened as a hardware input by the stream.
  A real device change (`audio.apply`) still stops and restarts the stream, once.
- **E6. A failed or lost device shows "Muted: <reason>".** Today a failed open prints once and runs the pump
  (`main.cpp:1130, :1143-1146`), a persisted request the device rejects mutes on every launch with no way to see why,
  and a device that disappears freezes the app (the emulator advances only inside the callback). After F5b each is the
  same fallback to `null` with a status the Settings row shows, and Apply stays enabled while the status carries an error
  (the retry). A watchdog trip stays muted until the next apply: there is no automatic retry or backoff.
- **E7. The clock tempo is saved in the project.** It was `audio.json`'s `clockBpm` (`:1197`). A project switch applies
  the new project's tempo; the transport still starts stopped.
- **E8. `config.json` gains `audio`; `controlSurface`'s bytes are unchanged.** The Launchpad's section keeps the root
  key and the four fields. The shape of the file changes by one key; `schemaVersion` stays 3. A PRE-F5b build sharing the
  file sees an unknown root key: the standalone keeps it verbatim, and a pre-F5b PLUGIN (whose root schema is strict)
  drops it on its next write - the unavoidable cost of releasing the plugin beside a newer standalone, and what the
  version stamp exists to make visible.
- **E9. A project saved in the standalone carries `midi`, which the plugin keeps verbatim.** A fresh project written by
  the PLUGIN has no `midi` block (F5a's E8 at one more key); a `.rplg` the standalone wrote opens in the plugin and back
  with the key where it was.

**Added by the draft-report decisions** (not in the approved list):

- **E10. On Windows the stream logs what is true.** Today `MidiIo::open` prints two RtMidi WinMM warnings and then
  announces virtual ports that do not exist (4.1(c)); after F5b `supportsVirtual()` is false there, no virtual port is
  attempted and the line is not printed. Linux and macOS are byte-identical (report item 16: accepted).
- **E11. The CLI's `midiOpen(client, "")` opens every hardware input again** (the user's item 6). It read none since the
  input default changed to None (`MidiRpcService.cpp:15-18`), against the documented contract (`core/ts/host.ts:63`,
  `MidiRpcService.hpp:31-33`) and what `n8-bridge` with no `--midi-in` expects (`n8/ts/bridge.ts:51`). A `test:native`
  case holds it (run with the port filter set, so no real device is opened); a named input still opens that port alone.
- **E12. `--block-size` and `--render-ahead` override the saved choice for that run** (the user's item 8), like
  `--audio-backend`; before, a saved `audio.json` won over `--block-size` whenever one existed. The consequence for the
  handheld: the muOS launcher's `--block-size 4096` now ALWAYS wins, so a Settings block pick there never takes effect
  (the Settings rows say "This run: block 4096 (a flag overrides the saved choice)", and the launcher's comment and
  `docs/sdl-standalone.md`'s muOS notes say so). `--audio-backend` replaces only the backend and keeps a saved driver and
  device when the section names the same backend.
- **E13. Startup builds cores before the device is open** (the user's item 1): cores are built at the default rate and
  re-rated once at `prepare`; held by the re-rate equality case (5.7), which passes on the real engine.
- **E14. On Windows PortAudio's Auto prefers WASAPI** (the user's item 10), where it resolved to MME; the Windows agent
  verifies it, and a saved `driver` still wins. The prototype found a hole in it that the run should not be the first to
  find: `PortAudioBackend` sets no `paWinWasapiAutoConvert`, so shared-mode WASAPI refuses a 48 kHz request on a 44.1 kHz
  mix format, the open fails, and Auto moves to the next BACKEND (SDL), not to MME as R2 expected. Setting the flag (a
  `PaWasapiStreamInfo` in `hostApiSpecificStreamInfo` when the host API is WASAPI) is cheap and should not wait for the run.

Not edges, and why: the engine's output (`renderAudioBlock` is the same body, the engine renders what it rendered; the
parity, kit and render baselines hold), any graph document, the plugin and the DPF standalone, the CLI's RPC surface,
and every Launchpad behaviour (the pack's 53 cases hold).

## 9. Proof

**The oracles held**, compared before and after each stage that can reach them: the strict parity baseline
(`pnpm test:native parity`, 197 cases in 26 groups), the kit baseline and the render baseline, the `buildGraph` and role
goldens' documents, the protocol-node digests, `edio-golden.json`, the device-check and box image hashes and the
bliptoaster pack bytes (nothing portable moves), the recorded menu matrix except its three audio/MIDI cases, the
registration golden (no package adds a node type), and `reaper:params` (VST3 and CLAP: the plugin is untouched, which
this proves rather than assumes). **Rules held:** `pnpm typecheck` (every program), `pnpm check:boundaries` and the
native boundary check judging `audio`, `audio-portaudio`, `audio-sdl` and `midi` (the shell's `SHELL_IMPORTS` /
`UI_IMPORTS` need no entry), the literal scanner, `check:docs`, and the Windows `/W4 /WX` build (11).

**RED first, and the new tests**, by stage, are in 3, 4.7, 5.7 and 6.8; the table below is what the verification found
the scripts read, which is what makes "the harnesses' greps hold" a checked claim.

| Script | What it greps or writes | After F5b |
| --- | --- | --- |
| `sdl:smoke` (`tools/run-sdl-smoke.sh`) | `running `, `clock self-test`, `transport self-test: playing=1 bpm=140`, `post-multiout: numOutputs=8 planarBufs=8`, `post-resize`, `wheel`, `close-guard`, `[idle-reclaim]`; exports `SDL_AUDIODRIVER=dummy` and `RP_MIDI_PORT_ALLOW=RP_NO_PORT` (`:28, :37`) | Unchanged lines, plus the legs the stages added (the Settings > Audio seam through the real hooks, E7, E12, the startup order, the real menu through `RETROPLUG_SDL_TEST_MENU`). It pins `RETROPLUG_SDL_AUDIO_BACKEND=null` and, as built, no longer exports `SDL_AUDIODRIVER`: with the dummy driver exported and `sdl` second in the preference order, an `auto` run on a runner whose PortAudio finds no device would open SDL's dummy driver and run on that instead of the sleep-paced pump the script means (report item 13). It also runs under the `/dev/snd` guard (7) `RETROPLUG_SDL_TEST_RECONFIG` and `TEST_MULTIOUT` keep calling the reconfigure through `AudioHost` and print the same `post-` lines |
| `sdl:pipewire` | `audio: PortAudio [h] 'd' N Hz`, `not found in the chosen driver`, `audio: PortAudio .*512 frames, 8 ch`, `pw-link` ports; writes `audio.json` | Greps unchanged (5.2). Writes `config.json`'s `audio` block, an Auto leg with no flag first, a new SDL-audio mode with a live-menu leg, an ALSA-fallback leg that holds the clean degradation, skipped legs counted (section 7); runs guarded, 25 passed 0 skipped |
| `sdl:controller` | writes `midi.json` three times (`{"input":"*",...}`); `reserved MIDI input`, `] MIDI: virtual ports`, `] running `, `MIDI in: 90 51 7F`, `MIDI out: '<p>' is held by the control surface`, `hardware inputs suspended`, `scan test: started=true`, `could not claim`, `in use by another RetroPlug`, `did not report its restore`; python over `config.json`'s `controlSurface` | The `midi.json` writes become a project with a `midi` section autoloaded (`--autoload`), since the selection is the project's now: leg 1 and leg 5 load an all-inputs project (`"midi":{"allInputs":true,...}`), leg 4 adds `"outputs":[{"name":"<port>"}]`, leg 3's project (`controller.rplg`) is unchanged. Leg 2's ordering `reserved_at < midi_at` is replaced: the claim must precede `] running ` and the claimed port must never be opened as a hardware input by the stream (stronger than "the first line"); leg 4's `claim_at > running_at` (claimed mid-session) holds. The python reads `config.json`'s `controlSurface` unchanged (E8). The script pins the `null` backend and sources the guard, so a bare `pnpm sdl:controller` is guarded; the kit skips it (it is run alone) and the NOSND log of an alone run is empty |

**What is not proved, said plainly.** The real Launchpad (manual, as F3); the muOS device run of render-ahead (owed
after `feature/handheld-perf` rebases); a real PipeWire, PulseAudio, CoreAudio, WASAPI or MME stream under the SDL
backend beyond the new leg on this machine; a MIDI hotplug (there is none); Windows and macOS builds and runs of every
new target and of `portMatch`'s table on real names (section 11).

## 10. The stages

Six stages on a prototype branch `proto/f5b-audio-midi`, one commit each, DeepSeek Flash implementing and Opus
reviewing (F4a to F5a's routing). As run, the reviews were S0 with S1, then S2, then S2b with S3 (the S3 review also
judged the S2 fix pass and the split), then the whole prototype, and each review's findings became a fix pass folded
into the stage that owns the file (autosquash, with one stop for S2b's namespace move), so no stage commit narrates a
review. Each stage leaves the tree green on every oracle of section 9 it
can reach. The shas below are the PROTOTYPE branch's (`proto/f5b-audio-midi`, unpushed); 13.1 maps each to the commit on
`feature/modularize` that replays it. After any fix-pass rebase, each stage's tree is extracted after the final rewrite, proven equal to its
commit, and typechecked and tested alone, never symlinking the worktree's `build/` for a run that writes.

0. **The SDL branch merge** (section 3). *Exit:* `retroplug-midi-test` 25 cases and the TSan ring case green;
   `test:plugin`, `sdl:smoke`. As built: `605c8b6a3` (`925fd40d9` on the prototype), 25 cases, TSan clean, the review
   found it exact.
1. **The `midi` package** (section 4): the interface, `MidiStream`, the fake, the facet as a provider, the Launchpad
   onto it, the CLI's `PROVIDERS`, the harness edits `sdl:controller` needs for the lines this stage moves. *Exit:* the
   output-swap case under TSan; the Launchpad's 53 cases over the fake; `launchpad-facet`, `launchpad-devices` and the
   N8 bridge sessions in `test:native`, plus the new case for E11 (`midiOpen(client, "")` opens every hardware input, a
   name opens that one); `sdl:controller` green UNCHANGED: until S3 the app keeps reading `midi.json` through a small
   shim that maps its one `input` / `output` onto `setInputs` / `setOutputs` (`"*"` is `all`), so the stage moves no
   startup order and no harness line (section 9 lists what S3 changes). As built `7b547cf29`, with the `tsanmidi` leg
   added to the kit.
2. **The `audio` packages and `AudioHost`** (section 5). *Exit:* the `AUDIO_BACKENDS` refusals and positive controls;
   the `AudioHost` cases (call order with the obtained rate, fallback, lost device, render-ahead, the watchdog) and the
   engine-graph re-rate equality case; a configure and `all` build with both backends linked into `retroplug-sdl` and
   neither into any other product; `sdl:smoke` on `null`; `sdl:pipewire` with its greps unchanged on a PortAudio run. As
   built `6bb3469ef`; `sdl:pipewire` did NOT run at S2 (the user's strict Zoom L6 rule, decision 30): it moved to S4,
   guarded, so the PortAudio move was proved by reading until then. `tsanaudio` joined the kit.
2b. **The `main.cpp` split** (section 5.5): a pure move, `ede472ed7` (`1932627c6` on the prototype). *Exit:* 1676 of
   1683 lines verbatim plus the seven reshaped ones named in 5.5, a configure and `all` build warning-free at the tree,
   `sdl:smoke` and `sdl:controller` green on `null` under the guard.
3. **TypeScript** (section 6). *Exit:* the config-section rules, `portMatch`'s table, the service and row tests (Rescan
   and the `--block-size` override among them); the recorded matrix's three cases re-recorded with the diff as the
   record; `sdl:controller` on the project-section form; `reaper:params` on the plugin's bundles (no plugin file
   changed). As built `6568fe4c6`; `reaper:params` ran at the tip (2725 / 644), not at S3.
4. **The SDL-audio leg and the docs** (section 7). *Exit:* `sdl:pipewire` in both modes; the docs pass; the readiness
   ticks. As built the tip of `proto/f5b-audio-midi` (`27f99be3d` on `feature/modularize`): `sdl:pipewire` 25 passed 0
   skipped under the guard at the prototype (27 after the follow-up), the linkage check, the guard tracked, the docs.

After the prototype: a plan generator, Flash-Next implementers, the serial acceptance kit (adding `reaper:all`, a fresh
clone's typecheck before any build, BlipToaster and NESVJ on scratch copies, and the SDL legs above; no hardware leg),
an Opus final review and follow-ups. Windows, macOS, arm64 and CI are owed. Section 13 lists what acceptance runs beyond
the prototype's kit.

## 11. Risks and open questions

Each is marked with what settles it; the draft report's items are all answered (rows 28 and 29), and what remains are
risks the prototype or a run on the named platform answers. After the prototype each carries **Prototype:**, what it
answered or left owed: R8, R9, R11, R13, R15 and R16 are answered, R10 is still unproved here, and R1 to R7, R12 and R14
are for the platform or the rebase.

**Windows (for the user's Windows agent).**

- **R1. SDL audio drivers on Windows.** SDL2 lists `wasapi`, `directsound`, `winmm` and the `dummy` and `disk` test
  drivers; which one the vcpkg `sdl2:x64-windows-static` build (`build.yml:328`) compiles in and which `SDL_AudioInit`
  picks first is not checked here. SDL initialises COM on the thread that opens a device; the UI thread opens it, as
  PortAudio's WASAPI does, and the two backends never hold a stream at once (the host stops one before it opens the
  other). The SDL leg of `sdl:pipewire` is Linux-only, so the Windows SDL path has no automated proof (a manual one is
  owed). **Prototype:** unanswered. The backend opens its device on the UI thread (COM lives there), and
  `SDL_GetDefaultAudioInfo` needs the vcpkg SDL2 to be 2.24 or later, which the Windows agent checks.
- **R2. MME against WASAPI.** `autoOutputHostApiIndex` falls back to the platform default's host, which on Windows is
  MME (release-readiness section 3); the PortAudio backend's `autoDriver()` now prefers WASAPI there (report item 10,
  E14), which no one has run on Windows: the Windows agent verifies that Auto opens a WASAPI device, that the fallback
  to MME works when WASAPI refuses, and that a saved `driver` still wins. **Prototype:** the fallback is the part that
  does NOT work as this risk assumed: with no `paWinWasapiAutoConvert` a rate other than the mix format fails the open
  and Auto moves to the SDL backend, not to MME (E14).
- **R3. No virtual MIDI ports on WinMM.** `supportsVirtual()` is false there (4.1), so the app has no "RetroPlug In/Out"
  for a DAW to connect to, as today, now said. MIDI inputs are exclusive on Windows: the scan's held edge
  (`scanSuspendsInputs`) is the one lever, and with no audio stop around it the hardware inputs close and reopen under a
  running stream. `portMatch`'s WinMM rows are UNVERIFIED (the prototype strips RtMidi's trailing `" <index>"`, which
  `deps/rtmidi/RtMidi.cpp`'s WinMM `getPortName` appends to every name; a real listing from the Windows agent replaces
  the table's rows, and the `N- ` prefix rule was dropped for want of one). **Prototype:** a refused input is retried
  only on a real apply (the first S3 fix retried it every frame, found and fixed in the whole-prototype review), a
  Reopen closes before it opens, and the scan's held edge is the only lever and now runs under a live stream (E5); two
  identical devices resolve to nothing by design.
- **R4. Vcpkg SDL2 and `/W4 /WX`.** `retroplug-audio-sdl` and `retroplug-audio-portaudio` are our targets and take `/W4
  /WX` (`AGENTS.md`); `retroplug-audio-sdl` includes `SDL.h` (`SDL_MAIN_HANDLED` is the product's, the backend needs
  none) and PortAudio's headers come from a subdirectory build that does not export its include directory (5.4). A
  rotted `#if defined(_WIN32)` half of the moved quieting code is where a first warning lands (it is Linux code, `#if
  defined(__linux__)`, so the Windows half is the `PaUtil_SetDebugPrintFunction` declaration alone). **Prototype:**
  `retroplug-audio`, `-audio-portaudio`, `-audio-sdl`, `-audio-host-test`, `retroplug-midi`, `-midi-facet` and
  `-midi-test` are on `_rp_werror_targets` with `_CRT_SECURE_NO_WARNINGS` (the first midi pass had left the moved RtMidi
  code in targets Windows did not judge); none has met MSVC. The `extern "C"` PortAudio declaration inside an anonymous
  namespace is the line to watch.
- **R5. The output-set swap's grace wait** can be as long as one WinMM bulk SysEx send (about 170 ms) because `send` is
  the critical section the wait outlasts (4.3): a Settings > MIDI change on Windows can stall the UI frame that long.
  Report item 19: built as approved, with a retire list only if a Windows run shows the stall. **Prototype:** the wait
  is the one send in flight (a snapshot wait that yields and then sleeps, not a spin); but `MidiStream::send` runs every
  selected hardware output's send inline on the AUDIO thread, so a SysEx the engine emits to a selected WinMM output
  blocks the audio callback about 170 ms (as `MidiIo` always did), a larger exposure than the UI stall this risk names
  (section 12).

**macOS.**

- **R6. CoreMIDI.** `midiSystemUsable()`'s client creation (a kept keepalive, three attempts, then "no MIDI") moves with
  `systemMidi()`; the Launchpad's copy of the probe goes, so the guard now covers both uses with one client. The abort
  it prevents (RtMidi's `throw()`-declared singleton) is only observable on a macOS run. **Prototype:** owed.
- **R7. The framework SDL.** The build links `SDL2.framework` (`build.yml:375-384`, 2.32.10) and `retroplug-sdl` is a
  bare executable, so `SDL_INIT_AUDIO` runs against the build machine's framework; the release packaging is out of
  scope. **Prototype:** owed.

**Everywhere.**

- **R8. SDL2 runs one audio driver at a time.** The `sdl` backend's device list is real only for the active driver
  (5.1). Switching drivers means `SDL_AudioQuit` and `SDL_AudioInit(name)` while no stream is open; `AudioHost`
  guarantees that by stopping and closing first, and a request that cannot switch (no such driver) is the same `open`
  failure as any other and falls back to `null` with the reason. **Prototype:** answered, after a review finding: the
  first pass's `devices(driver)` switched drivers for every listing and killed a running stream (5.1); now a listing
  answers only for the driver in force and `ensureDriver` refuses a switch while a device is open.
- **R9. Startup ordering** (6.6): the rendezvous now waits on two TS events (the restore and the first audio apply) and
  on a 500 ms bound; a slow UI mount costs up to half a second of silence at launch, as the restore wait does today.
  **Prototype:** answered. The wait is on `restored()` (where the Launchpad provider runs) and the audio apply, bounded
  at 500 ms, and the device opens afterwards at the app's old point (6.6), so a TS that never applies costs the bound
  and starts on the defaults with a printed line.
- **R10. PortAudio's PipeWire host API and `lost()`.** Whether the fork reports a removed PipeWire node as a finished
  stream, and whether SDL's device status flips on a removed device, is not checked in the design; the prototype
  verifies each, and `AudioHost`'s no-pull watchdog (5.2) covers a backend that never reports. **Prototype:** NOT
  verified. A leg could destroy the open sink with `pw-cli destroy` mid-run and grep the muted line; it was not built
  (owed, rather than a possibly flaky kit leg), so `lost()` on a really removed device is unproved for both backends,
  and what is held is the watchdog (its cases, and a device that never pulls).
- **R11. SDL's multichannel layouts** (quad, 5.1, 7.1) against PortAudio's declared layout. The prototype's wide-output
  leg found the difference: SDL 2.30's PipeWire driver accepts the 8-channel request at the API (the host renders 8 and
  `StreamInfo.channels` is 8) but the graph NODE it registers has 2 output ports, where PortAudio's declared-layout
  stream shows 8. WHY is not established here - SDL's own mixing and PipeWire's stream adapter channel-mixing to the
  sink's 2-port layout are both plausible - so what the leg holds is the fact: the node shows 2 ports and only pair 1
  reaches the sink. The SDL backend warns at open when a wide request lands on that driver.
- **R12. A configure-time dependency.** `portaudio`'s `add_subdirectory` moves earlier (root scope, before the
  packages), so its `find_package` / `pkg_check_modules` probes (ALSA, PipeWire, JACK) run for the plugin-only build
  too; today the same probes run, only later. The `PA_*` cache options are set at the same point as before relative to
  the subdirectory. **Prototype:** the kit configures and builds every product in every run with the move in place and
  the plugin's bundles are unchanged (`reaper:params`); no plugin-only configure on a machine without ALSA was run.
- **R13. `audio-portaudio` and ALSA.** The moved quieting code calls `snd_lib_error_set_handler`, which `rtmidi` linked
  implicitly; the target names ALSA itself on Linux, `find_package(ALSA)` and `ALSA::ALSA` (report item 22).
  **Prototype:** answered, as written (`find_package(ALSA REQUIRED)` on Linux).
- **R14. Merge order.** `feature/handheld-perf` edits the audio region this phase deletes (`4b9e39ea8` is `+168/-15` in
  `main.cpp`); its rebase is the user's follow-up, and its `main.cpp` hunk reduces to the muOS launcher flags once
  render-ahead is `AudioHost`'s. `39136feea` and `d64424b95` touch other regions. **Prototype:** owed, after the user's
  rebase; the muOS device run is the proof of render-ahead.
- **R15. Test binary naming.** `retroplug-audio-test` is taken (the per-channel taps, `AGENTS.md`); the new `AudioHost`
  binary is named `retroplug-audio-host-test` (report item 14). **Prototype:** done, and the binary is on
  `run-plugin-tests.mjs`'s list (the fourteenth `test:plugin` binary).
- **R16. The approval record.** `decisions.md` records the approval of sections 1, 2, 3 and 5 as "(user, yes)" and has
  no line for section 4 (config sections, settings, startup); the presentation file carries it among the approved five.
  Report item 17 is left to the user's confirmation; the design carries section 4 as approved. **Prototype:** answered:
  `decisions.md` records the section 4 approval late, as its own line, and the user then approved the design document as
  committed (`793c655fd`, "looks good").

## 12. What it leaves

For `spec/07-remaining-work.md`'s F5b entry, **the per-device MIDI routing follow-up first**:

- **Per-device MIDI routing into the graph** (the user's follow-up): each port its own source and destination, host MIDI
  I/O nodes per port (`rp.host.midi-in` / `-out`), `buildGraph` routing, the menus. The `midi` section's entries are
  objects (`{ name }`) so a routing field adds without a version bump; `MidiStream` merges every input into one ring
  today, so the staged messages carry no port.
- **The `feature/handheld-perf` rebase**: `4b9e39ea8` onto `AudioHost` (its render-ahead is `AudioHost`'s; the branch's
  `main.cpp` hunk reduces to the muOS launcher flags), then the muOS device run that proves render-ahead (xRT, underruns
  and frames per second at the quantum the server locks).
- **MIDI-in frame offsets.** Every message still stages at frame 0 (`main.cpp:847-899`); RtMidi's timestamp is discarded
  and `Message::seq` is never turned into an offset. Up to one block of jitter (10.7 ms at 512 frames, 85 ms at 4096).
- **MIDI clock phase lock, device hotplug** (a PortAudio device list is fixed at `Pa_Initialize`, MIDI ports are read
  when asked), **the PipeWire RT pin** (`SCHED_FIFO` 80 and a core pin applied to PipeWire's shared data-loop thread
  overrides rtkit: the release-readiness suggestion is to tune only on ALSA).
- **`MidiClockSync` into `src/host` with a Catch2 test, and the test scaffolding out of `main()`**
  (`docs/sdl-release-readiness.md` section 5).
- **The manual checks owed**: the real Launchpad on both the claim and the scan; the muOS run of render-ahead; Windows
  (SDL and PortAudio streams, MME and WASAPI, WinMM names against `portMatch`, `/W4 /WX`) and macOS (CoreMIDI,
  CoreAudio, the framework build); a PipeWire-backed SDL stream on a real desktop.
- **What the prototype found, for later.**
  - **The Windows agent's list**, none of it met MSVC or WinMM yet. (1) WASAPI Auto: `PortAudioBackend` sets no
    `paWinWasapiAutoConvert`, so a saved or default 48 kHz on a 44.1 kHz shared-mode mix fails the open and Auto moves to
    the SDL backend, not to MME; a `PaWasapiStreamInfo` in `hostApiSpecificStreamInfo` when the host API is WASAPI is
    cheap and should not wait for the run (E14, R2). (2) The ~170 ms audio-thread send: `MidiStream::send` runs every
    selected hardware output's send inline on the audio callback, so a SysEx the engine emits (a controller app's LED
    traffic) to a selected WinMM output blocks the AUDIO thread that long; pre-existing in `MidiIo`, unchanged, and a
    larger problem than the UI stall R5 names. The fix is a writer thread per hardware output, as the Launchpad's link
    has, not the retire list. (3) `/W4 /WX` on the new targets, first run (R4). (4) WinMM and CoreMIDI names for
    `portMatch`'s table, whose rows are unverified; the WinMM `N- ` rule was dropped for want of a real listing; two
    identical devices resolve to nothing. (5) A MIDI port is exclusive on Windows: a refused input stays refused until a
    real apply. (6) SDL's device opens on the UI thread, and `SDL_GetDefaultAudioInfo` needs SDL2 2.24 or later in the vcpkg
    port. (7) No virtual "RetroPlug In/Out" exists on Windows (E10).
  - **The `rp.midi.` node-type family has two owners**: `core` owns `rp.midi.route` (exempted in the literal scanner by
    `--node-exempt`) and the `midi` package owns every other `rp.midi.*` type. Decide the family's one owner before the
    per-device routing follow-up adds node types.
  - **`lost()` on a really removed device (R10)** is unproved for both backends; the leg is a `pw-cli destroy` of the open
    sink mid-run and a grep of the muted line. A watchdog trip also stays muted with no backoff retry.
  - **SDL's PipeWire node is stereo for a wide request** (R11): the host renders the requested channels, the node shows 2
    output ports, and why (SDL's own mixing, or PipeWire's stream adapter) is not established.
  - **`sdl:pipewire` and the guard are Linux-only and not in CI** (the runners have no PipeWire); `sdl:controller` is run
    alone, the kit skipping it. The ALSA-fallback leg holds only the clean degradation in this container (section 7).
  - **What a backend author copies is the header**: the three rules on `AudioBackend` (a constructor must not touch the
    library, a listing must not disturb a running stream, `open` returns a stream not started) were found by review, and a
    `MIDI` backend's equivalents sit in `MidiBackend.hpp`; spec/06's "Adding a package" carries the manifest wiring.
  - **`FakeMidiBackend::withClientName`** drops `onSend` and `answering`; its inline delivery runs a receiver under the
    fake's mutex, so a receiver that sends synchronously deadlocks. Both are in its header.
  - **Config-section parity with project sections**: a config section's absent key is not written until set, where a
    project section's is filled with `defaults()`; a pack author copying a project section must be told (spec/06).
  - **`n8-bridge` through the CLI's real RtMidi facet** has no test (only the host's fake-backed facet does); owed on a
    real N8.
- **What the phase decided to leave** (the reviews', when it has them): the CLI's `MidiStream` is not wired to the
  Launchpad's reservation (report item 21: left as is) (the CLI's `ProviderHost` keeps no stream,
  `cli/main.cpp:171-219`); `reaper:*` and the plugin take nothing from this phase; the `controller` section and the
  `controlSurface` block now share one registry shape and nothing pairs a config section key with a UI service, so a
  typo is silent (F5a's pairing-check item, one more key).
- **Spec bookkeeping** is done by stage 4: `spec/07`'s F5b entry, F5b out of the open list and the per-device routing
  and rebase notes in, `spec/05`'s two new sections, `spec/06`'s product wiring and tests, and the
  `sdl-release-readiness.md` ticks.

## 13. As built (the prototype)

The prototype ran as six stages on one branch (`proto/f5b-audio-midi`, cut from `feature/modularize`), DeepSeek Flash
implementing and Opus reviewing, with the kit (`.superpowers/verify/tip-verify-par.sh`) as the oracle at every stage and
the user's strict Zoom L6 rule (decision 30) over every harness. The work directory is `.superpowers/f5bpro/`: the stage
briefs, each report and each review. Sections 1 to 12 above are corrected to what was built; this section is the record
the plan is generated from. Every item here is implemented and held by a test or a harness named beside it, except the
ones listed as owed.

### 13.1 The stage commits

| Stage | Prototype (`proto/f5b-audio-midi`) | `feature/modularize` | Subject |
| --- | --- | --- | --- |
| S0 | `925fd40d9` | `605c8b6a3` | merge feature/sdl-standalone: the MidiIo ring fix and the SDL release-readiness list |
| S1 | `7b547cf29` | `f4a6198d2` | feat(midi): the midi package (MidiBackend, MidiStream, RtMidi and fake backends, the CLI facet as a provider) and the Launchpad onto it |
| S2 | `6bb3469ef` | `f98e6e8b7` | feat(audio): the audio packages, AudioHost and AUDIO_BACKENDS |
| S2b | `1932627c6` | `ede472ed7` | refactor(sdl): split main.cpp into the window, input, hooks and audio loop |
| S3 | `6568fe4c6` | `eac8b9674` | feat(settings): config sections, and the audio and MIDI settings as packages |
| S4 | `ec5eff770` | `27f99be3d` | feat(sdl): the SDL-audio leg, the linkage check and the docs |

The left column exists only on the local, UNPUSHED `proto/f5b-audio-midi` branch (`ec5eff770`, the tip the
whole-prototype review read, is on no branch at all); the right column is the history `feature/modularize` carries, one
commit per stage, and it is what the plan's tasks replay. Sections 3 and 10 name the prototype's shas where they were
written from the prototype's run.

The shas are the final ones: four fix passes rewrote the history (non-interactive `git rebase -i` with a written todo,
`fixup` for the files only one stage touches and `exec git commit --amend -F` for the messages, one `edit` stop for S2b's
namespace move), and after the last one every stage tree was extracted, proved byte-equal to its commit (a SHA-256 of
every tracked file against `git cat-file blob`: 3698, 3724, 3729, 3739 and 3743 files for S1, S2, S2b, S3 and S4, no
mismatch), installed on its own (`pnpm install --offline`, the submodules symlinked read-only) and typechecked and tested
alone: `typecheck`, `menu`, `packages` and `hosts` exit 0 at each, with S3's six new test files at S3 and S4. S2b's
compiling is held by a real build of its own tree (Task 4 configured and built `all` warning-free, and ran
`sdl:controller` on the linked binary); `c++ -fsyntax-only` was claimed for it and proves nothing about a translated
unit, so the record does not rest on it. The
stage messages describe the feature, not the review: S1's and S3's were reworded after the whole-prototype review (the
first S1 body was the S1 review's fix list and part of it was false at its own tree). The plan must also know: S2b carries
the namespace move (`retroplug::sdl`, `app.hpp` without the generated headers, `~AppState` out of line); S3's tree keeps
`tools/run-sdl-pipewire.sh`'s exec bit (100755; an earlier S3 dropped it, so a plan task copied from it must not carry a
mode change) and carries `check-doc-links`' rule change and the recorded matrix's three seam cases; the verification kit
and the first guard were `.superpowers` work files, so a plan task that needs the guard creates `tools/sdl-audio-guard/`
(S4: `nosnd.c`, `build-guard.sh`, `guard-env.sh`), and S3 adds `packages/audio/ts/test/audioDraft.test.ts`.

### 13.2 What each review found and each fix pass changed

**S0 (`925fd40d9`).** The review found it exact (section 3). Nothing changed.

**S1, first form (`a9417b4dd`), reviewed with S0.** Critical: the epoch handshake's memory orders allowed a use-after-free
on arm64 (a store-buffering shape no TSan run can see). Important: the retire wait was unbounded under back-to-back sends
(556 ms, and never returning when pinned to one core); the single-sender rule was unchecked (two senders freed a set under
a send); re-picking the same device no longer reconnected it; the midi targets were not under `/W4 /WX`; `check:boundaries`
had a standing warning; the proof gaps (log lines never asserted although three case names claimed them, no delivery-thread
case, no standing TSan leg). About twenty minors (the shim's startup write, `close()` beside a send, the fake on the
public include root, the interface naming RtMidi, two client names, `openVirtual*` not refusing on Windows). Fix pass,
amending S1: `seq_cst` on the four operations, a wait until the epoch CHANGES, a Debug assert plus a case, a re-pick that
reopens (then Diff/Reopen, below), the werror list, `check:boundaries` counting library targets, a log sink with nine
asserted lines, a threaded-delivery case, the `tsanmidi` leg. Each had a RED (the old code run against the new case) or a
stated reason it could not (the memory orders).

**S2, first form (`74314b92a`), reviewed with the S1 fix pass.** The S1 fixes were right except two new findings and one
the fix introduced: a forced re-apply opened a port again before releasing the old handle (WinMM exclusivity: the re-pick
meant to heal an unplugged device lost it), the CLI's virtual ports lost their session names (`n8-bridge`, `n8-sync` and the
loopback probe all published "RetroPlug In/Out"), and the boundary rule's target-name matching treated `-` as a boundary,
making the check vacuous for the audio backends. S2 itself: Critical: the interim Settings > Audio hooks broke TS's
contract so every Apply muted the run; the SDL backend's `devices(driver)` switched the active driver and killed a running
stream when Settings opened; render-ahead set its ring up after the device started pulling (five TSan reports). Important:
the audio threads were no longer tuned at all; the watchdog missed a device that never pulls, used a fixed bound and muted
for good; a stream that opened but failed to start was neither live nor muted; a backend switch with the same driver,
device and rate was a no-op; render-ahead shaped the engine for the wrong block when the run fell to null; the stats were
shared by two threads; proof gaps (no log-line case, a render-ahead identity case whose pulls never wrapped the ring, a
re-rate case that checked the mix). And the split of `main.cpp` had not been done. Fix pass, S1 by autosquash and S2
amended: Diff/Reopen with close-before-open, `withClientName`, the whole-token boundary rule; the old hook shape kept
until S3 (a pure label-to-choice function), the one-driver rule for SDL, the ring and renderer before `start`, per-thread
tuning, the watchdog's clock and bound, a refused start as an open failure, `sameChoice` with the backend, render-ahead
decided before the request, the renderer as the only reporter, and the cases for each; then S2b.

**S2b (`1932627c6`) and S3, first form (`4d30ebbc6`), reviewed with the S2 fix pass.** S2b was a pure move (ready). The S2
fix pass was right except that `--audio-backend` cleared the saved driver and device, so `sdl:pipewire`'s device legs could
not be expressed. S3: Critical: building ANY menu enumerated the sibling audio backends, so PortAudio and SDL audio were
initialised at launch with `null` pinned, and only the `LD_PRELOAD` guard kept the sound card closed (the `strace` the
report offered as proof cannot see a refused open); and `setSection` computed its patch from the in-memory copy, so it
reverted another process's change and overwrote a block a NEWER build had stamped. Important: the MIDI checklist
mis-read every saved entry that was not an exact, opened name; re-resolve happened only when the section's bytes changed;
`portMatch` did not match what RtMidi gives on Windows and its WinMM rows were invented; additive fields were lost (an
entry's extra field, a newer build's field in any contributed section); a muted device had no retry; the audio device
opened inside the rendezvous, before the Launchpad's restore, `midi.open()` and the providers' restore, which the design
text allowed and nothing listed; proof gaps (the smoke's transport leg vacuous for E7, no E12 test, `reaper:params` not
run). Fix pass, folded into S2 (the audio files) and S3: the "More drivers..." step and a pinned run's `[]` hook, the
re-read file, the checklist's mapping, edge re-resolve, verbatim keep, Apply live while muted, the override keeping
driver and device, `ProjectHost.revision()`, `CORE_CONFIG_KEYS` derived; the guard upgraded to LOG every refusal and every
audio-library dlopen so the kit can gate a null-pinned run on an empty log; and S4's `sdl:pipewire`, the linkage check and
the docs.

**The whole prototype (S0 to S4 at `ec5eff770`).** Critical: the fix for "re-resolve when the submenu opens" made the
builder request a re-apply and the apply repaint, so the MIDI service re-applied on every frame whenever a menu was on
screen (60 frames: 60 applies, 120 port listings, `/dev/snd/seq` opened 250 times against 28). Important: the verbatim
keep was lost on a shell-field write in a product that contributes the section (the author's own RED still failed);
`portMatch`'s WinMM rule broke ALSA renumbering for any multi-port interface; the audio device still opened before
`midi.open()` and the providers' restore (and three comments said otherwise); a tracked harness and two tracked docs
depended on an untracked guard file, and the guard loaded only when `LD_PRELOAD` was empty; `sdl:pipewire` never exercised
Auto, its ALSA leg proved less than its text and a skipped SDL leg was silent; several docs described code that was gone.
Fix pass (the last, giving the shas above): `MenuBuilder.onOpen` and edge-triggered re-resolve, `update()` carrying the
model, tiered `portMatch`, record-then-open-at-the-old-point, the tracked guard, the Auto leg and counted skips, the docs,
S2b's namespace, and the reworded messages. Minors fixed with them: the exec bit, a "More drivers..." stall, `readString`
reading a missing key as "undefined", orphaned doc comments and stale names, the linkage check's factory symbols, the SDL
wide-output warning, and the guard's lookup state (passed through `dl_iterate_phdr`'s data, not globals).

### 13.3 Departures from the design as committed (`793c655fd`)

Each is also corrected in place in sections 1 to 12; this is the one list.

**The `midi` package.**

1. `MidiBackend` owns its client name: `clientName()` and `withClientName()` are new, and a virtual-port open takes none,
   so a stream's opened and published ports cannot be named differently and the CLI facet owns one stream per session
   under the session's name (the S2 review's I8). `rtMidiBackend()` and `systemMidi()` are declared in `RtMidiBackend.hpp`,
   so the contract header names no library.
2. `MidiStream` gained `Apply { Diff, Reopen }` on its setters, `openedInputs()` / `openedOutputs()`, `setLogSink`, the
   virtual output carried in the published set, a Debug-only one-caller assert in `send`, a `seq_cst` handshake and a wait
   until the epoch changes that yields then sleeps. Reason: the S1 and S2 reviews (UAF on arm64; an unbounded wait; WinMM
   exclusivity).
3. `retroplug-host` names `midi` in `PROVIDERS` (the design named the CLI only): the headless host mounts the facet over a
   fake backend, which is why the E11 case runs there.
4. `retroplug-midi` links no node SDK (the facet does), the fake is test support behind an INTERFACE target
   (`retroplug-midi-test-support`) and not on the library's include root, and the midi and audio targets are on
   `_rp_werror_targets`. `check:boundaries` learned a dependency's own library targets, with `-` part of a name.
5. `portMatch` is TIERS (exact; ALSA's `<client>:<port>` stripped; RtMidi's trailing index stripped), each of which must
   find exactly one port, and the `N- ` rule is dropped. Reason: one strip cannot serve both rules (6.3). The Launchpad's
   `rematchPorts` shares `resolvePort`.

**The `audio` packages.**

6. `packages/audio` declares no `node-sdk` (the design listed it), because `AudioHost` never names the Engine.
7. The contract grew: `AudioStream::start(std::string& error)`, `AudioBackend::driverLabel()`, `devicesListableFor()`;
   `AudioHost` gained `backendDrivers()`, `live()`, `setLogSink`, `RunOverrides` and `applyRunOverrides()`. `describe()`
   enumerates the backend IN FORCE only (the design said every backend), because enumerating a backend is its library's
   first use (the L6 rule), and a sibling is asked for on an explicit "More drivers..." step, once per visit, not at all
   under a `--audio-backend` pin.
8. `AudioClient::prepare` hands the engine back to the audio thread at its end; the ring, its counters and the renderer
   are set up before `start`; a refused start is an open failure; `sameChoice` compares the backend; render-ahead is
   decided before the request. Reasons: the S2 review's C3, I3, I4 and I5.
9. The watchdog's clock starts at `start()` and is stamped at pull exit, its bound is max(250 ms, 4 x the longer of the
   render block and the device quantum), a trip stays muted until an apply with Apply live while muted as the retry, and
   `tuneAudioThread` runs per thread and only on a real device's thread. Reasons: the S2 review's I1 and I2.
10. SDL: a stale device warns and falls back like PortAudio's (not a mute), `blockFrames 0` asks for 512, `dummy` and `disk`
    are hidden, a driver switch is refused while a device is open, and a wide request on its PipeWire driver warns that the
    node shows 2 ports. PortAudio's driver ids come from a `PaHostApiTypeId` table, not display names.
11. `AUDIO_BACKENDS` has six manifest-level fixtures and four product-level cases (not four refusals and one implied), and
    `tools/check-audio-linkage.sh` is the 5.7(4) check, a standing kit leg.
12. `main.cpp`'s split is its own stage S2b, in the namespace `retroplug::sdl`; `MidiClockSync` is in `app.hpp`;
    `restoreControlSurface` is `waitForUiStartup`; `bootControlPlane` and `readReady` are `main.cpp`'s.

**Config, settings and startup.**

13. Six hooks (`__rp_audioDescribe` with `overrides`, `__rp_audioBackendDrivers`, `__rp_audioApply`, `__rp_audioStatus`,
    `__rp_midiDescribe`, `__rp_midiApply(sel, mode)`) where the design had five; `AudioDeviceHost.backendDrivers`,
    `MidiDeviceHost.apply(sel, mode?)` and `live` in the status; both groups optional on `UiHost`.
14. The Driver row lists the backend in force plus a "More drivers..." step, where the design listed every backend from one
    description (the S3 review's C1).
15. `ConfigModel { core, sections, raw, written }`: the verbatim-keep rule covers a contributed block this session did not
    set and survives a shell-field write; an absent key is not invented; `setSection` computes over the re-read file;
    `CORE_CONFIG_KEYS` is derived from the schema; `ProjectHost.revision()` is a new contract member;
    `MenuBuilder.onOpen` is a new member of the UI contract; `RP_MENU_MATRIX_UPDATE=<case>[,...]` re-records the seam
    cases.
16. The MIDI service re-resolves on EDGES and never on a render; its first apply (and the audio service's) lands on the
    first poll; the checklist's semantics are as 6.5 states; a reserved port shows "(control surface)".
17. `--audio-backend` replaces only the backend and keeps the saved driver and device when the section names the same
    backend; `--block-size` and `--render-ahead` override as the user decided, so a muOS `--block-size 4096` always wins.
18. Startup: the audio apply RECORDS the request in the rendezvous and the device opens at the app's old point, after
    `midi.open()` and the providers' restore, which the design text did not say (it said native opens audio after TS's
    apply). No new edge.
19. The C++ label-to-choice helper (`AudioChoice.hpp`) the first S2 pass kept was deleted with the hooks it served.

**Harnesses, kit and process.**

20. `sdl:smoke` exports no `SDL_AUDIODRIVER` (null is pinned), gained the E7, E12, startup-order and menu legs, and
    `RETROPLUG_SDL_TEST_MENU` is a new hook; `RETROPLUG_SDL_TEST_TRANSPORT` no longer sets a tempo.
21. The device guard is tracked (`tools/sdl-audio-guard/`) and all three SDL harnesses AND `tools/reaper-env.sh` source
    it; the kit gained the legs `tsanmidi`, `tsanaudio`, `audiolink`, `pipewire` and `nosnd`. `sdl:pipewire` gained an Auto leg, an SDL-audio mode with
    a live-menu leg and counted skips; its ALSA-fallback leg holds the clean degradation, not an ALSA open.
22. `check-doc-links` reports a dead link inside a `*-plan.md` record as a warning. The readiness ticks name the stage, not
    a hash.
23. Six stages, not five; sdl:pipewire ran at S4, not S2.

### 13.4 Verification

The kit (`.superpowers/verify/tip-verify-par.sh`, parallel, `TV_SKIP_SDLCTL=1`, run on the committed tree; the baseline at
`793c655fd` ran with `TV_SKIP_SDL=1` as well, per the L6 rule) exits 0 on every leg with no retry. Baseline against final
(`compare.sh` reports MISMATCH, and every line it lists is F5b's):

| Leg | Baseline (`793c655fd`) | Final (the S4 tip) | Why |
| --- | --- | --- | --- |
| `test` (`pnpm test`) | 2425 cases, 231 files | 2484, 237 | S3's config, midi and audio files and the fix passes' cases |
| `native`, `nativeci` | 629 cases, 172 files | 634, 173 | S1's E11 and facet cases, S3's project-section cases |
| `plugin` | 13 binaries | 14 | `retroplug-audio-host-test` (23 cases after the follow-up) |
| `retroplug-midi-test` | 24 | 48 | S1 and its fix passes, plus the follow-up's renamed-session case |
| `retroplug-engine-graph-test` | 337 cases | 338 | the re-rate equality case |
| `retroplug-launchpad-test` | 53 | 53 | held, over the fake |
| `parity` | exit 0, 250 | exit 0, 250 | nothing moved |
| `sdl` | skipped (L6) | exit 0 | S2 pinned `null` |
| `tsanaudio`, `tsanmidi` | absent | exit 0 | the new TSan legs: `tools/run-sanitizer.sh` builds and runs `retroplug-audio-host-test`, `retroplug-midi-test` and `retroplug-launchpad-test` under ThreadSanitizer |
| `audiolink`, `pipewire`, `nosnd` | absent | exit 0 | S4's legs: `pnpm check:audio-linkage` (tracked, the same script the kit's `audiolink` leg runs) and `pipewire` 27 passed, 0 failed, 0 skipped after the follow-up's stale-driver leg |
| `docs` | 101 files clean | 102 clean, 1 warning | one new doc; a plan record's dead link is a warning |
| `typecheck` | no audio, midi | audio and midi Done | S2, S1 |
| `boundaries` | 0 warnings | 0 warnings | |

Images and pack bytes are identical to the baseline: `imagesha` (the box and device-check ELFs) and `packbytes` (the
bliptoaster device pack's 64-bit and 32-bit blobs) match, so nothing portable moved; `kitbase` (39 banks) and
`kitedges`, `renderbase`, `renderchunks`, `rendertests`, `renderui`, `devcheck`, `devbox` and `devmode` (96 cases) exit
0 and re-recorded nothing. The binary sizes and bundle sizes moved by the new code and are recorded, never gated.
`reaper:params` at the tip, under the guard: VST3 2725 before and after, CLAP 644 before and after, both PASS (32
refusals of jackd's ALSA control-node probe each, none of a PCM node). `sdl:controller` alone (the kit skips it) is `SDL
controller OK` with five legs and an EMPTY `NOSND_LOG`. The `nosnd` leg: no `/dev/snd` refusal and no audio-library
dlopen in the null-pinned legs. `sdl:pipewire`: 25 passed, and the guard refused 2788 card paths (PortAudio's
control-node probe) and logged 299 audio library dlopens, no PCM node. The startup `strace` (null-pinned, guarded):
`/dev/snd/seq` opened 22 times (28 at S3, 250 before the last fix), the only `/dev/snd` path attempted, the guard log
empty, and the log reads `MIDI: virtual ports` before `audio:` before `running`.

### 13.5 The edges as built

| Edge | As built |
| --- | --- |
| E1 | `audio.json` and `midi.json` are ignored (the S1 shim read `midi.json` until S3 deleted it); the three harnesses were rewritten |
| E2 | MIDI is per project; a fresh project has none; names are matched by `resolvePort`'s tiers, and an entry that matches nothing is "(missing)" |
| E3 | multi-select lists, entries `{ name }` keeping unknown fields; the default stays None, `allInputs` the explicit choice |
| E4 | the Driver row names the backend; SDL's drivers appear after "More drivers..."; PortAudio's log line is byte-identical |
| E5 | no audio stop around a MIDI change or a claim; leg 2 of `sdl:controller` now asserts the claim precedes `] running ` and the claimed port is never opened by the stream |
| E6 | "Muted: <reason>" for a failed open, a refused start, a lost device and a watchdog trip; Apply stays live while muted; a trip stays muted until an apply |
| E7 | the clock tempo is the project's `midi.clockBpm`; the smoke holds it with a tempo the hook does not set |
| E8 | `config.json` gains `audio`; `controlSurface`'s bytes are unchanged; `schemaVersion` stays 3 |
| E9 | a standalone's `midi` block survives the plugin verbatim, and so does a newer build's additive field in any block |
| E10 | on Windows no virtual port is attempted and no false line is printed (unverified off this box) |
| E11 | `midiOpen(client, "")` opens every hardware input again, held by a `test:native` case run under the port filter |
| E12 | `--block-size`, `--render-ahead` and `--audio-backend` override the saved choice for the run; the muOS launcher's block always wins, and the Settings rows say "This run: ..." |
| E13 | startup builds cores at the default rate and re-rates once; the equality case passes on the real engine, so the rendezvous stayed |
| E14 | PortAudio's Auto prefers WASAPI on Windows; without `paWinWasapiAutoConvert` a mismatched rate moves Auto to SDL, not MME (unverified off this box) |

No edge was added: the startup order the first S3 pass changed was restored rather than listed.

### 13.6 What acceptance must run beyond the kit

The kit proves the tree green on this box. Acceptance (serial, with fixtures) adds:

- **A fresh recursive clone, in CI's order** (`pnpm install`, the nested `pnpm install` in `deps/dpf.js/deps/lv_binding_js`,
  typecheck before any build, then the configure): the stage trees were proved alone, never a whole clone.
- **`reaper:all`** and the two-instance legs (`reaper:two-instances`, `reaper:two-instances-load`): the prototype ran only
  `reaper:params`, in both formats. The plugin's bundles did change (the config serializer, the generated registries).
- **`pnpm test:native` whole, with the fixtures and `RP_FAIL_ON_SKIP=1`**, parity and the render baseline included (the
  kit's `nativeci` leg is the CI picture, 144 skips).
- **`test:plugin` (14 binaries), `test:ui`, `test:n8`, `device:check` and `device:box`**, run serially (the kit ran them in
  parallel).
- **`sdl:smoke`, `sdl:controller` and `sdl:pipewire` under the guard**, the controller alone and the pipewire run with its
  skip count read.
- **BlipToaster and NESVJ** on scratch copies (the consumers of the CLI, which now mounts `midi` and loses
  `RETROPLUG_N8_BRIDGE`).
- **The TSan and ASan builds** of the new test binaries (`tsanaudio`, `tsanmidi`) beside the existing legs.

Owed, manual and not provable here: the real Launchpad on both the claim and the scan; `n8-bridge` on a real N8 through
the CLI's facet; Windows (SDL and PortAudio streams, WASAPI Auto and its AutoConvert fix, MME, WinMM names against
`portMatch`, `/W4 /WX`, the 170 ms audio-thread send); macOS (CoreMIDI, CoreAudio, the framework build); `lost()` on a
really removed device (R10); and the muOS render-ahead run, after `feature/handheld-perf` rebases onto this phase.

## 14. Execution

F5b was executed on `feature/modularize` over the design commit `793c655fd`, as the plan commit `3c860286e`
([modular-graph-phase-f5b-plan.md](modular-graph-phase-f5b-plan.md), a record; 29,667 lines, regenerated once so that
every Reaper leg is guarded and the guard is built and `test -s`-checked before each preload) and the six task commits
after it, one per stage, each a replay of its prototype commit rather than a rewrite:

1. `fix(f5b)`-free stage 0: `feat(sdl)`'s merge, `605c8b6a3` (the merge of `feature/sdl-standalone`, second parent
   `d51c4a4ef`; the MidiIo ring fix, its test and the release-readiness list).
2. `feat(midi): the midi package ... and the Launchpad onto it`, `f4a6198d2`.
3. `feat(audio): the audio packages, AudioHost and AUDIO_BACKENDS`, `f98e6e8b7`.
4. `refactor(sdl): split main.cpp into the window, input, hooks and audio loop`, `ede472ed7`.
5. `feat(settings): config sections, and the audio and MIDI settings as packages`, `eac8b9674`.
6. `feat(sdl): the SDL-audio leg, the linkage check and the docs`, `27f99be3d`.

Each task recorded SPEC OK and a clean tree (the controller's log, `/tmp/f5bexec/results.md`, and each task's report,
`/tmp/f5bexec/t<N>-report.md`). **The implementers' findings** were all record-level, and every one is the follow-up's
to fix or the plan's to stop saying:

- **Task 1**: `check:docs` before staging fails on the untracked target (as F5a's did); the first parent and the tree
  differ only by the plan file.
- **Task 2**: stale post-fix-pass counts in the plan's text (`retroplug-midi-test` is 47 there, right); one step's
  `check:boundaries` warning was stale (0 is the fixed state); `check:docs` before `git add` is an ordering (an untracked
  file is not scanned); a `git grep` matched history documents where it was expected to match none; `PluginDSP.cpp:177`'s
  format-truncation warning and a moodycamel `-Wtsan` note are pre-existing.
- **Task 3**: a step order that leaves the tree between two steps unconfigurable (configured after the later one); stale
  counts (`audio-host` 21, not 20); the plan's "guard: 0 bytes" line is unprintable as written, because the log file is
  created only by a refusal.
- **Task 4**: the "1680 of 1683 verbatim" move count is not what the tree shows (1676 verbatim plus 7 named reshaped
  lines - fixed in 5.5 and 13.3); `test:ui` prints no skip summary, so the plan's "seven skips" was unbacked; no Task 4
  step runs `pnpm sdl:smoke` (the split binary was proved by `sdl:controller` instead).
- **Task 5**: three step orderings and two stale quotes; `audio-host` is 20 there (21 minus the case that was removed);
  `pnpm test` 237 files and 2,484 cases is the tip's count; assertion totals are not reproducible even where case counts
  are exact.
- **Task 6**: `sdl:pipewire` 25 passed (the Auto leg, the SDL stereo node, the ALSA leg muted with a status),
  `sdl:smoke` and `sdl:controller` guarded with empty logs, the linkage check clean, `reaper:params` 2725 / 644,
  BlipToaster 57/57, NESVJ 19/19, the device images and pack bytes identical; `guard-env.sh`'s Expected line was
  unprintable as written (the log exists only on a refusal), and its step 1 RED greps an empty log.

**The acceptance run was STOPPED**, at the end of the serial kit's third stage and before any PipeWire leg, when the
final review found what `sdl:pipewire`'s private wireplumber was doing (I7): its copied config dropped only the bluetooth
context, so the daemon also ran its ALSA, v4l2 and libcamera device monitors - opening the NES capture card on
`/dev/video0` and `/dev/video1`, and mirroring the MIDI interfaces' sequencer ports - and a bare `pnpm reaper:*` was
unguarded. Acceptance re-ran from the start over this section's follow-up, at `3220cfd32`: **PASS WITH FINDINGS**. A fresh
clone installed, typechecked (leaving no `build/`) and built in CI's order; the serial kit's 49 lines are every one exit
0, the guarded `sdl`, `sdlctl`, `nosnd` and `pipewire` legs among them, plus the parallel kit's `tsanaudio`, `tsanmidi`
and `audiolink`; strict parity held bit for bit (30 files, 250 cases, none skipped); the kit and render baselines,
`render-chunks`, `render-cli`, the registration golden and all fourteen `test:plugin` binaries held; both device images
are byte-identical to `793c655fd`'s and the pack bytes are the baseline's; `reaper:all`, both two-instance legs on VST3
and CLAP with `RP_LOAD_MODE=dialog` and the editor legs passed; BlipToaster 57/57 and NESVJ 19/19 on scratch copies.
Every SDL and Reaper run was guarded: 33 Reaper job logs held 1056 refusals, every one a card CONTROL node, and none a
card PCM, video, serial, HID or USB node; the null-pinned SDL logs were empty; `sdl:pipewire` passed 27 with `0 card
pcm, 0 video/serial`. Its findings: the pre-existing `PluginDSP.cpp:177` `-Wformat-truncation` on a from-scratch build
(owed, spec/07); `check:docs` at 103 files with the one plan-record warning; the Reaper harness re-points `NOSND_LOG`
into each job's own log directory, so the plan's named log paths are never written (the counts are printed and enforced
per job instead); stale figures the follow-up moved (`sdl:pipewire` 27, `retroplug-midi-test` 49 cases,
`retroplug-audio-host-test` 24, `render-cli` 6); and `run-reaper-two-instances-load.sh` on CLAP skipping once on a
missed dialog click (its heartbeat advancing, so no freeze), then passing on an identical re-run.

**The hardware incident, stated plainly.** The prototype's manual `sdl:pipewire` run left an orphaned private
`pipewire` / `wireplumber` / `dbus-daemon` behind (scratch directory `/tmp/f5bpro/pwman2`), and it kept running for about
5.5 hours: its own log shows the libcamera monitor loading. With the old harness config, every `sdl:pipewire` run that
started its session manager (the prototype's S4 stage and its fix pass, that orphan at its start, and Task 6) ran the
v4l2 monitor, which opens `/dev/video0` and `/dev/video1` to query them: the follow-up's live comparison under the new
guard logged that daemon's `Cannot open '/dev/video0'`. So the NES capture card was very likely opened, queried and
closed during those runs (a monitor probes; it does not stream). Nothing played to any card. The guard's wider scope,
the monitors' disablement and the harnesses' self-test exist to prevent it; the controller killed the orphan while
stopping acceptance.

**The final review** (`/tmp/f5bexec/final-review.md`, read-only over `793c655fd..27f99be3d`, five parallel sub-reviews)
found no Critical and eleven Important, its verdict "ready to call F5b complete with fixes". Its shape finding was that
the architecture holds: the two contracts, `MidiStream`'s handshake, `AudioHost`'s one sequence with the null fallback,
product-named backends, config sections on F5a's rules, the edge-triggered MIDI re-resolve, the startup order, the
menu matrix's three cases, `config.json`'s byte-identical round trip in both products, and `reaper:params`' 2725 / 644.
Its Important items were behaviour holes at edges the tests did not reach (a renamed CLI session's "all", the retry tap,
a long pull, a core MIDI name, a saved driver gone), two proofs that held less than they said (the re-rate case's NES;
the guard's load and its two unguarded harness families), a build break on the handheld's cross-build path, and a record
that carried the prototype's numbers.

**The follow-up** is five commits on top of the six, each with its RED in `/tmp/f5bexec/fuA-report.md`:

- `fix(f5b): the hardware guard covers every device, proves it is loaded, and guards Reaper` (`15f65bfa8`) is I7:
  `nosnd.c` refuses every `/dev/video*`, `/dev/media*`, `/dev/v4l-subdev*`, `/dev/ttyACM*`, `/dev/ttyUSB*`,
  `/dev/ttyDbgProbe*`, `/dev/serial/*`, `/dev/hidraw*` and `/dev/bus/usb/*` node beside every `/dev/snd` one but `seq`
  and `timer`; `guard-env.sh` self-tests after preloading (a probe open of `/dev/snd/nosnd-selftest` must be refused and
  logged, else the harness exits non-zero, which is what catches an unloadable or stale preload); `tools/reaper-env.sh`
  sources the guard, so every bare `pnpm reaper:*` is guarded too, and its teardown reports the log's counts and fails
  on a refused card playback, video or serial node; `sdl:pipewire`'s wireplumber config disables the ALSA, v4l2 and
  libcamera monitors and the script checks both the copied config and the daemon's log. RED: nine of ten hardware-shaped
  paths reached the syscall under the old guard.
- `fix(f5b): the final review's important findings` (`61462c6ed`) is I1-I6 and I8-I10: the port filter judges by the
  backend's FAMILY, so a renamed session's "all devices" no longer opens RetroPlug's own ports; the retry tap and Rescan
  read the machine again after the apply; the watchdog tracks a pull in flight, restamps the UI's own absence, and mutes
  in bounded time instead of freezing on a callback that will not stop; a saved driver this PortAudio lacks falls back to
  Auto with a warning and the status names the device's own host API; every WASAPI stream carries
  `paWinWasapiAutoConvert`; the re-rate equality case renders per system and holds the NES's own lanes; `SDL_GetDefaultAudioInfo`
  is behind `SDL_VERSION_ATLEAST(2, 24, 0)`; the WinMM index tier needs a WinMM-shaped listing; and the `audio` section
  keeps any non-empty backend id, with spec/06's rules for a backend author and a MIDI-using pack.
- `docs(f5b): the record says what is` (`374998d56`) is I11 and the implementers' record-level findings: the S2b count,
  the removed `-fsyntax-only` claim, 4.7's cases, the status line, section 13's two columns (the prototype's shas live
  only on the unpushed `proto/f5b-audio-midi`), AGENTS.md's provider list, the fourteen binary counts, spec/07's dropped
  section-12 items, and two proofs moved out of the untracked kit and into tracked scripts (`pnpm check:audio-linkage`
  and `tools/run-sanitizer.sh`'s audio leg).
- `fix(f5b): the final review's minors` (`bc97b50ac`) closes the minors the review listed, among them the output diff's
  exclusive-device retry, the fake backends' races, a rename carrying a selected output, the obtained device quantum, the
  ALSA quietening in the midi package, `__proto__` written as an own property, the transport repaint gated on
  `menuOpen`, the guard's non-canonical paths and `O_DIRECTORY` mode read, `sdl:smoke` off Linux, the null-pin gate CI
  can hold, the linkage check's gaps, and the stale comments and counts.
- `docs(f5b): record the acceptance run, the review and the follow-up` is this section, the status line and spec/07's
  F5b entry.

**What F5b leaves**, the final review's "what the next phase should know first":

- **Per-device MIDI routing** (the user's next want, and the reason a section's entries are objects): each port its own
  source and destination, `rp.host.midi-in` / `-out` per port (so `rp.engine.host-io` needs a new version), `buildGraph`
  routing keyed by SAVED name rather than resolved port to stay machine-independent, the menus, and a decision on the
  `rp.midi.*` family's owner before the first new type (`core` owns `rp.midi.route` behind `--node-exempt`, the `midi`
  package every other one). A per-port node needs a node target in the midi package, whose only manifest item is a
  provider today, which cannot also be an entry.
- **The `feature/handheld-perf` rebase**: `d64424b95` applies clean; `39136feea` conflicts in earlier phases' files
  (`Engine.hpp`, `SnapshotRegistry.cpp`, `EngineRpcService.hpp`, `backend.ts`, `realBackend.ts`); `4b9e39ea8` conflicts
  in `sdl/main.cpp`, whose hunk should be dropped entirely (`AudioHost` has the ring, the stats, the 85/80 tuning and
  both switches) and in `mux_launch.sh`, of which only the flags are kept. The device run must confirm the
  `PortAudio [PipeWire]` line, because Auto now falls to SDL rather than to the pump (I5's flag is what keeps WASAPI
  rather than MME), and the cross-build needs the SDL 2.24 guard (I8) before it compiles at all.
- **Downloadable packs** can reach none of this: `AUDIO_BACKENDS`, the generated backend table, `CONFIG_SECTIONS` and the
  TS backend list are build-time data, `setSection` throws for an uncontributed key, and there is no MIDI backend list.
- **The Windows and macOS hand-off**: WinMM's exclusive ports and its `portMatch` names, the ~170 ms send inline on the
  audio thread (a writer thread per hardware output is the fix), a `MidiPort` destructor that cannot wait for an
  in-flight WinMM callback; WASAPI's AutoConvert (set now) and Auto's landing; `/W4 /WX` on the new targets; CoreMIDI
  names, CoreAudio, the framework build and `sdl:smoke`'s legs; `lost()` on a really removed device; and the muOS
  render-ahead run.

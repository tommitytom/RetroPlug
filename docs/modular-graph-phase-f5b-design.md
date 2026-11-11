# Phase F5b: the SDL standalone's audio and MIDI as packages

**Date:** 2026-11-11 · **Branch:** `feature/modularize` **Status:** designed (this commit). The design was written over
`f0f56c3ff` (the F5a tip). The user's decisions are the `### F5` entries of `.superpowers/f-split/decisions.md` (the
2026-11-11 ones); the user approved the design section by section as presented (packages and product wiring; the audio
interface and `AudioHost`; the MIDI interface, `MidiStream` and its users; config sections, settings and startup; build,
proof, edges and process), which this document writes out with the verification the presentation did not have. Where the
code did not match what an approved section assumed, or a section left a real choice open, the draft's report
(`.superpowers/f5bpro/draft-report.md`, items 1 to 22) put it to the user and the controller; every answer (the last
`### F5` entry of `decisions.md`, "F5b draft-report items") is folded into the text and rows 28 and 29 of the decisions
table, and the text cites an answer as "report item N".

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

**Scope (one phase, five stages, by mechanism).** The branch merge (S0, section 3); the `midi` package, the Launchpad
pack onto it and the CLI's facet as a provider (S1, section 4); the `audio` packages, `AudioHost`, `AUDIO_BACKENDS` and
the `main.cpp` split (S2, section 5); the TypeScript half: config sections, the `controlSurface` move, the `audio` and
`midi` sections, rows and startup (S3, section 6); the SDL-audio leg of `sdl:pipewire` and the docs (S4, section 7). It
runs as one prototype in five stages (section 10).

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
| 11 | Audio interface | `AudioBackend` / `AudioStream`, a saved choice is `(backend, driver, device)` by name and never a library's numeric id; Auto = the first backend in the product's `AUDIO_BACKENDS` order whose auto driver opens, PortAudio's auto keeping today's PipeWire preference; SDL2 runs one audio driver at a time, so its backend lists real devices only for the active driver and offers "Default" for the others (stated, not flipped under a running stream) | A numeric driver id; flipping drivers to enumerate |
| 12 | `AudioHost` | Lives in `retroplug-audio`, never names the Engine; owns open / start / stop / reconfigure (once), the fallback to `null` with a status TS shows, a lost device as the same fallback, render-ahead from `4b9e39ea8`, `RETROPLUG_SDL_AUDIO_STATS`, and keeps today's log line form | Each backend owns its own loop |
| 13 | Render-ahead | A backend-neutral feature of `AudioHost` (`renderAheadFrames > 0`: a render thread fills a ring in `blockFrames` blocks, the stream opens with `blockFrames 0`, the pull only copies; Linux RT priorities callback 85 above renderer 80, core pinning); set by `--render-ahead` / `RETROPLUG_SDL_RENDER_AHEAD`, not a setting | A setting; merge the branch as is |
| 14 | MIDI interface | `MidiBackend` / `MidiPort` in `rp::midi`, one exception type for a refused open, `supportsVirtual()` false on Windows (said, not found out by a catch), the port filters once, `systemMidi()` (RtMidi behind the CoreMIDI guard), `FakeMidiBackend` | The Launchpad's opener seam grown in place |
| 15 | `MidiStream` | Today's `MidiIo` over a `MidiBackend&`: exact names (TS matches), `setInputs(names, all)` / `setOutputs(names)`, the reservation and the scan suspension over lists, many producers serialised by a mutex into the ring with one consumer, a drop count, `send` to the virtual output and every selected hardware output | Per-port rings |
| 16 | No audio stop around a reopen | Inputs need nothing (destroying a port cancels its callback; the audio thread only reads the ring); outputs are an immutable set the audio thread reads through one pointer, a change publishes the new set, waits out the block in flight (at most one block, none when audio is not running), then frees the old ports. Settings > MIDI, a project switch and the Launchpad's reservation stop costing an audio stop and restart. MIDI-in frame offsets stay 0 | A pause handshake with the callback |
| 17 | Config sections | `definePackage({ configSections })` with F5a's section shape (`key`, `schema`, `defaults`, optional `version` + `migrations`, `merge`, refuse-newer per section); a section is a non-core ROOT key of `config.json` (core keys stay the shell's: `schemaVersion`, the bindings, `defaultZoom`, `sramAutoSave`), a key with no contributor is kept verbatim, writes keep today's re-read; a generic `config` host group (`section(key)` / `setSection(key, patch)`) replaces `ConfigHost.controlSurface`; the Launchpad's `controlSurface` is its section under the same root key, so existing bytes do not move | Per-package files |
| 18 | The `audio` section | `{ backend: "auto" \| "portaudio" \| "sdl" \| "null", driver: "", device: "", sampleRate: 48000, blockSize: 512, outChannels: 2 }`, today's defaults; render-ahead stays a flag, not a setting, and `--block-size` / `--render-ahead` override the saved choice for that run (report item 8) | Backend-specific fields |
| 19 | The `midi` section | `{ inputs: [{ name }], allInputs: false, outputs: [{ name }], clockBpm: 120 }`, key `midi`, distinct from the shell's `midiRouting`; a fresh project has no ports | A global default selection |
| 20 | Name matching | `midi/ts/portMatch.ts`, pure: exact first, then the name with the volatile parts stripped (ALSA's `NN:NN` numbers, the Windows `N- ` index prefix, spacing), which must find exactly one port; otherwise the entry stays saved, shows "<name> (missing)" and opens nothing; tested against a table of real ALSA / WinMM / CoreMIDI names | Match in native |
| 21 | Reaching native | `core/host.ts` gains `audioDevice` (`describe`, `apply`, `status`) and `midiDevice` (`describe`, `apply`, the transport and clock calls); the shell's UI host offers them only where the SDL hooks exist, so the plugin and the DPF standalone hide the rows as today; two hooks replace `__rp_getAudioConfig` / `__rp_setAudioConfig` (`audio.describe()`, `audio.apply(cfg)`) and two replace the MIDI trio | Keep the eight globals |
| 22 | Menus | Settings > Audio: one Driver row ("Auto (PortAudio: PipeWire)", "PortAudio: PipeWire", "SDL: pipewire", ...), Output Device, Rate, Block, Out Channels, Apply (the draft model stays), a status row when muted. Settings > MIDI ("saved with this project"): an inputs checklist plus "All devices", an outputs checklist, the missing entries, applied at once and dirtying the project. The MIDI Clock submenu moves into `midi`, its tempo saved in the project | Keep the rows in the shell |
| 23 | Startup | The rendezvous widens: native opens audio after TS's first `audioDevice.apply` from the `audio` section, or with defaults (Auto) at the timeout; the virtual MIDI ports open at once, the hardware ports when a project's section is applied; a project switch re-applies with no audio stop (cores are built at the default rate and re-rated once at `prepare`, report item 1; `sdl:controller` is rewritten for it, report item 15) | Keep opening audio in `setupSdl` |
| 24 | Product lists | `desktop` gains `audio`, `audio-portaudio`, `audio-sdl`, `midi` (`midi` before `launchpad`, the audio packages after `core`); `plugin` lists none: a project's `midi` block passes through it verbatim | The plugin lists them |
| 25 | The shell loses | `MidiIo.*`, `MidiRpcService.*`, `registerMidiRpc`; `deps.cmake`'s PortAudio and RtMidi wiring; `retroplug-sdl`'s direct `portaudio` / `rtmidi` links; `userConfig.ts`'s `controlSurface` schema and `ConfigHost.controlSurface`. `retroplug-sdl` keeps `renderAudioBlock`, `MidiClockSync` and the engine handoff, and `main.cpp` splits into `window`, `input`, `hooks`, `audioLoop` and `main` | Leave `main.cpp` whole |
| 26 | Build and proof | `AUDIO_BACKENDS` joins `retroplug_product` with four configure refusals; `portaudio` stays added at root scope (moved out of `deps.cmake`) and is linked only by `retroplug-audio-portaudio`; `retroplug-audio-sdl` links the SDL2 the root already requires; RtMidi is linked only by `retroplug-midi`; `retroplug-sdl` names both real backends so CI's `all` compiles both on all four platforms; `--audio-backend <id>` / `RETROPLUG_SDL_AUDIO_BACKEND` overrides the saved choice for one run | |
| 27 | Process | An Opus-reviewed prototype `proto/f5b-audio-midi` in five stages S0 to S4; then the plan (DeepSeek), per-task implementers (Qwen, each task's tree held to the prototype's), acceptance, the Opus final review, a follow-up. Owed, manual: the real Launchpad, the muOS device run of render-ahead after the rebase, Windows and macOS | |

| 28 | The draft-report decisions that are the USER's | Item 1: keep the approved startup (audio opens after TS's apply, cores built at the default rate then re-rated once) plus an engine-graph case holding a re-rate to 44.1 kHz bit-equal to a fresh build for a GB and a NES core; move the rendezvous before the autoload only if that case fails. Item 6: honour the CLI contract, `midiOpen("")` (and `n8-bridge` with no `--midi-in`) opens EVERY hardware input again, with a `test:native` case (a CLI edge). Item 8: `--block-size` and `--render-ahead` override the saved choice for that run, like `--audio-backend`. Item 10: PortAudio's Auto prefers WASAPI on Windows (an edge; the Windows agent verifies) | |
| 29 | The draft-report decisions that are the CONTROLLER's | 2: reconfigure is stop, release, open, prepare(`info()`), start (adopts the obtained rate). 3: ids in `AUDIO_BACKENDS` plus an `"id"` on the manifest item. 4: `retroplug-midi` / `retroplug-audio` stay out of the manifest `native` list, linked by name (the native boundary check must accept it). 5: the Launchpad holds its own RtMidi backend instance under `RetroPlug Launchpad`. 7: `RETROPLUG_N8_BRIDGE` deleted. 9: `AudioHost` adds a no-pull watchdog. 11: the shell keeps the Settings containers; placements `settings.audio` and `instance.clock`. 12: re-resolve at project load and on opening Settings > MIDI, plus a Rescan row. 13: `sdl:smoke` pins `null` and drops the stale `SDL_AUDIODRIVER` export. 14: the binary is `retroplug-audio-host-test`. 15: `sdl:controller` rewritten as section 9 says (told to the user). 16: accept (E10). 18: as drafted. 19: as approved, a retire list only on a Windows stall. 20: noted. 22: `find_package(ALSA)`, linked on Linux. `config.json`'s verbatim-keep and the `controlSurface` move land in one commit | |

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

## 4. Stage 1: the `midi` package

### 4.1 What it is

`packages/midi` (`@retroplug/midi`; dependencies `node-sdk` for the native half's `graph/rp_provider.h` and the byte
string, and `core` and `ui` for the TS half, one list the TS and native boundary checks both read; the native half alone
needs `node-sdk`, report item 20). Two native targets:

- **`retroplug-midi`**: the interface, the RtMidi backend, `systemMidi()`, the fake backend, `MidiStream` and the port
  filters. Links `rtmidi` PRIVATE and `Threads::Threads`.
- **`retroplug-midi-facet`**: `MidiRpcService`, as a provider with an rpc (manifest form in 4.5). Links `retroplug-midi`
  PUBLIC.

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
    virtual std::unique_ptr<MidiPort> openInput(const std::string& name, Receiver);   // throws std::runtime_error
    virtual std::unique_ptr<MidiPort> openOutput(const std::string& name);            // throws std::runtime_error
    virtual bool supportsVirtual() const;                                  // false on Windows
    virtual std::unique_ptr<MidiPort> openVirtualInput(const std::string& client, Receiver);   // "<client> In"
    virtual std::unique_ptr<MidiPort> openVirtualOutput(const std::string& client);            // "<client> Out"
};

MidiBackend& systemMidi();             // RtMidi, behind the macOS CoreMIDI guard (midiSystemUsable)
class FakeMidiBackend;                 // tests: scripted ports, recorded sends, deliveries on a thread or inline
}
```

This is the approved interface with the methods spelled out. Four things it leaves to the prototype, each verified
against the code and none a decision: **(a)** the listing and the filters: `listInputs` / `listOutputs` return every
port the system names, and the one copy of `isHardwareMidiPort`, `midiPortAllowed` and `hardwarePortIndices` filters
them in the callers (`MidiStream`, and the Launchpad's lister), so the fake can be tested against the filter; the RtMidi
backend's `openInput` / `openOutput` additionally refuse a name the allow list hides, as `rtMidiPortOpener` does today
(`RtMidiPort.cpp:112-115`, "MIDI port not allowed"). **(b)** The macOS guard is one function in `systemMidi()`'s
translation unit, as `midiSystemUsable` is today in both copies (`MidiIo.cpp:33-56`, `RtMidiPort.cpp:27-42`); the
Launchpad's copy goes. **(c)** `supportsVirtual()` replaces a catch: RtMidi's WinMM `openVirtualPort` does not throw, it
raises a WARNING (`deps/rtmidi/RtMidi.cpp:2903-2908`, `3125-3130`) that prints and returns, so today on Windows
`MidiIo::open` prints two warnings and then logs "virtual ports 'RetroPlug In' / 'RetroPlug Out' open" (`MidiIo.cpp:88`)
for ports that do not exist; the stream now logs what is true (edge E10, section 8). **(d)** The Launchpad opens its
ports under the client name `RetroPlug Launchpad` and the stream under `RetroPlug`, which the interface above does not
carry (report item 5): the Launchpad provider holds its own RtMidi backend instance constructed with the client name
`RetroPlug Launchpad`, while `systemMidi()` is the one under `RetroPlug`, so on-the-wire names and the pack's "a second
instance's link ports are not devices" rule are unchanged.

### 4.2 `MidiStream`

`MidiStream` is `MidiIo` rewritten over a `MidiBackend&` (not a copy of the file: its RtMidi calls, probe objects and
three `reopen` paths go; its policy stays). The public surface, as approved, with the call sites it serves:

```cpp
class MidiStream {
public:
    struct Message { std::uint64_t seq = 0; std::vector<std::uint8_t> bytes; };
    static constexpr std::size_t kCapacity = 256;               // the ring; one poll() returns at most this many

    explicit MidiStream(MidiBackend& backend);
    ~MidiStream();                                              // close()

    bool open(const std::string& client);                       // virtual "<client> In/Out" where supported
    void close();
    bool isOpen() const;

    // The selection: EXACT names (TS matched them, 6.3). Applied at once if open; a name not present is skipped and
    // re-applied by the next apply. `all` opens every hardware input and `names` is then ignored.
    void setInputs(const std::vector<std::string>& names, bool all);
    void setOutputs(const std::vector<std::string>& names);

    // The control surface's claim and a scan's held edge: the same rules as MidiIo, over lists. Neither stops audio.
    void setReserved(const std::string& input, const std::string& output);
    const std::string& reservedInput() const;   const std::string& reservedOutput() const;
    void setHardwareInputsSuspended(bool suspended);
    bool hardwareInputsSuspended() const;

    std::vector<std::string> listInputs() const;                // hardware, filtered (the pickers' list)
    std::vector<std::string> listOutputs() const;
    bool supportsVirtual() const;

    void poll(std::vector<Message>& out);                       // audio thread; lock-free; clears out first
    void send(const std::uint8_t* data, std::size_t n);         // audio thread: the virtual + every selected output
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

The outputs are an immutable `OutputSet` (the virtual output and a vector of hardware ones) reached through one atomic
pointer. A change builds the new set, publishes it (`store`, release), waits for the block in flight, then frees the old
one. The wait is an epoch handshake: `send` brackets its use of the set with a counter the audio thread increments to an
odd value on entry and an even one on exit (two relaxed-cost atomic stores, no lock and no allocation on the audio
path); the control thread reads the counter after publishing and, if it is odd, waits until it has changed. A block runs
at most one `send` per pending message (`renderAudioBlock` drains `engine.midiOut()` in one loop, `main.cpp:899-901`),
so the wait is at most one block's sends: none when audio is not running (the counter is even), at most about one
block's duration otherwise (10.7 ms at 512 frames, 85 ms at the handheld's 4096). A port send that blocks longer (a
WinMM bulk SysEx blocks about 170 ms, which is why the Launchpad writes on its own thread) makes the wait that long;
this is the cost of the approved design, and a retire list drained from the frame loop is the alternative that never
blocks the UI (report item 19: built as approved; a retire list drained from the frame loop is added only if the Windows
run shows a stall).

`reconfigureMidi`, `onReserveMidi` and `onSuspendInputs` therefore stop calling `stopAudio` / `startAudio`; the
PipeWire-link drop in `docs/sdl-release-readiness.md` section 1 loses two of its three callers here (the third is
`reconfigureAudio`, a real device change). A project switch re-applies the selection the same way, with no stop.

### 4.4 The users

- **The SDL app**: one `MidiStream` over `systemMidi()` in `AppState` (replacing `retroplug::MidiIo midi`, `:226`), the
  hooks `midi.describe()` and `midi.apply({ inputs, allInputs, outputs })` (6.4), the audio loop's `poll` / `send`
  unchanged (`renderAudioBlock`, `:848-899`), `extractRealtime` and `MidiClockSync` unchanged.
- **The CLI**: `MidiRpcService` over its own `MidiStream`, as `midi`'s provider facet, mounted by naming `midi` in the
  CLI's `PROVIDERS` (`retroplug-cli` joins `n8 launchpad midi`), which retires `#ifdef RETROPLUG_N8_BRIDGE` in
  `cli/main.cpp:29-31, 241-247` in favour of the generated `rp_register_package_rpc`. The RPC method names, parameters
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
packages/midi/native/src/midi/RtMidiBackend.cpp
    new: systemMidi(), the CoreMIDI guard, virtual ports, SysEx on
packages/midi/native/src/midi/MidiStream.{hpp,cpp}
    new: MidiIo's policy over the interface, the ring, the output set
packages/midi/native/src/midi/FakeMidiBackend.hpp
    new: header-only test support (4.6)
packages/midi/native/src/midi/facet/
    new: MidiRpcService.{hpp,cpp} (moved), the provider, registerRpc
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
    retroplug-cli: PROVIDERS n8 launchpad midi (:267)
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
header-only test support in `retroplug-midi`'s include root, linked by the Launchpad's test and compiled into the
headless host and nothing else (the shape `retroplug-launchpad-harness` has: "a header a host, and only a host,
compiles"). `FakePorts.hpp` shrinks to the device model over it.

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

## 5. Stage 2: the `audio` packages, `AudioHost` and `AUDIO_BACKENDS`

### 5.1 The packages and the interface

`packages/audio` (`@retroplug/audio`; dependencies `node-sdk`, `core`, `ui`: one list for both checks, the native half
needs `node-sdk` alone, report item 20): native target `retroplug-audio` (the interface, the `null` backend,
`AudioHost`), plus, in S3, the TS section and rows. `packages/audio-portaudio` and `packages/audio-sdl` (each depends on
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
    virtual bool start() = 0;
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
    virtual std::unique_ptr<AudioStream> open(const Request&, Pull, void* ctx, std::string& error) = 0;
};
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
flipping drivers under a running stream", and this is that.

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
    AudioHost(std::vector<std::unique_ptr<AudioBackend>> preference, AudioClient& client);   // `null` appended last
    bool apply(const Config& cfg);          // the whole sequence, once (below); true when a real stream is running
    void stop();                            // the teardown path
    bool poll();                            // the UI frame: a lost stream falls back; true if the status moved
    AudioStatus status() const;
    Describe describe() const;              // every backend with its drivers and devices, the auto choice, the status
};
```

`Config` is the `audio` section's fields (`backend`, `driver`, `device`, `sampleRate`, `blockSize`, `outChannels`) plus
`renderAheadFrames`, which the flag sets and the section does not carry. `AudioHost` owns, in the order of what it
replaces:

- **open / start / stop / reconfigure.** The sequence is stop, release, open, `prepare(info())`, start (report item 2).
  It is today's `reconfigureAudio` (`main.cpp:1254-1281`) with the open moved before the re-rate: stop the stream;
  `client.release()` (`setAudioThreadOwns(false)` and the invoker drain); open (not started); `client.prepare(...)` with
  the OBTAINED rate, channels and block (`engine.setSampleRate`, `setHostOutputs`, `setMaxBlockFrames`, which today
  straddle the open); hand the engine back; start. Written once here and not in each backend. The order differs from the
  approved sketch (prepare before open) on purpose: today's code re-rates the engine with the REQUESTED rate before
  opening and records the obtained one only in `AppState::sampleRate` (`:1268`, `:1093`, `:1148`), so a device that
  adopts another rate plays an engine rendering at the first; the interface allows the new order because `open` does not
  start.
- **fallback.** A failed `open` logs the backend's error and falls to the `null` backend (the sleep-paced pump of
  `audioPumpLoop`, `:927-936`, which stays what `sdl:smoke` runs on), recording a **status**
  (`{ backend, driver, device, rate, error }`) that TS shows, so Settings says "Muted: <reason>" instead of silence.
  Today a failure prints once ("`Pa_OpenStream(...) failed: ... (muted)`", `:1143-1146`) and a persisted request that
  the device rejects mutes on every launch with no way to see why (release-readiness section 1).
- **a lost device.** Today's freeze (the emulator advances only inside the callback) becomes the same fallback: the UI
  frame calls `poll()`, which checks `stream.lost()` and reopens on `null`, with the status naming the reason. The
  detection is each backend's (PortAudio's finished stream, SDL's device status; neither is verified against the forks
  in the design, the prototype checks each), and `AudioHost` adds a backend-neutral watchdog (report item 9): a pull
  that has not run for a bound while the stream is started (a few block durations, never under 250 ms) counts as lost,
  so a backend that never reports still falls back. The watchdog reads one counter the pull stamps (a relaxed atomic),
  costs the audio path nothing else, and is off for `null` and while stopped.
- **render-ahead** (decision 13), ported from `4b9e39ea8`: `renderAheadFrames > 0` runs a render thread filling an
  interleaved ring in `blockFrames` blocks; the stream opens with `blockFrames 0` and the pull only copies, padding a
  short ring with silence and counting an underrun once the ring has primed. The ring holds at least two blocks
  (`max(renderAhead, 2 * block)`, `4b9e39ea8`'s `ringFrames`). The Linux real-time treatment moves here: the callback
  thread at `SCHED_FIFO` 85 above the renderer's 80 and both on the audio core (`tuneAudioThread(priority)`), applied
  per thread rather than once per process: the `static std::atomic<bool> tuned` latch (`:916`) means a re-opened
  stream's new thread is never tuned (release-readiness section 1). Set by `--render-ahead <frames>` /
  `RETROPLUG_SDL_RENDER_AHEAD`; when `feature/handheld-perf` rebases its `main.cpp` hunk reduces to the muOS launcher
  flags.
- **stats.** `RETROPLUG_SDL_AUDIO_STATS` and the five-second load line of `4b9e39ea8` (`AudioStats`) move here, backend
  neutral; `underflow` is the pull's flag, which SDL never sets (it has none).
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
  before `Pa_Initialize` at first use of the backend. A driver's `id` is the lower-cased host API name (`pipewire`,
  `alsa`, `jack`, `mme`, `wasapi`, ...) and its label PortAudio's. The backend calls `snd_lib_error_set_handler`
  directly (`:1072`), which today links only because `rtmidi` brings `libasound` along; the target therefore names ALSA
  itself: `find_package(ALSA)` and a link to `ALSA::ALSA` on Linux (report item 22). `deps/portaudio`'s options and
  `add_subdirectory` move to `cmake/portaudio.cmake` at root scope (5.4).
- **`audio-sdl`**: `SDL_InitSubSystem(SDL_INIT_AUDIO)` at first use (`setupSdl` initialises video and the game
  controller only, `:1390`), `SDL_OpenAudioDevice` with a float32 pull callback, 2, 4, 6 or 8 channels, the frequency
  allowed to change and adopted (as PortAudio's is) and the channel count exact, `SDL_GetNumAudioDrivers` /
  `SDL_GetAudioDriver` for drivers, `SDL_GetNumAudioDevices` / `SDL_GetAudioDeviceName` for devices, and
  `SDL_GetDefaultAudioInfo` (SDL 2.24 and later; the tree has 2.30.0, the macOS CI framework 2.32.10) for the default's
  name. SDL has no underflow flag and no "unspecified" block size; both are prototype questions (report item 18):
  `drivers()` hides the `dummy` and `disk` test drivers unless a request names one, `blockFrames 0` maps to a small
  power of two the prototype picks, and the obtained size is reported in `StreamInfo`.

### 5.4 The build: `AUDIO_BACKENDS`

`AUDIO_BACKENDS` joins `retroplug_product` beside `PROVIDERS` (`cmake/packages.cmake:1540-1579`, the signature and
`cmake_parse_arguments` at `:1579`). A new manifest item
`{ "target": "retroplug-audio-portaudio", "audioBackend": "rp_portaudio_backend", "device": false }` is read beside the
provider branch of `_retroplug_read_manifest` (`:230-263`), into `RP_PACKAGE_<name>_AUDIO_BACKENDS` and
`_AUDIO_BACKEND_TARGETS`. What the product does, following the provider rule (`:1666-1677`):

- **A product that lists the package without naming it drops the target** (the provider rule's loop over
  `_providerTargets`), so the CLI, the headless host, `retroplug-render-host-test`, `retroplug-ui-test` and the Node
  addon, which list the desktop packages and name no backend, link neither PortAudio nor SDL audio.
- **Four configure refusals**, each a `message(FATAL_ERROR ...)` with its own words and its own fixture:
  `AUDIO_BACKENDS` names a backend no manifest declares; names one twice; names one whose package the product does not
  list; and a manifest item that is an `audioBackend` and an `entry` or a `provider` at once (the reader's refusal, in
  the family of `EntryWithProvider` and `ProviderTargetWithEntry`, `packages/native/cmake/tests.cmake:188, 200`). A
  fifth is implied by the rest of the file and is added with them: an `audioBackend` item that is `device: true` (a
  device image links no audio), as `ProviderOnDevice` refuses a provider there.
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
sources, no package; the file list is `retroplug-sdl`'s in `packages/native/CMakeLists.txt:333-337`):

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

### 5.6 What changes, by file

```
packages/audio/package.json
    new: @retroplug/audio; retroplug.native = { "target": "retroplug-audio" } (a library: see 4.1 and
    report item 4); app and ui arrive in S3
packages/audio/native/
    new: audio/AudioBackend.hpp, AudioHost.{hpp,cpp}, NullBackend.cpp, FakeAudioBackend.hpp (test
    support), the test
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
    retroplug-sdl: sources become the six units; PROVIDERS stays; adds AUDIO_BACKENDS portaudio sdl;
    links retroplug-audio and retroplug-midi; loses the direct portaudio link (:345) and its include (:359-360)
packages/native/sdl/*.cpp, app.hpp
    the split above; reconfigureAudio, openAudio, startAudio, stopAudio, audioPumpLoop, paCallback,
    quietenAudioLogs, AudioCfgJson and the PortAudio enumeration are gone from the product
root package.json
    retroplug.products.desktop: core, audio, audio-portaudio, audio-sdl, midi, launchpad, system, ...
    (the plugin list is untouched)
packages/native/test/packages/refused/
    AudioBackendWithEntry.json, AudioBackendWithProvider.json, AudioBackendOnDevice.json
    (+ their lines in tests.cmake:183-211)
packages/native/test/packages/product-lists/
    the product-level cases: unknown, duplicate, not listed, on DEVICE
tools/run-sdl-smoke.sh
    pins RETROPLUG_SDL_AUDIO_BACKEND=null (section 9)
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
one that pulls steadily never does; neither fires while stopped); **open failure -> null with a status** (the fake's
`open` fails with an error string; the host falls to `null`, `status().error` carries it, a later `apply` with a working
fake recovers); **lost device -> null** (the fake's `lost()` flips; `poll()` moves to `null` and reports a status
change); **render-ahead output bit-identical to in-callback across ragged pull sizes** (a deterministic client whose
`render` writes a counter ramp and a state-dependent tail: pulls of 1, 7, 64, 255, 512 and 1000 frames from a
render-ahead host and from a plain one must produce the same sample stream, and the underrun counter stays zero once
primed). The ring case also runs in `build-tsan/`. (3) The PortAudio and SDL backends compile in CI's `all` on all four
platforms; no unit test opens a real device. (4) The product tree: `retroplug-sdl`'s `nm -D` (or the platform's
equivalent) must carry no `Pa_` symbol when it names only `sdl`, and the CLI must carry none either way (`readelf -d`
shows no `libportaudio` / `libSDL2` audio dependency beyond what it already has).

**Held:** `sdl:smoke` (on `null`, section 9), `sdl:pipewire` with its greps unchanged and its configuration written as a
`config.json` section (section 7), every baseline of section 9, the registration golden (no node type moved), the device
images, `reaper:params` (the plugin is untouched), and `check:boundaries` / the native boundary check judging the three
new audio packages (`audio`'s links: `node-sdk` only; `audio-portaudio` and `audio-sdl` link `audio` and a third-party
library each).

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
  equality test compares those bytes.
- **Writes keep today's re-read** (`UserConfigStore.update`, `userConfigStore.ts:211-216`): a section write is computed
  over the file as it is on disk, because two processes may share it (the plugin and a standalone).
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
(`packages.mjs:900`).

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
saved `audio.json` wins (E12).

**`midi`** (project section, `midi` package):

```ts
{ inputs: [{ name: string }], allInputs: false, outputs: [{ name: string }], clockBpm: 120 }
```

Key `midi`, distinct from the shell's `midiRouting` (the graph's routing, unchanged; `CORE_SETTING_KEYS`,
`projectConfig.ts:33`). The entries are objects so per-device routing can add a field later without a version bump; the
section declares no `version` (additive only, so nothing is stamped). A fresh project has no ports (the virtual ones
only), which is today's default (`MidiIo.hpp:196-205`). `clockBpm` is clamped to 20..999 (`MidiClockSync::kMinBpm` /
`kMaxBpm`, `main.cpp:139-140`, the range the estimator accepts). The desktop's fresh project gains a `midi` block; the
plugin's does not (F5a's E8 at one more key), and a block the standalone wrote survives the plugin verbatim (E9).

### 6.3 Name matching

`midi/ts/portMatch.ts`, pure and platform-free: `resolvePort(saved: string, present: string[]): string | null`. Exact
first. Then both sides are normalised, with the volatile parts stripped: ALSA's trailing `NN:NN` client:port numbers
(`"LPProMK3 MIDI:1 24:0"` against `"... 28:0"`, which move as other clients come and go; the harness's own fakes carry
them, `tools/run-sdl-controller.sh:82-87`), the Windows `N- ` index prefix WinMM puts before a name
(`"2- MIDISPORT 2x2 Port A"`), CoreMIDI's repeated device name, and runs of spaces. The normalised names must match
exactly ONE present port; two candidates, or none, resolves to nothing. An entry that resolves to nothing stays in the
section, shows "<name> (missing)" in the rows, and opens nothing. Tested against a table of real ALSA, WinMM and
CoreMIDI names: the table is data in `packages/midi/ts/test/` and is extended from a real machine of each kind (the
Windows and macOS rows are the user's agents', owed: section 11). Matching lives in TS so the stored name is the only
thing native sees as a selection: `midiDevice.apply` is handed EXACT port names.

### 6.4 Reaching native

`core/host.ts` gains two contracts (the group names are the approved ones; the member shapes are the sketch's):

```ts
export interface AudioDevice {
  describe(): AudioDescription;     // backends, drivers, devices, defaults; the auto choice; the status
  apply(cfg: AudioConfig): AudioStatus;   // the whole reconfigure; returns what was adopted (or why it is muted)
  status(): AudioStatus;            // cheap read for the status row and the UI frame
}
export interface MidiDevice {
  describe(): MidiDescription;      // { inputs, outputs, supportsVirtual, reserved: { input, output } }
  apply(sel: { inputs: string[]; allInputs: boolean; outputs: string[] }): void;
  transport(): TransportState;  setTransport(playing: boolean): void;  setClockBpm(bpm: number): void;
}
```

The native side binds four globals, replacing eight: `__rp_audioDescribe`, `__rp_audioApply`, `__rp_audioStatus`,
`__rp_midiDescribe` and `__rp_midiApply` (the names are this draft's), keeping the transport trio (`__rp_getTransport`,
`__rp_setTransport`, `__rp_setClockBpm`) that the MIDI Clock submenu and the smoke's `RETROPLUG_SDL_TEST_TRANSPORT` use
(`main.cpp:1723-1724`), minus `setClockBpm`'s `saveAudioConfig` (`:663`: the tempo is the project's now). The shell's UI
host (`ui/host/uiHost.ts:40-77`) builds `audioDevice` and `midiDevice` only where the globals exist, so the plugin and
the DPF standalone hide the rows as they do today (`isStandalone() && hasAudioConfig()`, `menuDefs.ts:1349-1350`); the
headless and test hosts fake the globals (`test/menu/recorded-cases.ts:584-587`, `test/menu/leaves.test.ts:1224-1225`
change with them).

`audio.describe()` returns, per backend, the label, the drivers (id, label), per driver the device names and the
default's name, and the auto choice (which backend and driver "auto" resolves to right now, for the row "Auto
(PortAudio: PipeWire)"), and the status; `apply` validates (channels 2, 4, 6, 8; rate and block positive) and returns
the status. `midi.describe()` returns the hardware port lists (filtered), whether virtual ports exist, and the reserved
pair, so the checklist can mark a port the control surface holds.

### 6.5 Menus

The rows move out of the shell, with the files they live in: `audioDraft.ts` and the audio rows of `menuDefs.ts`
(`:137-199`, `audioSettingsChildren`) into `audio`'s `ts/ui/`; `midiDevices.ts`, the device pickers (`:241-290`,
`deviceCyclerNames`, `inputCyclerNames`, `midiSettingsChildren`) and the MIDI Clock submenu (`midiClockChildren`,
`midiClockRow`, `transport.ts`) into `midi`'s `ts/ui/`. The shell keeps the Audio and MIDI submenu containers and the
clock slot (report item 11): it has the placement `settings.midi` and none for audio or the clock
(`ui/ts/contribution.ts:160-171`), so it adds `settings.audio` and `instance.clock`, and each package fills its own.

- **Settings > Audio**: one Driver row whose entries name the backend ("Auto (PortAudio: PipeWire)", "PortAudio:
  PipeWire", "PortAudio: ALSA", "SDL: pipewire", ...), Output Device, Rate, Block (with the latency label), Out
  Channels, Apply (the draft model stays: the rows edit a pending value and Apply commits it, `audioDraft.ts:2-9`), and
  a status row when muted ("Muted: <reason>", an `infoRow`).
- **Settings > MIDI**, headed "saved with this project": an inputs checklist plus "All devices", an outputs checklist,
  and the entries that do not resolve as "<name> (missing)", and a "Rescan" row that re-resolves the saved names against
  a fresh port list. The menu model has no checkbox kind (`MenuItemKind`, `packages/ui/ts/menu/menuTree.ts:14`), so a
  checklist is a column of `action` rows with `keepOpen`, labelled `[x]` / `[ ]`. A change calls
  `project.setSection("midi", ...)`, which marks the project dirty and so raises the close guard, and applies at once
  through the service below. The Launchpad's scan rows still land under `settings.midi`.
- **The MIDI Clock submenu** moves, its tempo (`Tempo: N BPM`) now a write to `midi.clockBpm`; the transport's play
  state is still not persisted (it starts stopped at every launch, `main.cpp:121-185`'s comment).

A **UI service** in `midi`'s `ts/ui` (`defineUi({ services })`, `UiService` in `ui/ts/contribution.ts:244-250`) watches
`project.section("midi")`: its `start(host)` runs at every mount (it must be idempotent), `poll` compares the section
with the last applied selection and, on a difference, resolves each saved name against `midiDevice.describe()` (6.3) and
calls `midiDevice.apply(...)`; it also applies `clockBpm` through `setClockBpm`. A project switch changes the section
and so re-applies, with no audio stop (4.3). There is no hotplug, so the service re-resolves (a list probed again) at
every project load and whenever the Settings > MIDI submenu is opened, and the submenu has a "Rescan" row that does the
same on demand (report item 12).

### 6.6 Startup

The SDL app's rendezvous holds audio and MIDI until TS's `restored()` or 500 ms (`restoreControlSurface`,
`main.cpp:1000-1011`, which today waits only when the product started the Launchpad provider). It widens to a second
condition: TS's first `audioDevice.apply`, issued from the `audio` section by the UI's mount. Native therefore opens
audio after that apply or, at the 500 ms bound, with defaults (Auto), so a missing or broken `config.json` can never
keep the app silent (the rule `restoreControlSurface` states). `setupSdl` no longer opens a device (`:1437-1441` goes):
it initialises video and the game controller, and the audio backends initialise their libraries at first use. The
virtual MIDI ports open at once (nothing in the file chooses them); the hardware ports open when a project's section is
applied, which is after the autoload (`:1670`) and the UI mount; the control surface's claim, reserved from TS's
`restored()`, still precedes any hardware open of that port, which is what `sdl:controller` leg 2 holds (section 9).

Two consequences the order carries, both found in verification: the engine is constructed before any device is open, so
`bootControlPlane`'s `engine.setSampleRate(a.sampleRate)` (`:1335`) reads the default 48000 rather than an obtained
rate, and a configuration that differs gets one in-place re-rate at `prepare` (`Engine::setSampleRate`,
`Engine.cpp:682-690`; `reconfigureAudio` already relies on it): The user's decision (report item 1) keeps this order and
holds it with the engine-graph case of 5.7 (a re-rate bit-equal to a fresh build, GB and NES), moving the rendezvous
before the autoload only if that case fails. The rendezvous waits on TS, so a product whose TS never applies (a broken
bundle) costs the 500 ms and then starts on defaults, as today's does for the restore.

### 6.7 What changes, by file

```
packages/audio/package.json, ts/
    app module: definePackage({ configSections: [audio] }); ts/ui: the rows
packages/midi/ts/
    app: projectSections [midi]; portMatch.ts; ts/ui: rows, clock, the service
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
packages/retroplug/src/userConfig.ts
    controlSurface schema deleted; raw non-core keys carried
packages/retroplug/src/userConfigSerialization.ts
    writes contributors' sections, then unknown keys, verbatim
packages/retroplug/src/userConfigStore.ts
    controlSurface()/setControlSurface() -> section()/setSection()
packages/retroplug/ui/host/{uiHost,nativeHooks}.ts
    the config group; audioDevice/midiDevice; the eight globals -> the new ones
packages/retroplug/ui/screens/menu/
    audioDraft.ts, midiDevices.ts, transport.ts, the audio/MIDI/clock rows move out
packages/retroplug/package.json
    + @retroplug/audio, @retroplug/midi
test-ui/
    audio-device, audio-driver, midi-settings, midi-clock: moved with the rows
test/menu/{recorded-cases,leaves}.ts, golden
    the three seam cases re-recorded; the matrix's other cases held
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
the Driver labels against a described list with two backends and a same-named driver ("PortAudio: PipeWire" and "SDL:
pipewire"), the status row, and a host with no hook hiding the submenu. (5) The `controlSurface` move:
`launchpad-devices.test.ts` and `menu/launchpad.test.ts` re-pointed at `host.config.section`, with `config.json`'s
`controlSurface` bytes compared before and after the move on a recorded file (E8). **Held:** the buildGraph and role
goldens' documents (no model moves; the tests that deep-equal a fresh project's settings gain `midi`:
`test/project/config.test.ts`, `sections.test.ts`, `tolerance.test.ts`, `test/graph/plugin-product.test.ts`), the
recorded menu matrix except its three audio/MIDI cases, the parity, kit and render baselines, `check:boundaries` with
`audio` and `midi` (rule 9's lists need no entry: the shell imports no new package by hand), the literal scanner over
`src`, `cli` and `ui`.

## 7. Stage 4: the SDL-audio leg and the docs

**`sdl:pipewire` and the SDL-audio mode.** `tools/run-sdl-pipewire.sh` writes its configurations as `audio.json`
(`CFG="$HOME/.config/retroplug/audio.json"`, `:53`, `:108`, and the `writecfg` lines `:126-154` and `:192`); after F5b
it writes a `config.json` with a stamped `audio` block (`{"schemaVersion":3,"audio":{...}}`: `backend`, `driver`,
`device`, `sampleRate`, `blockSize`, `outChannels`) and drives the PortAudio checks with `--audio-backend portaudio` (or
`RETROPLUG_SDL_AUDIO_BACKEND`), which overrides the saved choice for the run. Its greps do not change: the `sed` over
`audio: PortAudio [..] '..' N Hz` (`:117`, `:201`), `grep "not found in the chosen driver"` (`:144`), the wide-output
`audio: PortAudio .*512 frames, 8 ch` (`:206`), and `pw-link -o | grep -c '^retroplug-sdl:'` (`:198`: the PortAudio
stream's node name). The explicit-driver case's `"hostApi":18` becomes `"driver":"pipewire"`. A **new mode of the same
script**, `--audio-backend sdl` (the leg the approved section adds): the default sink is followed, an explicit device
pick beats it, a stale pick falls back with a warning, and 8 channels open 8 PORTS wide with pair 1 linked, all against
the same private PipeWire server with the three null sinks. Its greps are the SDL backend's own log line (the same form
with `SDL [pipewire]`), and its node-name prefix is read from the run, not assumed, because SDL names its PipeWire node
from its app-name hints (`setupSdl` sets `SDL_HINT_APP_NAME` to `RetroPlug`, `:1388`). SDL's multichannel layouts (4 is
quad, 6 is 5.1, 8 is 7.1) differ from PortAudio's "declared layout" stream, so "pair 1 linked" is verified in the leg,
not assumed (risk, section 11). Not in CI (the runners have no PipeWire); the devcontainer ships one.

**`sdl:smoke`** pins `RETROPLUG_SDL_AUDIO_BACKEND=null` (section 9); **`sdl:controller`** is rewritten around the
project section (section 9).

**The docs**: `AGENTS.md` (the SDL and CLI sentences, the providers paragraph gains `midi`, a paragraph for the audio
backends and `AUDIO_BACKENDS`, "config sections", the `test:plugin` count, which gains `retroplug-audio-host-test`,
thirteen to fourteen, and a `retroplug-midi-test` description over the stream: report item 14); `spec/00` to `spec/06`
where a figure moved (`spec/05` for the `audio` root section and the `midi` project section and the two ignored files;
`spec/06` for the new product wiring, the new tests, the harness changes and the root-scope PortAudio); `spec/07` (the
F5b entry, the per-device-routing follow-up and the `feature/handheld-perf` rebase note, F5b removed from the open
list); `docs/sdl-standalone.md` (the persistence, audio-backend and MIDI-selection paragraphs, `:64-76`, `:104-123`,
`:195-246`, which still describe `audio.json`, `midi.json` and a PortAudio-only host); and the release-readiness ticks
(`docs/sdl-release-readiness.md`, with the commit hash in each): the `Pa_StopStream` lock, the dead device, the bad
persisted config, the per-process RT tuning, the unversioned `audio.json` / `midi.json`, and the `main.cpp` split.
`check:docs` stays green (relative links only to tracked files).

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
  ALSA", "SDL: pipewire". PortAudio's log line is byte-identical to today's (5.2); SDL's is the same form.
- **E5. A MIDI change or a Launchpad claim no longer stops audio.** `reconfigureMidi`, `onReserveMidi` and
  `onSuspendInputs` lose their `stopAudio` / `startAudio` (4.3). What `sdl:controller` reads that this touches: the
  claim still logs `control surface: reserved MIDI input '<port>', output '<port>'` (`main.cpp:959-969`, grepped at
  `run-sdl-controller.sh:148, :160, :209, :231`), the stream's lines are unchanged, and no script greps an audio stop,
  but leg 2 compares the claim's line with `] MIDI: virtual ports` (`:165-169`), and under decision 23 the virtual ports
  now open BEFORE the claim, so that comparison cannot stay; section 9 gives its replacement (report item 15). A real
  device change (`audio.apply`) still stops and restarts the stream, once.
- **E6. A failed or lost device shows "Muted: <reason>".** Today a failed open prints once and runs the pump
  (`main.cpp:1130, :1143-1146`), a persisted request the device rejects mutes on every launch with no way to see why,
  and a device that disappears freezes the app (the emulator advances only inside the callback). After F5b each is the
  same fallback to `null` with a status the Settings row shows.
- **E7. The clock tempo is saved in the project.** It was `audio.json`'s `clockBpm` (`:1197`). A project switch applies
  the new project's tempo; the transport still starts stopped.
- **E8. `config.json` gains `audio`; `controlSurface`'s bytes are unchanged.** The Launchpad's section keeps the root
  key and the four fields. The shape of the file changes by one key; `schemaVersion` stays 3.
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
  `--audio-backend`; before, a saved `audio.json` won over `--block-size` whenever one existed.
- **E13. Startup builds cores before the device is open** (the user's item 1): cores are built at the default rate and
  re-rated once at `prepare`; held by the re-rate equality case (5.7).
- **E14. On Windows PortAudio's Auto prefers WASAPI** (the user's item 10), where it resolved to MME; the Windows agent
  verifies it, and a saved `driver` still wins.

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
| `sdl:smoke` (`tools/run-sdl-smoke.sh`) | `running `, `clock self-test`, `transport self-test: playing=1 bpm=140`, `post-multiout: numOutputs=8 planarBufs=8`, `post-resize`, `wheel`, `close-guard`, `[idle-reclaim]`; exports `SDL_AUDIODRIVER=dummy` and `RP_MIDI_PORT_ALLOW=RP_NO_PORT` (`:28, :37`) | Unchanged lines. It pins `RETROPLUG_SDL_AUDIO_BACKEND=null`: with `SDL_AUDIODRIVER=dummy` still exported and `sdl` second in the preference order, an `auto` run on a runner whose PortAudio finds no device would open SDL's dummy driver and run on that instead of the sleep-paced pump the script means (report item 13). `RETROPLUG_SDL_TEST_RECONFIG` and `TEST_MULTIOUT` keep calling the reconfigure through `AudioHost` and print the same `post-` lines |
| `sdl:pipewire` | `audio: PortAudio [h] 'd' N Hz`, `not found in the chosen driver`, `audio: PortAudio .*512 frames, 8 ch`, `pw-link` ports; writes `audio.json` | Greps unchanged (5.2). Writes `config.json`'s `audio` block; a new SDL-audio mode (section 7) |
| `sdl:controller` | writes `midi.json` three times (`{"input":"*",...}`); `reserved MIDI input`, `] MIDI: virtual ports`, `] running `, `MIDI in: 90 51 7F`, `MIDI out: '<p>' is held by the control surface`, `hardware inputs suspended`, `scan test: started=true`, `could not claim`, `in use by another RetroPlug`, `did not report its restore`; python over `config.json`'s `controlSurface` | The `midi.json` writes become a project with a `midi` section autoloaded (`--autoload`), since the selection is the project's now: leg 1 and leg 5 load an all-inputs project (`"midi":{"allInputs":true,...}`), leg 4 adds `"outputs":[{"name":"<port>"}]`, leg 3's project (`controller.rplg`) is unchanged. Leg 2's ordering `reserved_at < midi_at` is replaced: the claim must precede the first `MIDI in: connected hardware port` line for the claimed port (stronger: that line must never appear for it) and still precede `] running `; leg 4's `claim_at > running_at` (claimed mid-session) holds. The python reads `config.json`'s `controlSurface` unchanged (E8) |

**What is not proved, said plainly.** The real Launchpad (manual, as F3); the muOS device run of render-ahead (owed
after `feature/handheld-perf` rebases); a real PipeWire, PulseAudio, CoreAudio, WASAPI or MME stream under the SDL
backend beyond the new leg on this machine; a MIDI hotplug (there is none); Windows and macOS builds and runs of every
new target and of `portMatch`'s table on real names (section 11).

## 10. The stages

Five stages on a prototype branch `proto/f5b-audio-midi`, one commit each, DeepSeek Flash implementing and Opus
reviewing each before the next (F4a to F5a's routing). Each stage leaves the tree green on every oracle of section 9 it
can reach. After any fix-pass rebase, each stage's tree is extracted after the final rewrite, proven equal to its
commit, and typechecked and tested alone, never symlinking the worktree's `build/` for a run that writes.

0. **The SDL branch merge** (section 3). *Exit:* `retroplug-midi-test` 25 cases and the TSan ring case green;
   `test:plugin`, `sdl:smoke`.
1. **The `midi` package** (section 4): the interface, `MidiStream`, the fake, the facet as a provider, the Launchpad
   onto it, the CLI's `PROVIDERS`, the harness edits `sdl:controller` needs for the lines this stage moves. *Exit:* the
   output-swap case under TSan; the Launchpad's 53 cases over the fake; `launchpad-facet`, `launchpad-devices` and the
   N8 bridge sessions in `test:native`, plus the new case for E11 (`midiOpen(client, "")` opens every hardware input, a
   name opens that one); `sdl:controller` green UNCHANGED: until S3 the app keeps reading `midi.json` through a small
   shim that maps its one `input` / `output` onto `setInputs` / `setOutputs` (`"*"` is `all`), so the stage moves no
   startup order and no harness line (section 9 lists what S3 changes).
2. **The `audio` packages and `AudioHost`** (section 5). *Exit:* the `AUDIO_BACKENDS` refusals and positive controls;
   the `AudioHost` cases (call order with the obtained rate, fallback, lost device, render-ahead, the watchdog) and the
   engine-graph re-rate equality case; a configure and `all` build with both backends linked into `retroplug-sdl` and
   neither into any other product; `sdl:smoke` on `null`; `sdl:pipewire` with its greps unchanged on a PortAudio run.
3. **TypeScript** (section 6). *Exit:* the config-section rules, `portMatch`'s table, the service and row tests (Rescan
   and the `--block-size` override among them); the recorded matrix's three cases re-recorded with the diff as the
   record; `sdl:controller` on the project-section form; `reaper:params` on the plugin's bundles (no plugin file
   changed).
4. **The SDL-audio leg and the docs** (section 7). *Exit:* `sdl:pipewire` in both modes; the docs pass; the readiness
   ticks.

After the prototype: a plan generator, Flash-Next implementers, the serial acceptance kit (adding `reaper:all`, a fresh
clone's typecheck before any build, BlipToaster and NESVJ on scratch copies, and the SDL legs above; no hardware leg),
an Opus final review and follow-ups. Windows, macOS, arm64 and CI are owed.

## 11. Risks and open questions

Each is marked with what settles it; the draft report's items are all answered (rows 28 and 29), and what remains are
risks the prototype or a run on the named platform answers.

**Windows (for the user's Windows agent).**

- **R1. SDL audio drivers on Windows.** SDL2 lists `wasapi`, `directsound`, `winmm` and the `dummy` and `disk` test
  drivers; which one the vcpkg `sdl2:x64-windows-static` build (`build.yml:328`) compiles in and which `SDL_AudioInit`
  picks first is not checked here. SDL initialises COM on the thread that opens a device; the UI thread opens it, as
  PortAudio's WASAPI does, and the two backends never hold a stream at once (the host stops one before it opens the
  other). The SDL leg of `sdl:pipewire` is Linux-only, so the Windows SDL path has no automated proof (a manual one is
  owed).
- **R2. MME against WASAPI.** `autoOutputHostApiIndex` falls back to the platform default's host, which on Windows is
  MME (release-readiness section 3); the PortAudio backend's `autoDriver()` now prefers WASAPI there (report item 10,
  E14), which no one has run on Windows: the Windows agent verifies that Auto opens a WASAPI device, that the fallback
  to MME works when WASAPI refuses, and that a saved `driver` still wins.
- **R3. No virtual MIDI ports on WinMM.** `supportsVirtual()` is false there (4.1), so the app has no "RetroPlug In/Out"
  for a DAW to connect to, as today, now said. MIDI inputs are exclusive on Windows: the scan's held edge
  (`scanSuspendsInputs`) is the one lever, and with no audio stop around it the hardware inputs close and reopen under a
  running stream. `portMatch`'s `N- ` prefix table needs real WinMM names.
- **R4. Vcpkg SDL2 and `/W4 /WX`.** `retroplug-audio-sdl` and `retroplug-audio-portaudio` are our targets and take
  `/W4 /WX` (`AGENTS.md`); `retroplug-audio-sdl` includes `SDL.h` (`SDL_MAIN_HANDLED` is the product's, the backend
  needs none) and PortAudio's headers come from a subdirectory build that does not export its include directory (5.4). A
  rotted `#if defined(_WIN32)` half of the moved quieting code is where a first warning lands (it is Linux code,
  `#if defined(__linux__)`, so the Windows half is the `PaUtil_SetDebugPrintFunction` declaration alone).
- **R5. The output-set swap's grace wait** can be as long as one WinMM bulk SysEx send (about 170 ms) because `send` is
  the critical section the wait outlasts (4.3): a Settings > MIDI change on Windows can stall the UI frame that long.
  Report item 19: built as approved, with a retire list only if a Windows run shows the stall.

**macOS.**

- **R6. CoreMIDI.** `midiSystemUsable()`'s client creation (a kept keepalive, three attempts, then "no MIDI") moves with
  `systemMidi()`; the Launchpad's copy of the probe goes, so the guard now covers both uses with one client. The abort
  it prevents (RtMidi's `throw()`-declared singleton) is only observable on a macOS run.
- **R7. The framework SDL.** The build links `SDL2.framework` (`build.yml:375-384`, 2.32.10) and `retroplug-sdl` is a
  bare executable, so `SDL_INIT_AUDIO` runs against the build machine's framework; the release packaging is out of
  scope.

**Everywhere.**

- **R8. SDL2 runs one audio driver at a time.** The `sdl` backend's device list is real only for the active driver
  (5.1). Switching drivers means `SDL_AudioQuit` and `SDL_AudioInit(name)` while no stream is open; `AudioHost`
  guarantees that by stopping and closing first, and a request that cannot switch (no such driver) is the same `open`
  failure as any other and falls back to `null` with the reason.
- **R9. Startup ordering** (6.6): the rendezvous now waits on two TS events (the restore and the first audio apply) and
  on a 500 ms bound; a slow UI mount costs up to half a second of silence at launch, as the restore wait does today.
- **R10. PortAudio's PipeWire host API and `lost()`.** Whether the fork reports a removed PipeWire node as a finished
  stream, and whether SDL's device status flips on a removed device, is not checked in the design; the prototype
  verifies each, and `AudioHost`'s no-pull watchdog (5.2) covers a backend that never reports.
- **R11. SDL's multichannel layouts** (quad, 5.1, 7.1) against PortAudio's declared layout: the wide-output leg verifies
  pair 1 is linked; it is the one place the two backends may differ audibly.
- **R12. A configure-time dependency.** `portaudio`'s `add_subdirectory` moves earlier (root scope, before the
  packages), so its `find_package` / `pkg_check_modules` probes (ALSA, PipeWire, JACK) run for the plugin-only build
  too; today the same probes run, only later. The `PA_*` cache options are set at the same point as before relative to
  the subdirectory.
- **R13. `audio-portaudio` and ALSA.** The moved quieting code calls `snd_lib_error_set_handler`, which `rtmidi` linked
  implicitly; the target names ALSA itself on Linux, `find_package(ALSA)` and `ALSA::ALSA` (report item 22).
- **R14. Merge order.** `feature/handheld-perf` edits the audio region this phase deletes (`4b9e39ea8` is `+168/-15` in
  `main.cpp`); its rebase is the user's follow-up, and its `main.cpp` hunk reduces to the muOS launcher flags once
  render-ahead is `AudioHost`'s. `39136feea` and `d64424b95` touch other regions.
- **R15. Test binary naming.** `retroplug-audio-test` is taken (the per-channel taps, `AGENTS.md`); the new `AudioHost`
  binary is named `retroplug-audio-host-test` (report item 14).
- **R16. The approval record.** `decisions.md` records the approval of sections 1, 2, 3 and 5 as "(user, yes)" and has
  no line for section 4 (config sections, settings, startup); the presentation file carries it among the approved five.
  Report item 17 is left to the user's confirmation; the design carries section 4 as approved.

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
- **What the phase decided to leave** (the reviews', when it has them): the CLI's `MidiStream` is not wired to the
  Launchpad's reservation (report item 21: left as is) (the CLI's `ProviderHost` keeps no stream,
  `cli/main.cpp:171-219`); `reaper:*` and the plugin take nothing from this phase; the `controller` section and the
  `controlSurface` block now share one registry shape and nothing pairs a config section key with a UI service, so a
  typo is silent (F5a's pairing-check item, one more key).
- **Spec bookkeeping** is done by stage 4: `spec/07`'s F5b entry, F5b out of the open list and the per-device routing
  and rebase notes in, `spec/05`'s two new sections, `spec/06`'s product wiring and tests, and the
  `sdl-release-readiness.md` ticks.

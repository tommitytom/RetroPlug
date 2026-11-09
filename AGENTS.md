# Agent guide

Start with these — all short:

- [README.md](README.md) — what RetroPlug is, how to build, project layout.
- [spec/](spec/README.md) - the architecture (one doc per concern). The build lives in `packages/native/` (the shell:
  `src/host/` layer + `plugin/`, plus the format-neutral helpers `src/kit/` and `src/util/`) + `packages/retroplug/`
  (TS/UI), with the graph libraries in the workspace packages `packages/node-sdk/`, `packages/graph-runtime/`,
  `packages/core/` and `packages/dsp/` (formerly `native/src/graph` and `native/src/dsp`), the contract every console
  implements in `packages/system/`, the consoles' native halves beside their TS in `packages/sameboy/` and
  `packages/mesen/`, the host's own I/O nodes in `packages/host-io/`, the carts in `packages/lsdj/`, `packages/mgb/`,
  `packages/risa/` and `packages/smsggdj/`, the tracker layer they share in
  `packages/tracker/`, BlipToaster's app half and its MicroQuickJS MIDI policy in `packages/bliptoaster/`, the control
  surface in `packages/launchpad/` with the LSDj MI.MAP app that drives it in `packages/lsdj-launchpad/`, the test
  harness in `packages/testing/`, the box's device nodes in `packages/device-io/`, the device check's test nodes in
  `packages/devtest/` (listed by no product), and everything about the Everdrive N8 Pro in `packages/n8/`. The older
  **legacy** build has been removed; [spec/07-remaining-work.md](spec/07-remaining-work.md) tracks the residual
  cleanup + feature gaps.
- [docs/lsdj.md](docs/lsdj.md) — the LSDj domain reference + how to test LSDj sync /
  DAW timing / audio quality headlessly.

The React / LVGL / QuickJS framework itself is the `deps/dpf.js` submodule
(documented in its own README).

The rules below are the parts that don't fit those.

## Workflow rules

- **Don't push to remotes or open PRs without an explicit ask.** The user pushes
  their own work.
- **`dpf.js`** (DPF, lv_binding_js → LVGL/txiki, rpcpp, msgpack-c, dpf-widgets) is a
  nested git submodule at `deps/dpf.js` — clone `--recursive`. It's consumed via
  `require.resolve('dpf.js')` + `add_subdirectory` + a pnpm `link:./deps/dpf.js`;
  `pnpm install` wires the link before the first configure. lv_binding_js has its
  OWN pnpm workspace (react / react-reconciler / lvgljs-ui) — after a fresh checkout
  also run `pnpm install` in `deps/dpf.js/deps/lv_binding_js`, or the UI bundle
  can't resolve `react`. Don't bump any submodule pointer (in either repo) without
  checking — they're managed deliberately. RetroPlug also keeps `deps/sameboy` +
  `deps/mesen` / `deps/r8brain` (the shared core; `deps/enkiTS` is vendored under `deps/`, but no
  target adds or links it since the native kit compile retired), `deps/mquickjs` (MicroQuickJS, vendored: upstream's
  files unpatched with their LICENSE, built by `cmake/mquickjs.cmake`, the engine behind the device packs), and
  `deps/efsw` (the file-watcher: `add_subdirectory`'d `EXCLUDE_FROM_ALL` at the root, `efsw-static`
  linked into the shell's own `retroplug-file-watcher` library, which holds `NativeFileWatcher`
  behind `HostRpcService`'s `FileWatcher` interface; the watcher is opt-in -
  `enableWatching(std::unique_ptr<FileWatcher>)` - and only the plugin builds one and hands it over.
  `NativeFileWatcher`'s `efsw::FileWatcher` member is its LAST, so the efsw thread is joined before the state
  its callback touches goes). `deps/catch2` is
  the C++ unit-test framework:
  it's `add_subdirectory`'d at the root (`EXCLUDE_FROM_ALL`) and linked by the
  thirteen `test:plugin` binaries (`retroplug-plugin-test` / `-classid-test` / `-audio-test` /
  `-sameboy-test` / `-mesen-test` / `-watcher-test` / `-dynparams-test` / `-midi-test` / `-launchpad-test` /
  `-lottie-test` / `-graph-test` / `-engine-graph-test` / `-device-mode-test`) as `Catch2::Catch2WithMain`. `retroplug-engine-graph-test`
  holds the engine's graph to the cores it wraps: each system node renders what its core renders
  driven directly, bit for bit, its inputs, settings and params included (C2,
  [docs/modular-graph-phase-c2-design.md](docs/modular-graph-phase-c2-design.md): every setting reaches its core at
  the next block and all of them at activation, a param at the node's first block, an LSDj's capture switching in the
  block its decoder first runs in the new mode); a system node builds its core at activation from the resources its
  settings name (C3, [docs/modular-graph-phase-c3-design.md](docs/modular-graph-phase-c3-design.md)): the ROM and the
  seeds as resources, its construct-time fields, each console's twin against a directly built core, and a refused plan
  unwinding the cores and pins it built; a warmed core's whole block allocates and frees nothing, emulation included,
  and does so with a block's host input staged into it (C4,
  [docs/modular-graph-phase-c4-design.md](docs/modular-graph-phase-c4-design.md)): the ring, the scratches, the tap
  rings and the ingress queues sized from the build request's block and the capacities the inputs' sources declare,
  the audio path and then the whole block counted at ragged sizes with input staged, a freshly built core's first
  block costing only the boot savestate capture, the `[video]` hashes, a live arm
  sizing nothing, a `[input-drops]` case per queue kind, the tap a capability only a NES offers (so the tap names no
  console; with C5's cart node the engine makes no console check at all), a request that names no build function
  refused before it claims anything, and a fixture console package the engine recognised by its descriptor's category
  alone; the engine places, re-arms and frees what the graph documents
  TS builds say; the host MIDI I/O nodes move what the hosts stage and drain; the hardware nodes (C5,
  [docs/modular-graph-phase-c5-design.md](docs/modular-graph-phase-c5-design.md)): `rp.host.devices/2` over fake
  devices, a host that owns no device, a link kept across a reactivate and given back once at a teardown, and a
  warmed block touching both links allocating nothing; the capability jobs (`rp.system.memory/1`, `.sram/1`,
  `.rom/1`) on each emulated console against the system-id RPCs and the ROM file, a write completing at the block
  that publishes it, a node removed or a plan replaced under a job, the engine's 64-job bound, a real device
  thread's progress, and a warmed block with writes in flight; `rp.n8.nes` from one source receiving what
  `rp.mesen.nes` receives, in order and at their frames, its expansion volume, the note-offs a pause dropped, the
  notes it holds handed to its link when it leaves the plan (by an idle reclaim too, with no further publish), its
  jobs' busy and not-connected refusals, an abandoned job, and the node declaring no `threads` so that a Pico target
  plans it, and the same node behind a
  real Engine driving the production cart adapters to a fake serial port (`[n8-adapters]`), a host's teardown
  included; the
  surface's two nodes (a frame-0 drain, the queue's bound, the first-block discard, uniqueness, one link each, the
  generation and the drops the link reports, and the two types planning on a Pico target); and
  the UI's staged message reaching the node the running plan places under its id; the offline job pool (D2,
  [docs/modular-graph-phase-d2-design.md](docs/modular-graph-phase-d2-design.md) sections 3 and 11,
  `test/engine-graph/OfflineJobs.test.cpp`): a document run to completion on a host of its own held to the same nodes
  run directly, the pool's workers and 64-job bound, a cancel queued or running, a teardown that abandons jobs in
  flight, every refusal's reason, the RPC's samples, both kit windows and the cap's arithmetic against copies of each
  formula, the file input against `SampleCache`'s decode, the live plan unchanged beside a job and its warmed blocks
  allocating nothing, a guard count and a per-buffer non-finite flag, and a job's own floating-point environment
  (`[offline]`, with a hidden `[offline-bench]`); engine jobs, a render run on a pool worker by a `RenderHost` and an
  `Engine` of its own (D3, [docs/modular-graph-phase-d3-design.md](docs/modular-graph-phase-d3-design.md),
  `test/engine-graph/EngineJobs.test.cpp`): a job's WAV against the same render run directly, the worker budget and its
  first-in-first-out order, a cancel at boot, mid-render and after the body, an engine destroyed with renders in flight,
  a flush-to-zero submitter, continuous blocks and the carry's keys, and per-channel stems (`[engine-jobs]`, with the
  fixture-gated `[engine-jobs-hff]`; the pool's sanitizer checks are this tag built in `build-tsan` / `build-asan`);
  TS nodes run behind the C ABI in the script host (B3a,
  [docs/modular-graph-phase-b3a-design.md](docs/modular-graph-phase-b3a-design.md)): marshalling, reshape,
  creation and release on the block thread, throws, a node's promise jobs charged to it; script node packs beside the
  built-in one (C1, [docs/modular-graph-phase-c1-design.md](docs/modular-graph-phase-c1-design.md),
  `test/engine-graph/ScriptPacks.test.cpp`) run in runtimes of their own, what one does to its realm reaching no
  other pack's node and its jobs and memory limit faulting only its own, and load, reload and free while an audio
  thread runs blocks, a refused one changing neither the types a document can name nor the running plan and a reload's
  replan carrying the running plan's tap as it is;
  the protocol script nodes (B3b: the MIDI routing, a
  controller session and each role's protocol node, in the documents `buildGraph` builds, read from its role goldens
  and run in its plan order, `test/engine-graph/ProtocolNodes.test.cpp`) send exactly the digests the DSP kernel they
  replaced was recorded sending in the cases it runs, a throw stopping only its own node the one difference those
  reach, checked (the B3b design's section 9 lists the others, which no case sends a message through), but for the
  host MIDI out of MI.OUT and master sync, which differs by design: LSDj's decoder, `rp.lsdj.serial-out`, after its
  Game Boy, sends each message at its byte's frame in the block the core captured it, where the kernel sent it at
  frame 0 of the block after, so the three cases that carry it hold that decoder's recorded digests
  ([docs/lsdj-serial-out-design.md](docs/lsdj-serial-out-design.md)), and every case that records digests holds a
  second one over its system inputs alone, which the rig gave equal at the commit before the decoder moved, so the
  move changed no system input; and every document in `buildGraph`'s goldens, handed to the engine with real cores,
  plans and runs with no node faulting, no script node throwing, no message dropped and no system node's settings
  naming a field its type does not read (a C++ node ignores one, so a misspelt knob would plan and do nothing): the
  B1 translator's matrix a block each (the 33-system `perSystem` ones are refused, as they must be), and
  `golden/build-graph-roles.json`,
  `buildGraph`'s own record of documents with every protocol node type, a controller each way and every MIDI routing,
  twenty driven blocks each (the wiring itself is `buildGraph`'s, held by `pnpm test graph` to explicit tables, to the
  B1 translator's goldens and to the role goldens, which `RP_GOLDEN_UPDATE=1 pnpm test graph/build-graph-role-goldens`
  re-records), and the driven role runs hold every cart node's pushes to what the system it drives fed its own N8
  FIFO, byte for byte, so the routed MIDI and the generated bytes are pinned reaching the real cart's link.
  A hidden bench times a script node per block, and the protocol nodes' plan, at 1, 64 and 512 frames
  (`build/bin/retroplug-engine-graph-test "[bench]"`), and a profile build adds eight `[profile]` cases
  (`tools/run-profile.sh unit`).
  `retroplug-graph-test` holds the DSP library and its nodes: the kernels are chunk-exact and allocation-free in
  `process` once warmed, the resampler from its first chunk, the verbatim references of every dither algorithm and of
  both padded error diffusions, and the DPCM kernel against a verbatim copy of the retired codec (`[dsp][dpcm]`), all
  in `[dsp]`; each `rp.dsp` node matches its kernel through a real plan, a block counted from the plan's first block
  allocating nothing, a descriptor golden per type, the clamp at each widened bound with the asked value's stream
  proved different, a biquad reset when a sweep past Nyquist poisons it, and recovering, a dither's channels drawing
  their own noise and a non-finite input (a NaN, or an infinity the diffusion forms latch) restarting a channel, and
  gain/biquad/dither all plan on the Pico (`[dsp-nodes]`); and `rp.dsp.resample` plays a 22.05 kHz buffer into a
  48 kHz plan and a 48 kHz one into the kit rate, at six splits, takes a loop committed after
  its buffer ran out, carries on after a rate change, wraps a loop and clamps to silence past the buffer's end when
  re-activated and is refused on the Pico (`[dsp-resample]`), and `[dsp-buffer-nodes]` holds the three
  buffer-reading nodes to the kit arithmetic: normalize whole and split, its peak and a committed gain, a dither's
  `seedFrom` and a committed seed, the DPCM's bits and its `normalize` rebuild, a block counted from the plan's first
  block, the refusals, and the Pico plan.
  `retroplug-dynparams-test` is the odd one out: it compiles DPF's format-neutral core (`DistrhoPlugin.cpp`)
  plus its OWN `DistrhoPluginInfo.h`, building no plugin format at all, to drive
  `PluginExporter::reinitParameters` over a toy plugin.
  `retroplug-device-mode-test` holds the node SDK built for the RP2350 to the desktop's: `rp.core` and `rp.dsp` (and
  `rp.n8.nes`, `rp.launchpad.surface-in` / `-out`, `rp.device.midi-in` and `rp.devtest`, in the `[device-check]` cases;
  `rp.core.midi-filter` is a core type, held by the twin table) compiled with `RP_DEVICE`, `-fno-exceptions` and
  `-fno-rtti` into an object library reached through the C ABI, which also builds the box's link in device mode (Catch2
  itself keeps exceptions), 96 cases in eight files. A device-mode node created from msgpack settings renders bit for
  bit what a desktop-mode node renders from the same settings as JSON, with create, process and destroy allocating
  nothing; the desktop reads msgpack the same; the decode defaults a missing field, skips an unknown one and refuses
  what JSON refuses in the same words, naming the field (two inputs part by choice, pinned: design section 6); the
  converter writes each number as the JSON spells it; and nineteen `[device-check]` cases hold the image's cases to the
  manifests' device entries, run the whole check in its arena with no allocation, stop it at each of its failure steps,
  run the check's 10-node plan and drive the N8 pack's Edio, menu commands and operations over a scripted cart, and run
  the script pack's side (the devtest pack's types, a script bench, a 4-node script plan held to the desktop's digest,
  the clock a node's deadline is read from, the deadline case: a loop over a builtin in the box's pack's heap faulted by
  its deadline, and the policy case: the box's policy over the blocks that fill its heap most). The script host is in it
  as well ([docs/modular-graph-phase-e4-design.md](docs/modular-graph-phase-e4-design.md), `[script-pack]`, 50 cases, with
  `ScriptPack.test.cpp` and the fixture packs in `test/device-mode/script-packs`): a TS node run on MicroQuickJS in
  both builds of the host (the device build and the desktop's), a lifecycle, the block API, every fault (a throw, a
  deadline, a loop over a builtin held to it, a comparison's bytes and a callback's returns counted toward the next
  poll, a heap running out silencing only its pack, the engine's dropped exceptions, a getter the host reads, the
  re-entry cap), kept objects, and a warmed block allocating nothing, a plan
  exported and loaded over the device-mode builds digest-identical to GraphHost, and the loader's package memory
  (sizing, placement, refusals); `[bliptoaster]` holds `rp.bliptoaster.midi-policy` to `rp.core.midi-filter` message
  for message (the twin) and its heap to 64 full blocks; `[debug-gc]` runs the packs on an engine built to collect at
  every allocation. The engine sources are in `deps/mquickjs` (upstream's, unpatched) and the fixes are wrappers in
  `rp_mqjs_engine.c`, whose spans a configure-time hash guards; **a bump of the pin that changes one fails the
  configure** and means regenerating the wrapper, not loosening the guard. It holds the plan loader too
  ([docs/modular-graph-phase-e3-design.md](docs/modular-graph-phase-e3-design.md), `[plan]`): a document exported by
  the library's `exportPlan` (`retroplug-graph`) and loaded by `loadPlan` into an arena over the device-mode builds
  renders bit for bit what GraphHost renders, allocating nothing from load to unload, every loader refusal says its
  words, every bit of each blob golden flipped (the CRC resealed) loads into an arena of exactly the measured bytes or
  is refused cleanly (`[mutation]`; its bounds checks bite only under build-asan, which is run by hand), and the three
  blob goldens (`golden/*.hex`, recorded with `RP_PLAN_BLOB_UPDATE=1`) pin the format. `rp.device.midi-in` has four
  cases (`[device-io]`). The box's host pieces are in it as well (`[box]`: `pico/host/`'s block driver and staging,
  `BoxCartLink`, the volume write and the whole pass over a fake transport, with `plan.json`'s blob, and the
  `retroplug-export-plan` tool run as a process), and `UsbCdcTransport` over a fake TinyUSB (`[usb-cdc]`); the device
  check's digest literal is compared only on `linux-x64-gnu13`. Its configure runs three refused-source checks (a heap
  type, a pointer in `Settings` and a heap header must each fail in device mode with their own reason; libc++ skips
  the header one).
- **The graph libraries are workspace packages, and portable nodes have rules.** `packages/<name>/package.json`'s
  `retroplug` field is the manifest [cmake/packages.cmake](cmake/packages.cmake) reads; each product lists its
  packages (`retroplug_product`) and gets a generated `rp_register_packages()` (the `[packages]` golden holds the type
  ids; a configure check fails a binary that links the engine and is no product). A node meant for the Pico (a package
  entry with `device: true`) includes `graph/node.hpp` and keeps its settings to `rp::text<N>`, `rp::list<T, N>`,
  `rp::choice` (not `rfl::Literal`), numbers, bools, `rp::resource_ref` and enums (a fixed underlying type, no
  `operator|`, enumerators within -256..256; `enum_param` too), and wraps `validate`, `adjust_layout` and the helpers
  only they use in an `#ifndef RP_DEVICE`: one per node type around its desktop hooks and the helpers only they use. The desktop build compiles a `<target>-device` object library for every
  such target, so a missed guard or a non-portable `Settings` fails the ordinary build. A custom `rfl::Reflector<E>`
  is not seen by the device decode. Details:
  [docs/modular-graph-phase-e1-design.md](docs/modular-graph-phase-e1-design.md).
- **A package's TypeScript half is its `ts/` tree, and its manifest says what it contributes.** Every package with TS
  has `ts/` (sources), a `tsconfig.json` extending `tsconfig.base.json`, a `typecheck` script, `ts/test/` where it has
  tests of its own (the runner's slug for one is `<package>/<path>`) and an `exports` map with no wildcard (the API
  convention: each entry is public deliberately, and the boundary check refuses one that could publish a test module
  or names no file) where another package imports it. Adding one has a checklist, spec/06's "Adding a package", which
  is what a new pack is copied from. `retroplug.app`
  names the module that default-exports `definePackage({ roles, romProviders, trackerIntegrations, kitFormats, embeds })`
  (the lists the app used to hardcode), and `retroplug.consoles` names a module (its own, because the planner reads it
  and must carry no schema) default-exporting a package's console descriptors and the physical carts one of them can
  drive; `retroplug.protocol` names the module that default-exports a package's `DefinedNode`s beside the `nodes` (type,
  span label: 16 up with no gaps, unique) and the role kinds it places. A tracker integration carries the cart hooks the
  shell's stores and renderer reach through it (`render`, `playback`, `sramSignature`, `songImport`), so a cart's code
  is never imported by the shell: `SystemsStore.loadEmbedded(id)` takes an embedded ROM's platform from the `embeds`
  declaration the package's app module makes, and `AudioDriver.compileKitFormat(id, spec)` dispatches to the
  `kitFormats` contribution the id names. `retroplug.native` names the package's C++ target and the `extern "C"` entry a
  product registers it by (`system` has a `target` and no entry: a contract, not a node set), and `retroplug.embed`
  declares a file the package bakes in (`{ "id", "file" }`, as `mgb`'s ROM is), which `retroplug_package` turns into an
  array and `retroplug_product` into the product's `rp_embedded_resources()` table; the generated module pairs each
  manifest entry with the app module's `embeds` declaration (the id, the label and the console), refusing an unpaired
  side, an id declared twice and a platform no console registers. Both are generated registries' inputs:
  [packages/retroplug/scripts/lib/packages.mjs](packages/retroplug/scripts/lib/packages.mjs)
  writes `packages/retroplug/src/generated/{packages,consoles,protocol,scriptNodes}.ts` from
  `retroplug.products.desktop`, which
  is a **dependency order** (a package before the ones using it, the shell last) AND the registration order. Only a ROM
  provider's place in it is behaviour: the providers run in that order, and the first to match a ROM fixes a system's
  role-list order. Roles, kit formats and tracker integrations are looked up by kind, id or an exclusive marker role,
  so for those a duplicate - refused by the generated module - is the hazard, not the order. Two checks hold the
  boundary, with one deliberate allowlist between them (rule 9's `SHELL_IMPORTS`; `EXCEPTIONS` is empty, and the native
  check has none): `pnpm check:boundaries`
  ([check-boundaries.mjs](packages/retroplug/scripts/check-boundaries.mjs): a package imports only what its
  `dependencies` declare, nothing imports a shell, a cycle is refused, and - rule 9 - the app shell's own
  `packages/retroplug/src` may import only the contract modules its explicit list names, each with its reason, so a
  cart's or a console's code is reached only through the generated registries; an entry no import uses fails it, and
  `src/generated/` is not judged) and [cmake/packages.cmake](cmake/packages.cmake)'s native twin (a package's targets
  link and include only declared packages' trees). A test also walks `packages/retroplug/src` with TypeScript's syntax
  tree and refuses a platform id or a ROM-extension literal there (`test/source-literals`), with the ids and extensions
  read from the console registry, so a console's knowledge is its descriptor's alone. Details:
  [spec/06-build-test.md](spec/06-build-test.md) and
  [docs/modular-graph-phase-f1-design.md](docs/modular-graph-phase-f1-design.md).
- **A device pack is TypeScript on MicroQuickJS, and it runs the same bytes on the desktop and the box.** A package
  whose manifest has a `retroplug.script` member (`entry`, `device: true`, `heapBytes`, the box's 32-bit context) is
  built at build time by the custom target `retroplug-<name>-script-pack` (`retroplug_device_script_pack`, which runs
  `packages/retroplug/scripts/build-device-pack.mjs`: bundle, ES5, the scan for library calls the engine lacks, a
  transcript and a heap check on `retroplug-mqjs-run`, bytecode at 64 and 32 bits), and `retroplug_product` registers
  it beside the package's native entries. Its nodes use the same `defineNode`, which refuses an audio port in a device
  pack (the toolchain's scan refuses `async` and `Promise`, and the host a promise returned from a hook); `idle:
  "skip"` and the block API's `forward()` keep an idle block off the interpreter. A pack's memory is `rp_package`'s
  `memory` hook, sized and placed by the loader; a device image gets a pack from the desktop build its exporter came
  from (`RP_DESKTOP_BUILD_DIR`), and `RP_SCRIPT_HEAP_BYTES_<package>` sets its heap in one image. A node faults on a
  throw, a deadline (10 ms and 250 us a message on the box, polled finer as it nears and inside the builtins whose one
  call can run long, so a node overruns it by at most 156 units of work on the box, a call, a branch or an allocation
  each, plus the rest of the one builtin call or straight-line run the count ran out in; a collection inside a unit and
  an `indexOf` at the heap's size are unmeasured, the watchdog the backstop) or a dead heap; a throw or a deadline
  faults only that node, a dead heap its whole pack. Details:
  [docs/modular-graph-phase-e4-design.md](docs/modular-graph-phase-e4-design.md).
- **The N8 is a pack (`packages/n8`), and a product starts it through a provider, naming no N8 class.** Its portable
  target (`retroplug-n8`, `device: true`) holds the Edio protocol written ONCE, in C++ with no exceptions, over an
  abstract `Transport`, plus the menu commands, the operations (`bootRom`, `readFile`, `writeFile`), `rp.n8.nes` and the
  `CartLink` it drives; the desktop target (`retroplug-n8-desktop`) holds the serial transport, `N8Link`, `N8Host`, the
  `N8CartLink` and the RPC facet (`N8Rpc`), and the Pico box (`pico/n8-host`) runs the same Edio. A node's `threads`
  flag
  declares what the NODE needs, not its device link: `rp.n8.nes` and the Launchpad's two declare none (their links'
  threads are the providers'). A manifest entry's `provider` is how `retroplug_product` generates
  `rp_product_providers.hpp` (`rp_start_providers`, `rp_restore_providers`, `rp_stop_providers`,
  `rp_register_package_rpc`, over an `rp::Providers` each product instance holds); only a product whose sources include
  it starts the provider, so listing a pack is not starting it. TS reaches the Edio through `createN8Client(call)` in
  `@retroplug/n8` (`packages/n8/ts`: the decoders, the load helpers, sessions and the streaming link); the CLI's
  sessions
  run over the facet, and the Edio exists once. The pack's tests are `pnpm test:n8`
  (`retroplug-n8-test` and `retroplug-serial-test`, built from `packages/n8/native/test`, NOT in `test:plugin`), whose
  golden (`edio-golden.json`, 28 cases) is the protocol's byte-level oracle; the box's gate is repeated on the rig by
  hand and tests BOOT BLIPTOASTER, never `n8-midi.nes`. Details:
  [docs/modular-graph-phase-e2-design.md](docs/modular-graph-phase-e2-design.md).
- **The Launchpad is a pack (`packages/launchpad`), and the LSDj app that drives it is a bridge beside it.** The
  pack's portable target (`retroplug-launchpad`, `device: true`) holds the `SurfaceLink` contract and the two surface
  nodes (`rp.launchpad.surface-in` / `-out`: `unique`, category `device`, and no `threads`, because the threads are
  the link's). Its desktop target (`retroplug-launchpad-desktop`) holds the protocol-free transport (`ControllerLink`,
  with the RtMidi callback thread that fills its generation-tagged receive ring, the writer thread that drains the
  send ring, and a UI queue of its own so `send()` never waits for a write in flight; `PortClaim`; `MidiPortPool`;
  `RtMidiPort`), the provider (`rp_launchpad_provider`) and the facet (`LaunchpadRpc`, 12 synchronous `launchpad*`
  methods, TS's only door to a port through `createLaunchpadClient(call)` in `@retroplug/launchpad/client`).
  **`PROVIDERS` is the product's half: a product starts only the providers it names.** The list has no default (a
  provider opens ports and runs threads), the order it names them in is the start order, a duplicate is refused, and a
  manifest item may not be both an `entry` and a `provider`. The SDL standalone, the headless host and the CLI name
  `n8 launchpad`; the plugin names `n8` alone, and its configure refuses a plugin linked to the desktop target, so a
  DAW host is handed no control surface. **The provider ABI is 2:** `reserve_midi` keeps a claimed pair out of
  `MidiIo`'s shared stream (`MidiIo` reopens only what actually moves, with audio stopped and restarted),
  `suspend_hardware_inputs` is the scan's held edge (a real lever on Windows alone, where a MIDI input is exclusive),
  `restored` ends the startup rendezvous, and `rp_provider`'s `wake` is the per-frame pump (`rp_wake_providers`) that
  writes what the audio thread queued. Surface-in reports the claim's `generation` (0 = nothing attached, so a pair
  swapped inside one block still reads as a new device) and a `drops` count, both wrapped at 2^24 (the generation into
  1..2^24 - 1, so a wrap never reads as nothing attached). **TS owns the selection:** `config.json`'s `controlSurface`
  (`input`, `output`, `device`, `enabled`, additive so no migration), written through a re-read so two processes can
  share the file, and the standalone holds MIDI and audio until TS's `restored()` lands or 500 ms pass, so a restored
  pair is never opened by the shared stream first. **A controller app is a contribution in two halves:** the control
  plane's `definePackage({ controllerApps })` (`id`, `label`, the app's config schema; the shell's
  `src/controller/registry.ts` fills the registry from the generated list and adds `parseAppConfig`) and the runtime's
  `protocol.apps` entry (`module`, naming the app's factory, playback model and launch target; `syncOverride` as data
  for the cart sync mode an app needs), joined by `id`, with the generator refusing a duplicate id and the generated
  module refusing an unpaired half at load. MI.MAP is `packages/lsdj-launchpad`'s, which depends on `launchpad` and `lsdj` and which `launchpad` never
  imports. **The scenario mode is the portability proof:** `build-device-pack.mjs --scenario <file.ts>` bundles a
  package's portable core instead of a node entry, runs it under Node and on `retroplug-mqjs-run` and demands
  byte-identical transcripts in `--heap-bytes`; both scenarios run in `pnpm test` (the core's budget 76800, the
  bridge's 184320, measured, not claimed to fit the box's 64 KiB arena). The pack declares NO `script` member in F3,
  so the scenarios are the proof and no box image carries it. The pack's tests are `retroplug-launchpad-test` (one of
  the thirteen `test:plugin` binaries: the link, the claim, the pool, the provider, the facet, the port filter) and the
  facet's cases in `test:native` (`launchpad-facet`, `launchpad-devices`, over the headless host's scripted device);
  `pnpm sdl:controller` is the real standalone against an ALSA fake (Linux only, not in CI), and it runs under
  `RP_MIDI_PORT_ALLOW`, the test-only allow list of port-name substrings that hides and refuses every other port in
  the pack and in `MidiIo`. The harness writes its two fakes' WHOLE port names, whose client name (`RPFakeLPProMK3`) no
  real device has, and its listing guard fails the run before anything is probed if any other port appears or if one
  named like a Launchpad does. Details:
  [docs/modular-graph-phase-f3-design.md](docs/modular-graph-phase-f3-design.md).
- **`deps/sameboy` is patched at configure — a dirty working tree there is
  EXPECTED, not stray changes.** The per-channel (4-stem) Game Boy audio tap lives
  in `Core/apu.{c,h}` and ships as a tracked patch
  ([cmake/patches/sameboy-per-channel-audio.patch](cmake/patches/sameboy-per-channel-audio.patch));
  a second, [cmake/patches/sameboy-process-statics.patch](cmake/patches/sameboy-process-statics.patch),
  makes the core's process-wide statics safe for cores on several threads at once
  (`Core/random.c`, `Core/camera.c`, `Core/sgb.c`). Both are applied idempotently at configure by
  [cmake/sameboy.cmake](cmake/sameboy.cmake) via `git apply` (it uses
  `git apply --reverse --check` to no-op when already present). The parent repo
  tracks only the pinned submodule pointer, so after any configure `git status`
  shows `deps/sameboy` with modified `Core/apu.c` / `Core/apu.h` / `Core/random.c` /
  `Core/camera.c` / `Core/sgb.c` (all five expected): **don't
  reset / stash / commit that, and don't bump the pointer to "absorb" it.** A
  submodule bump that invalidates a patch fails LOUDLY at configure (rather than
  silently dropping what it adds); regenerate the patch if that happens. (Mesen is vendored in-tree, so its fixes are
  plain source changes, each marked `// RetroPlug:` - 45 files carry one, `Utilities/SimpleLock.{h,cpp}`'s atomic
  holder id and count for cores on several threads among them, and the per-console `#if MESEN_ENABLE_*` guards of
  `deps/mesen/CMakeLists.txt`'s compile-out are the newest batch.)
- **Don't `rm -rf build` to "fix" CMake** — investigate first. The configured
  `build/` is load-bearing for the dev loop.
- **Build via `build.sh` (Linux/macOS) / `build.bat` (Windows)** — the canonical
  entry points; they run the configure this project needs and build in parallel.
  Bare `./build.sh` = incremental; `--clean` wipes `build/`. (`--tests` /
  `BUILD_TESTING` is inert — the C++ Catch2 test binaries are `EXCLUDE_FROM_ALL`,
  built by name from the `pnpm test*` scripts, not gated on `BUILD_TESTING`.) For a
  single target after a configure,
  `cmake --build build --target <t> -j$(nproc)` — always pass `-j`, the default is
  a single-threaded slog. `build.bat` enters vcvars64 + the tool PATH and runs the
  `cl` + vcpkg-`x64-windows-static` configure (overridable via `VCPKG_ROOT` /
  `RGBDS_DIR` / `NODE_DIR`). Both scripts pass any `-D<var>=<value>` straight
  through to the configure (and force one, so the entry lands on an existing tree).
- **The Windows build is `/W4 /WX`, and CI runs the SAME compiler you do.** The
  warning block at the end of
  [packages/native/CMakeLists.txt](packages/native/CMakeLists.txt) puts `/W4 /WX` on
  RetroPlug's OWN targets only (`RETROPLUG_WERROR=OFF` opts out); the vendored subtrees
  keep their own levels. Windows is the one platform whose compiler ever sees the
  `#if defined(_WIN32)` half of a platform guard, so it is where a rotted guard shows up.
  The GNU/Clang side of this is a different lever: `retroplug_silence_dep_warnings()` in the
  root [CMakeLists.txt](CMakeLists.txt) puts `-w` on a DEP's own target; that form trips `D9025`
  on MSVC, which is why Windows suppresses per-number on OUR targets instead. Three rules when
  a Windows warning fires:
  - **Fix our code; suppress only what we do not own** — and suppress by NUMBER, pinned
    to the one header that raises it, so a new warning class still fails. You cannot
    lower a level with `/W0` or `/w`: a second `/W`-family flag trips command-line
    warning `D9025`, which is itself un-suppressable.
  - **A vendored `.cpp` compiled straight into one of our targets** (lodepng, dpf-widgets
    `LVGL.cpp`) takes a per-source `/wd` list via `set_source_files_properties` —
    per-source options are appended after the target's, so they win over `/W4`. A whole
    vendored translation unit with no reason to sit under our level gets a target of its own
    instead, kept off `_rp_werror_targets` and given the deps' `-w` on GNU/Clang
    (`retroplug-r8brain-base`, which builds r8brain's `r8bbase.cpp`).
  - **Both windows CI jobs run `windows-2025-vs2026`** (VS 2026, MSVC 14.51) to match the
    dev box. That is deliberate: plain `windows-2025` ships the same VS 2022 17.14 /
    MSVC 14.44 as `windows-2022`, and 14.44 vs 14.51 disagree about which warnings
    exist — before they were matched, a locally clean `/W4` build could still go red on
    CI (it did, on `C4127` in rpcpp). Don't "fix" that class of failure locally-only.
- **Mesen's consoles RetroPlug never runs are compiled out, by options the root turns OFF.** RetroPlug emulates the
  NES, the GBA and the SMS/GG, so `deps/mesen/CMakeLists.txt` declares `MESEN_ENABLE_SNES` (the Super Nintendo AND the
  Game Boy: Super Game Boy couples them), `-PCE` and `-WS`, ON by default as upstream Mesen builds, and the root
  CMakeLists sets all three OFF as cache defaults before `add_subdirectory(deps/mesen)` (a `-D` overrides them, which
  is what `tools/build-mesen-all-consoles.sh` / `pnpm build:mesen-all` does; its `--off` mode proves the reverse). Each
  option drops that console's `Core/<Dir>/` translation units and its CPUs' `ExpressionEvaluator.<Cpu>.cpp` through one
  table, and is a PUBLIC compile definition, so a consumer's headers see the same set. The call sites into a dropped
  console are guarded `#if MESEN_ENABLE_*` with `// RetroPlug:`; a `case` whose console is off is gone from the switch,
  and no dropped console can reach it (it cannot be constructed). A switch that had a default keeps it; a void one
  without one becomes a no-op for that CPU, and a value one falls to the code after it, which answers (`nullptr`,
  `false`, its own `return` or its throw). Nothing NES, GBA or SMS changed. A missed guard links in Release (the
  compiler folds the arm a template parameter picks) and fails at `-O0`: `tools/build-mesen-all-consoles.sh --off`
  builds the library as Debug and refuses an archive that references a C++ symbol no member defines. 288 translation
  units of 426, 19.7 MB of 32.7 MB of archive. The vendored tree is SourMesen/Mesen2 at `b9fa69d` with no `.git` of its
  own: a merge re-applies the 45 files carrying a `// RetroPlug:` marker (the compile-out guards among them) over the
  upstream snapshot, then runs BOTH modes of `tools/build-mesen-all-consoles.sh`, `--off` being the one that catches a
  guard the merge dropped (a Release link folds it away).
- **Mesen LTO is opt-in (`-DRETROPLUG_MESEN_LTO=ON`), and that is deliberate.** It
  buys ~10% on the NES core (Cortex-A53: xRT 0.83 -> 0.92), but it turns every mesen
  object into LTO bitcode, so each of the *fourteen* binaries linking that static lib
  (the plugin's three formats, the standalone, the host, the CLI, the Node addon, the
  SDL binary, `rp-golden-dump` and the five test binaries - the render host one of
  them) re-runs codegen over its objects: 288 with the consoles RetroPlug does not run
  compiled out, 426 with `MESEN_ENABLE_SNES` / `-PCE` / `-WS` all on. The cost of a
  one-line `.cpp` edit was measured before the compile-out, at nine links over 426
  objects: **1m05s wall / 26m51s CPU with it on, ~2s / ~9s with it off** - the same
  single compile, the rest full LTO links. Re-measured since, per target: compiling
  mesen takes 13 s with every console on and 8 s with them off, and relinking the CLI
  alone 11 s against 9 s. So it's OFF by default and every `release.yml`
  job passes it ON; shipped builds keep the speedup, the dev loop doesn't pay for it.
  Toggling the option recompiles mesen (~45 s). If a build with it ON seems to stall on
  every link, that's LTO link-time codegen over mesen's objects, not a
  stale-dependency bug: `[NN%] Built target mesen` prints whether or not it did work.
  (It used to announce itself with repeated `emu2413.cpp` / `-Wodr` warnings at each
  link. Those are silenced now - the per-TU `-w` can't reach link-time codegen, so
  `deps/mesen/CMakeLists.txt` puts a `-w` on the consumers' link line under LTO too.)
- **`retroplug-sdl` is in the default build** — `build.sh` builds it, and CI covers it
  (CI builds `all`) on all four platforms. It needs nothing extra installed: SDL2 is
  already REQUIRED at root scope for every plugin variant, and rtmidi is an in-tree
  submodule. `pnpm sdl:smoke` builds it by name then runs the headless smoke; that smoke
  is still NOT a CI step, so CI proves it compiles + links, not that it runs.
  `pnpm sdl:pipewire` is the audio-device half the smoke can't cover (it runs with no
  audio server at all): it stands up a PRIVATE PipeWire server + three null sinks in its own
  `XDG_RUNTIME_DIR` and asserts WHICH output device the PortAudio PipeWire backend opens —
  the session default sink (`default.audio.sink`) wins, an explicit Settings pick beats it,
  a stale pick warns and falls back, and a libpipewire-less run degrades to ALSA. That server
  runs with NO session manager: the sinks AND the `default` metadata object are declared as
  `context.objects` in a daemon-only config, because wireplumber needs logind/dbus and exits
  without them here — which silently collapsed every expectation onto the priority fallback
  when the check depended on it. The third sink (`rp-decoy`) always outranks the others on
  `priority.session` and is never the default, so "followed the default" can't pass by
  accident. It also covers WIDE output (`Out Channels` 4/6/8): 8 channels against a stereo
  sink must open, be 8 PORTS wide, and still have pair 1 linked — the port count is the
  assertion that matters, since a stream with no declared layout silently comes out 2 wide
  while the log still says 8. That leg runs LAST and is the one part needing a session
  manager (ports only materialise once something links the stream); it starts wireplumber
  with its bluetooth context dropped, because that context's logind module has no
  `/run/systemd` in a container and takes the daemon down with it, and on a private
  `dbus-daemon`, without which it quits at startup whenever the shell has no session bus.
  Not in CI (the runners have no PipeWire); the devcontainer ships one for it.
  **It ships in the `build.yml` artifact for every platform but is deliberately kept OUT
  of releases** — `release.yml`'s four packaging steps are explicit allowlists, and
  `retroplug-sdl` is not on any of them (each carries a comment saying so). Don't add it
  until the standalone is release-ready. On macOS the artifact binary is build-machine-only:
  the `SDL2.framework` embed covers the plugin/app BUNDLES, and `retroplug-sdl` is a bare
  executable, so it would need a framework copy + an `@executable_path` rpath to run
  elsewhere.
- **Never commit derived artifacts** — the embedded bundle C arrays
  (`build/native/*bundle_data.c`).
- **Config migrations (versioned, raw-JSON).** Persistence is TS-owned. Every serialized
  JSON root (project / DPF state, user config, bindings, recent) is version-**stamped**: a
  file stamped newer than the build is refused AND latched **read-only** (refusing to read it
  is not enough — the store's fallback value is the defaults, so the next write would replace
  the newer file with them); one stamped older is **migrated** up. A **malformed** root stays
  writable on purpose, so corruption heals on the next change — which is why refusal carries a
  reason (`parseVersionedRoot`) rather than one null. Keep
  only the LATEST zod schema per root — never a per-version copy. A breaking (non-additive)
  change bumps that root's version constant (`K_PROJECT` / `*_SCHEMA`) and adds one raw
  `(obj) => obj` step to its migrations map
  ([migrate.ts](packages/retroplug/src/migrate.ts)), applied to the raw JSON *before* zod
  validates; steps must be idempotent-safe. Additive-only changes still need no step (zod
  `.default()`s fill them). Two exceptions: the per-system **role-config** that crosses to
  native stays reflect-cpp `DefaultIfMissing`-tolerant (the one outlier), and the LSDj `.sav`
  carries its own binary format version. The model + stamps live in
  [spec/05-data-persistence.md](spec/05-data-persistence.md).

## Framework gotchas

**`lv_binding_js` ignores `insertChildBefore` (always appends).** React reorders
children by calling `insertChildBefore`, but lv_binding_js's
[comp.cpp](deps/dpf.js/deps/lv_binding_js/src/render/native/core/basic/comp.cpp)
ignores `beforeChild` and always appends. So swapping a component at a stable React
position (e.g. a tile → a menu) or a mid-list insert lands the new node at the END
of the LVGL child list, not its source position. Two workarounds, both already in
the code:

- **A stable per-id wrapper** whose position never changes, with a single swappable
  child (`appendChild` lands correctly when the parent has ≤1 child) — `StableSlot`
  in [SystemGrid.tsx](packages/retroplug/ui/screens/grid/SystemGrid.tsx).
- **Re-key the parent on the visible set** to force a full remount, so every child
  mounts fresh in JSX order —
  [Menu.tsx](packages/retroplug/ui/screens/menu/Menu.tsx).

If a tile / row / menu renders in a confusingly wrong slot, this is almost certainly
why. (Build-target gotchas — which target rebuilds the standalone vs the embedded
bundle — are in [spec/06-build-test.md](spec/06-build-test.md).)

**There is no font-family — the UI face is monospaced by SYMBOL SUBSTITUTION.** A
component picks a font by pixel size and nothing else: the TS style pipe turns
`font-size` into an index
([text.ts](deps/dpf.js/deps/lv_binding_js/src/render/react/core/style/pipe/text.ts)) and
native maps that index onto an `lv_font_montserrat_*` pointer
([font.hpp](deps/dpf.js/deps/lv_binding_js/src/render/native/core/style/font/font.hpp)).
That table is in a submodule and names the symbols literally, so the face is changed by
changing what the names POINT AT, not by asking for another one. Hence: every
`LV_FONT_MONTSERRAT_*` in [lv_conf.h](packages/native/src/lv_conf.h) is **0** so LVGL
compiles none of its own, `LV_FONT_CUSTOM_DECLARE` declares the seven sizes the table
uses, and `packages/native/src/fonts/lv_font_montserrat_{12,14,16,18,22,24,32}.c` supply
those symbol names built from **DejaVu Sans Mono** — regenerate with
[tools/gen-mono-fonts.sh](tools/gen-mono-fonts.sh), which also renames each generated
file's uppercase include guard (lv_font_conv derives it from the filename, so it would
otherwise be the very `LV_FONT_MONTSERRAT_*` macro now set to 0, and the file would
preprocess away to a missing symbol). Three consequences:
- **Turning an `LV_FONT_MONTSERRAT_*` back to 1 is a duplicate-symbol link error**, not a
  font change. Switch faces by regenerating, not in `lv_conf.h`.
- **The generation keeps the FontAwesome pass.** `LV_SYMBOL_*` glyphs live inside these
  same font files (the binding's `dropdownlist.cpp` draws them); drop that `--font` and
  they become placeholder boxes.
- The `.c` files are committed, like LVGL's own are, so a build needs no npm/network.
  They are added to the **`lvgl`** target in the root [CMakeLists.txt](CMakeLists.txt) —
  the archive the objects they replace came from, so nothing depends on static-link order.

The face is monospaced, but **don't let layout start depending on that**: the
action-cycler row aligns its arrows with real widget geometry rather than space-padding
for exactly this reason, and a UI test pins it.

## Verification loop

Verify your own work headlessly before claiming it's done — **a "tests pass" claim
must be backed by an actual exit-zero** from one of these.

**On Windows, run the suites through `test.bat`, not bare `pnpm test*`** (`test.bat` =
all four; `test.bat native ui` = a subset). It enters the MSVC env the `cmake-build`
step needs and pins pnpm via corepack, because pnpm strips `PATH` for spawned children
on Windows — without it the scripts die on `spawnSync cmake ENOENT`, which looks like a
broken tree but is not.

**A case that cannot run SKIPS, and the skip is counted.** `skip(reason)` (exported by
`@retroplug/testing`'s `harness.ts`, and by the CLI SDK) reports the case as TAP `ok N - name # SKIP reason`
rather than as a pass — the convention it replaced was a `console.log("# SKIP ...")` plus a bare
`return`, which printed `ok` and counted as coverage. It THROWS, so it works from inside a helper
or a loop; call it only from code the test body runs or awaits, since any `catch` between it and
the harness swallows it (which is why `toThrow` re-throws a skip). Which files may skip is pinned by
`packages/retroplug/scripts/skip-baseline.json` — generated with the sibling fixture trees ABSENT,
so it records CI's worst case and a dev box that owns the fixtures simply skips less. A file that
starts skipping, skips more than its entry, or loses cases fails the run. Regenerate with
`--update-skip-baseline` (refused on a filtered run); `RP_FAIL_ON_SKIP=1` demands zero skips.
Fixtures are reached through `__REPO_RESOURCES_DIR__` / `__RESOURCES_DIR__` / `__RISA_SRC__` / `__MGB_ROM__`, never
a path literal; pointing `__RESOURCES_DIR__` and `__RISA_SRC__` at a nonexistent dir reproduces the CI picture
locally.

**The four `pnpm test*` suites run in PARALLEL, and a slow suite is nearly always one slow
FILE, not the pool.** Each runner's unit of work is an isolated child process, dispatched by
the shared pool in
[scripts/lib/testPool.mjs](packages/retroplug/scripts/lib/testPool.mjs) — default half the
logical threads, override with `--jobs N` / `-j N` / `TEST_JOBS` (`=1` serial). Work is
dispatched LONGEST-FIRST from durations the previous run recorded in the gitignored
`packages/retroplug/.test-timings/`, so the wall clock is bounded by the slowest file rather
than by when the pool happened to reach it. That makes **one** file the thing to look at when
a suite drags: read `.test-timings/native.json`, and if the top entry is near the whole wall
clock, raising `--jobs` will do nothing. Two traps put it there, both of which cost
`test:native` a 10x factor:
- **`audio.renderAudio(ms)` takes MILLISECONDS, not frames.** `renderAudio(44100)` is 44
  seconds of emulated audio, not one. Every honest call in the suite is a plain 50–6000.
- **A constructed system that is never removed is still emulated by every later test in that
  file** — `renderAudio` advances ALL live systems in the host process, so per-file cost grows
  with the square of the boots. Remove each system once its state has been read:
  `s.project.systems.removeSystem(id)` for one the stores built, `graph.remove(id)` for one a
  `DirectGraph` placed (or `LsdjProbe.closeAll()`, which the probe-based files call at the TOP of
  each test so an early return still cleans up). A file whose realtime factor DEGRADES case by
  case has this.

The headless loop (the only path — legacy is gone) is documented in
[spec/06-build-test.md](spec/06-build-test.md): `pnpm test` (pure-TS mock),
`test:native` (real host + cores), `test:ui` (LVGL React), `test:plugin` (Catch2
C++ unit checks — the per-context window-hook routing, the class-id counter sync
that keeps the DAW-hosted editor from rendering blank, and the DPF dynamic-parameter
diff classifier), `device:check` (`tools/build-device-check.sh` - builds `rp-device-check`, the RP2350 image of every
portable `rp.core` / `rp.dsp` / `rp.n8.nes` / `rp.launchpad.surface-in` / `-out` / `rp.device.midi-in` /
`rp.devtest` type, the N8 pack's Edio, a
10-node plan exported at build time and run through the loader on a 4 KB stack, and the devtest script pack on
MicroQuickJS (its types, a bench of a block and a message, and a 4-node script plan with a deep nest, whose stack is
judged in two phases), with `arm-none-eabi-g++` and the Pico
SDK 2.1.1 (`PICO_SDK_PATH`), `-Werror=stack-usage=1024` on every compile and the desktop `retroplug-export-plan` built
first (`--packages` names the packages an image's plan may use), under a link
trap on the heap, on exceptions and on RTTI (87 wrapped symbols with arm-none-eabi GCC 13.2.1; MicroQuickJS, compiled
at -Os, passes it with `abort` wrapped) and a probe that must fail to link; built and never
run, in CI on linux-x64; needs no hardware), `device:box` (`tools/build-box.sh` - builds the Pico box's firmware,
`pico/n8-host`, which is C++ on the N8 pack's Edio and runs the built-in `plan.json` (MIDI in -> the `bliptoaster`
policy, a TypeScript node -> the N8) through `pico/host/` and the loader, with `retroplug-export-plan` built first, in
both USB variants (Pico-PIO-USB, the rig's, and `RP_NATIVE_USB`) under the same link trap over the WHOLE image,
TinyUSB and Pico-PIO-USB included, each with its own
probe that must fail to link; needs the SDK's TinyUSB submodule and a Pico-PIO-USB checkout at 5a37a66
(`PICO_PIO_USB_PATH`, default `/workspaces/Pico-PIO-USB`); built and never run, in CI on linux-x64 beside
`device:check`; the box's hardware gate is `pico/n8-host/loop.sh`, `stress.sh` and `faults.sh`, run by hand on the
rig), `screenshot`,
`reaper:editor` (`tools/run-reaper-editor.sh` — floats the hosted plugin editor in
headless Reaper and asserts its LVGL snapshot rendered; the only check of on-screen
editor rendering, not in CI), `reaper:editor-reopen` (`tools/run-reaper-editor-reopen.sh`
— loads mGB through the UI, closes + reopens the editor, and asserts the project is
still shown; mouse-driven since keys don't reach the plugin editor headlessly),
`reaper:editor-autoload` (`tools/run-reaper-editor-autoload.sh` — floats the editor with a
project already in the control plane and asserts the editor shows it, the session-restore /
setState half; deterministic, no mouse) — both guard the editor↔control-plane single-store
graph (a close/reopen or a setState-restored project showing the start menu means the UI
composed its own store again); not in CI. **`reaper:two-instances` /
`reaper:two-instances-load`** are the only checks that run MORE THAN ONE instance in a host,
which is its own state: a control-plane runtime each, and an LVGL display each on one UI
thread. The first loads N instances + N editors and watches the host survive
(`RP_TWO_FORMAT=vst3|clap`, `RP_TWO_COUNT`, `RP_TWO_AUTOLOAD`); the second drives the reported
failure — load a ROM through instance 1's editor, open instance 2, load through THAT one — and
judges by a ReaScript defer HEARTBEAT, because the bug freezes rather than crashes and a live
process proves nothing. Reaper runs defer on the same main thread both editors idle on, so a
blocked UI thread stops the counter dead; `RP_LOAD_MODE=dialog` (the "Load..." row) is the leg
that reproduced it, and the harness SIGKILLs, since a host spinning in LVGL never answers
SIGTERM. Neither is in CI. `reaper:params` / `reaper:params-clap`
(`tools/run-reaper-params.sh` — reads the plugin's parameter names through ReaScript before and
after click-loading mGB, asserting the per-ROM CC labels replaced the generic pool and the
parameter COUNT held; the only proof a host acts on the re-read flag DPF raises, and the only
check that covers the editor's parameter-map idle poll. Run it per FORMAT: the two take different
paths in DPF, and it verifies which format Reaper actually loaded, since a bare "RetroPlug" lets
Reaper pick VST3 for both). Then the `tools/run-sanitizer.sh` thread / address checks,
and `validate`. The LSDj-sync / DAW-timing / audio-quality matrix runs headlessly
too — the real-Reaper
`reaper:lsdj-*` renders + `tools/reaper-timing-analyze.py`; see
[docs/lsdj.md](docs/lsdj.md). `reaper:risa-sync` is the NES/risa twin of the LSDj drift
render (same `--drift` analyzer, whose labels are therefore tracker-neutral): risa host sync
is driven by the DAW **transport** alone, so that project carries no MIDI item at all.
To run the **whole** Reaper leg (**15 checks: 9 renders + 6 editor/host**) at once,
`pnpm reaper:all` fans them out concurrently
([tools/run-reaper-suite.sh](tools/run-reaper-suite.sh)); each job is isolated by
`RP_JOB_TAG` via the sourced [tools/reaper-env.sh](tools/reaper-env.sh) (uniquely named
JACK server + per-tag config dir / display / logs), which is also what makes the
individual `reaper:*` scripts safe to run in parallel.

**The same Reaper leg runs on WINDOWS**, via PowerShell twins that reuse the
`.rpp` fixtures, the `.lua` authors and the Python analyzer unchanged:
`pnpm reaper:win` ([tools/run-reaper-suite.ps1](tools/run-reaper-suite.ps1)),
plus `reaper:win-editor` / `reaper:win-params`. The Linux scaffolding has no
Windows equivalent and needs none - each piece maps to something simpler
([tools/reaper-env.ps1](tools/reaper-env.ps1) documents the mapping):
- **Xvfb + openbox -> nothing.** There is a real desktop.
- **jackd dummy -> Reaper's own Dummy Audio device**, `[audioconfig] mode=4`.
  `dummy_blocksize` IS the render block size, so `REAPER_JACK_PERIOD` means the
  same thing on both platforms (the midi-timing scenarios need 8192).
- **the xdotool EULA dismisser -> seeding the `[nag]` key** from the user's own
  `%APPDATA%\REAPER\REAPER.ini`. Without it a fresh config stops at the EULA
  before running the ReaScript and the run hangs with nothing on stdout.
- **`HOME=<cfg>` -> `-cfgfile`**, which on Windows relocates the WHOLE resource
  tree (Scripts/, the plugin-scan cache), not just the ini.
The one real loss is concurrency: a single desktop and a process-global audio
device mean the Windows scenarios run **sequentially**, so budget the sum. A
scenario whose ROM is absent (risa) reports SKIP, not FAIL. The analyzer needs a
REAL Python plus numpy - the `python.exe` on PATH under `WindowsApps` is a Store
stub, not an interpreter.

**Renoise is the second host** ([tools/run-renoise-render.ps1](tools/run-renoise-render.ps1)
+ the `com.retroplug.harness.xrnx` tool). It exists because every host-integration
bug this repo has hit - the class-id collision that blanked the hosted editor, the
per-ROM parameter relabelling - is a bug about how ONE host drives the plugin, and
one host cannot catch those. Renoise has no render CLI, so the tool IS the CLI: it
runs at startup, reads a job spec, drives the Lua API and quits. Two modes, and the
split is not cosmetic: `-Mode render` needs a REGISTERED Renoise (`song:render()` is
disabled in the demo, and Renoise says so with a modal dialog rather than a Lua
return, so the runner detects that in `Log.txt` and reports SKIP instead of hanging);
`-Mode probe` instantiates the plugin and dumps the parameter surface the host sees,
which needs no render and therefore works in the demo. Renoise finds the plugin
through its own configured VST path - point it at `build\bin` once in
Preferences > Plug-ins; the harness deliberately does NOT rewrite `Config.xml`,
which Renoise owns and rewrites wholesale on exit.

**The parity baseline** (`pnpm test:native parity`, [spec/06-build-test.md](spec/06-build-test.md)) holds the
engine's output to the BIT: 197 store-driven cases in 26 groups, 1-frame blocks and FL-style ragged splits among
them (the `blocks-tiny-*` groups B3a recorded first), each hashed per lane per segment plus MIDI out, SRAM
and work RAM, against `test-native/parity/baseline/*.json`. Every other audio test asserts levels, which a
refactor can move without failing one; this one fails on a single changed bit, so treat a parity failure as a
real change to the audio, never as noise. Exact audio is compared only on `linux-x64-gnu13` (this devcontainer
and CI's linux-x64 job); elsewhere a case must agree with itself and its integer state is still compared (CI's
linux-x64 step sets `RP_PARITY_REQUIRE_TARGET=1`, so there a foreign host fails instead). An
INTENTIONAL audio change re-records the groups it moves in the same commit (`RP_PARITY_UPDATE=1 pnpm
test:native parity/<group>`, one group by its full name; a prefix covering several takes the DASH form,
`parity-blocks-tiny` for the five `blocks-tiny-*` groups, since `parity/blocks-tiny` matches nothing), and the
baseline diff is the record of what moved; recording refuses silence,
order-dependent cases and an ignored stimulus. Three traps, all met building it: cores must power on in a
fixed state (`RETROPLUG_DETERMINISTIC=1`, which the runner sets for `parity/` only; the SMS VDP's random start
scanline was the last source); mGB ignores MIDI for its first ~1.5 s, so a case sends nothing before
`BOOT_MS`; and a second instance of one ROM path gets a fresh `-2.sav` battery slot, so two systems that each
need a battery each need their own copy (`rig.rom(src, prefix)`). Two engine defects are pinned on purpose (design
section 2.5 of [docs/modular-graph-phase-b0-design.md](docs/modular-graph-phase-b0-design.md)); fixing one is its
own change with a re-record, as the LSDj master-sync fix and the narrow-host NES fix were
([docs/lsdj-master-sync-fix-design.md](docs/lsdj-master-sync-fix-design.md),
[docs/modular-graph-phase-c4-design.md](docs/modular-graph-phase-c4-design.md) section 10). The fixture groups need
`RETROPLUG_RESOURCES_DIR` pointed at the sibling resources tree, and CI runs only the 152 fixture-free cases
(every LSDj case, its MIDI-out and link-cable ones among them, and every controller, risa and GBA case is
fixture-only), so a Phase B step is accepted only on a run with the fixtures present and `RP_FAIL_ON_SKIP=1`.

**The kit baselines** (`pnpm test:native kit`, [spec/06-build-test.md](spec/06-build-test.md)) hold the kit compiler's
bytes: each cart's `ts/kitFormat.ts` (lsdj's and risa's, over `@retroplug/dsp/kitJobs`) compiles each case as chains of
offline graph jobs, and the 39 synthesised cases'
banks (and 12 edge banks) are compared as FNV-1a-64 and size (and, for the 39, slot length) against
`packages/native/test/audio/golden/kit-baseline.json` and `.../kit-edges.json` (the edge records carry no slot).
Exact bytes are compared only on `linux-x64-gnu13` (CI's linux-x64 job sets `RP_KIT_GOLDEN_REQUIRE_TARGET=1` on its
`test:native` step), elsewhere a
case must agree with itself and the layout facts are still compared, and a re-record (`RP_KIT_GOLDEN_UPDATE=1`) is
refused off that host and for a case that disagrees with itself. Unlike parity it is a 4-bit view: a one-ulp change in
a dither can leave every hash matching, which is what `[dsp]`'s and `[dsp-buffer-nodes]`' references are for. The C++
`[kit-baseline]` suite went with native's compiler; that tag now exits 2 in `retroplug-audio-test`.

**The render baseline** (`pnpm test:native render-baseline`, [spec/06-build-test.md](spec/06-build-test.md)) holds the
bytes of renders run as engine jobs: 13 cases (smsggdj playing authored songs, mGB as a Super Game Boy, LSDj, risa),
each output WAV's FNV-1a-64 and size against `packages/retroplug/test-native/golden/render-baseline.json`. Exact bytes
are compared only on `linux-x64-gnu13` (CI's linux-x64 job sets `RP_RENDER_GOLDEN_REQUIRE_TARGET=1`), elsewhere a case
renders twice and must agree with itself, and a re-record (`RP_RENDER_GOLDEN_UPDATE=1`) is refused off that host, for a
case that disagrees with itself and for a run missing a case. The test-native runner gives `render-baseline`,
`render-chunks` and `render-cli` `RETROPLUG_DETERMINISTIC=1` (`DETERMINISTIC_FILES`), as it does `parity/`. The LSDj and
risa cases need fixtures, so CI runs 5 of the 13 (smsggdj and the SGB jingle): accept a render-path change only on a
run with fixtures and `RP_FAIL_ON_SKIP=1`. `render-chunks` holds the sample stream to every pull length.

**`retroplug-cli` as a consumer's whole test harness** (`pnpm test:cli-ts`):
`retroplug-cli test <dir>` strips and runs a directory of `.ts` tests, and `run <session.ts>` does one
file - so a consumer repo (BlipToaster) needs **no Node, npm, esbuild or node_modules**. Its kit is the
**binary plus tests, and nothing else**, and `sync-cli-to-bliptoaster.sh` is a ONE-FILE copy. A test
imports the SDK by the BARE specifier `"retroplug-cli"`, which the binary serves from QuickJS's
loaded-module table: cli/main.cpp `JS_ReadObject`s the embedded module bytecode (tjsc `-n
retroplug-cli`) before a session evaluates, and `js_host_resolve_imported_module` checks that table
BEFORE the module loader, so the SDK never becomes a file - nothing to sync or let go stale. A relative
specifier could not work this way: `tjs_module_normalizer` only consults the import map for BARE names
(`if (name[0] != '.')`), so `"../sdk/x.js"` always hits the disk. Only the `.d.ts` is still written out
([cli/sdkAssets.ts](packages/retroplug/cli/sdkAssets.ts)), because editors and `tsc` read declarations
from disk and reach it via a tsconfig `paths` entry; it is written only when a source actually imports
the SDK (`buildTsDir` reports `needsSdk`). Two pre-existing facts make the stripping work:
the txiki runtime **already resolves `import` off disk at runtime** (so tests never needed bundling, only
stripping), and consumer tests already use explicit `.js` specifiers, so emitting `X.ts` -> `X.js` at the
same directory DEPTH needs no specifier rewriting. That depth is why stripped output lands in the source
dir's SIBLING `.rp-test-build/` ([cli/tsStrip.ts](packages/retroplug/cli/tsStrip.ts) `buildDirFor`) - put
it anywhere else and every `../sdk/...` import breaks. Each test file runs in its **own process**
(`tjs.spawn`), which is required, not tidy: the TAP harness calls `tjs.exit` when a file ends, its case
list is module-level, and the native `Engine` is per-process. That isolation is also why the files run
**concurrently** - a bounded pool, default half the logical cores, `--jobs N` / `-j N` / `RP_TEST_JOBS`
(BlipToaster's 51-file suite: 154s serial -> 16s). Dispatch is longest-first from durations recorded in
`<outDir>/.timings.json` (inside the derived build dir, so a consumer gains no tracked file), and each
child's stdout+stderr are captured and flushed as one labelled block, since concurrent children sharing a
terminal shred the TAP. **`-j1` keeps the old behaviour exactly** - listed order, stdio inherited, output
streaming live - which is what you want when debugging one file. The pipes are drained CONCURRENTLY with
the wait; `await stdout.text()` then `await stderr.text()` deadlocks on a child noisy enough to fill the
other pipe's buffer. The stripper is ts-blank-space + the TS
parser, compiled in as global-code bytecode (+4 MB) and loaded **on demand** via `__rp_loadTsStripper`, so
no other command pays for it. **Types are stripped, not compiled**: only erasable syntax works - `enum`,
`namespace` and constructor parameter properties are refused with `file:line:col`. Wiring
ts-blank-space's optional `onError` is what enforces that; omit it and an `enum` passes through as invalid
JavaScript. (`@swc/wasm-typescript` was tried first and rejected: under the WAMR interpreter it core-dumped
on 3 of 7 real test files and silently emitted EMPTY output on 2 more, which makes a test file "pass".)

**UI/background rendering** (the `System > Render` menu; [spec/11-ui-rendering.md](spec/11-ui-rendering.md)):
the CLI `render` command and the UI render are one engine job (D3): both submit `backend.engineRun("render", spec)`,
and a pool worker runs the shared library (`packages/retroplug/src/render/`) in a bare-QuickJS `RenderHost` with an
`Engine` of its own, in continuous 1024-frame blocks. The UI keeps its list of renders in `AppStores.renders`
(`src/renderJobs.ts`). Verify with the render tests (`pnpm test:native render`, `pnpm test render`,
`pnpm test:ui render-badge`), `[engine-jobs]`, and `retroplug-render-host-test` (built by name - `cmake --build build
--target retroplug-render-host-test`), a manual smoke that CI does not run: `<job-json>` runs one render as an engine
job (`cmp`-identical to `retroplug-cli render`, the same code); `--parallel <j>...` runs jobs concurrently on one pool;
`--cancel <j>` cancels mid-render. Build it in `build-tsan/` for a ThreadSanitizer pass.

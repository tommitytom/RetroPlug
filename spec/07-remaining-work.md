# 07 — Remaining work

The port is complete: the legacy build is gone, the transitional target suffix is dropped, and the
plugin identity is the canonical `RetroPlug`. What is left is **feature work, not migration** — a
short list of gaps, deferred features, one pending refactor, a code-comment cleanup backlog, and the
node-graph engine's next steps (section 5). This is the authoritative inventory; the other docs note a
gap in a line and point here.

The runtime architecture is [01-architecture.md](01-architecture.md); nothing below but section 5
changes it.

---

## 1. Feature gaps

Things a user could reasonably expect that aren't wired yet. None block shipping.

### Raw LSDj Keyboard mode (LsdjSyncMode 4)

`lsdj-sync` implements every sync mode except raw **Keyboard**: MidiSync, MidiSyncArduinoboy, MidiMap,
KeyboardMidi and MidiPassthrough ship in
[script/nodes/lsdjSync.ts](../packages/retroplug/src/script/nodes/lsdjSync.ts), the role's protocol
node, and MidiOut (ArduinoboyMaster) and MasterSync in
[script/nodes/lsdjSerialOut.ts](../packages/retroplug/src/script/nodes/lsdjSerialOut.ts), the role's
second node, after its Game Boy. Raw Keyboard sends nothing because it needs a per-block **`keys`
feed**, which nothing supplies yet: `rp.lsdj.sync` has no input for host keys (the DSP kernel carried
empty `buttons` and `keys` arrays until B3b deleted it), and no host stages any. A `keys` input on the
node, and a host I/O node that feeds it, unblock this mode. (See
[04-roles-protocol-nodes.md](04-roles-protocol-nodes.md).)

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

There is no live "watch RAM" streaming path. Reads are one-shot through
the [`SnapshotRegistry`](../packages/native/src/host/engine/SnapshotRegistry.hpp) read door, and the
consumers that want live memory poll it: the LSDj overlay per frame, and the HD player
([useLsdjHdSession.ts](../packages/retroplug/ui/screens/hd/useLsdjHdSession.ts)) on a frame divider,
because a full 128 KiB WRAM copy plus a song decode is too expensive to do every frame. So this is a
cost question, not a capability one - an arming seam would let those two stop copying the whole image
to read a few hundred bytes of it. Nothing of the old mechanism is left to switch on: `enableMemorySnapshot`,
`disableMemorySnapshot`, `memorySnapshot`, `publishMemorySnapshots` and the nine-slot array behind them had no caller and
published nothing, and C4 deleted them.

### MIDI **out** is capped at 4 bytes

MIDI **in** takes every size (`PluginDSP.cpp:319` reads `dataExt` when a message outgrows the inline
`data[4]`), so a DAW can upload an N163 wave the way the CLI does. The **out** path drops anything
longer: `PluginDSP.cpp:363` skips events over `MidiEvent::kDataSize`. Lifting it is not a one-line
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

None known. The last entry, [host/graph/SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)
(`GameBoy::apply`), pointed at a comment that is load-bearing (why the fast boot is applied before the model)
rather than at a deferral; its marker died with `applyConfigField`.

Not cleanup (intentional domain terms that read like TODOs): `deferredProject` in
[systemsStore.ts](../packages/retroplug/src/systemsStore.ts) and `kind: "deferred"` in
`fileSelection.ts` are the sibling-`.rplg` load-handoff concept, not incomplete work.

---

## 5. The node-graph engine: after B3b

Phase B of [modular-graph-design.md](../docs/modular-graph-design.md) moves the engine onto the graph in
steps, each held bit for bit to the parity baseline ([06-build-test.md](06-build-test.md)). B3a
([modular-graph-phase-b3a-design.md](../docs/modular-graph-phase-b3a-design.md)) made the DSP kernel's
input and output real wires (host I/O nodes, `serialIn` / `midi` / `bytes` / `buttons` inputs on every
system node, the Game Boy's `serialOut`, `rp.n8.cart-mirror`) and ran the kernel as one TS node in the
script host every Engine embeds. LSDj master sync was then fixed on its own
([lsdj-master-sync-fix-design.md](../docs/lsdj-master-sync-fix-design.md)): a Game Boy's serial-out capture is
a mode, `listen` for master sync and `drive` for MI.OUT, and the decoder sends a clock per byte and a stop after a
200 ms silence timed on the bytes' frames. B3b
([modular-graph-phase-b3b-design.md](../docs/modular-graph-phase-b3b-design.md)) split the kernel into one
protocol script node per role beside the MIDI routing and the controller session, moved `kernelProjection`
into `buildGraph`, took zod out of the script-node bundle (78 KB, from 660), gave `rp.host.midi-out` each
input's system from its settings, and deleted the kernel. Then LSDj's serial out got a node of its own
([lsdj-serial-out-design.md](../docs/lsdj-serial-out-design.md)): `rp.lsdj.serial-out`, after the Game Boy,
decodes MI.OUT and master sync in the block the core captured the bytes and sends each message at its byte's
frame, `rp.host.midi-out` merges its inputs by frame, the decoders allocate nothing, and the profile's
`apu-render` spans the plan. What is left, in order:

- **Script node packs, built in Phase C1**
  ([modular-graph-phase-c1-design.md](../docs/modular-graph-phase-c1-design.md)): each pack of script node types
  runs in a QuickJS runtime pair of its own, the built-in bundle the first, packs load and reload while audio runs
  (`loadScriptPack`, on every host), and a node pays for its own promise jobs (run at a checkpoint after each call
  into it) and its pack's memory limit. Nothing in the app loads a pack yet. What it leaves (its sections 5, 12 and
  13):
  - **Isolation inside a pack.** Nodes of one pack share its realm, one author having written them all: one node's
    code can still change what another's reads through (an accessor on `Object.prototype` or `Array.prototype`, a
    global, a definition in `__rp_scriptNodes`), and a field a node deletes puts the host's next write of it back
    on the chain ([02-native-host.md](02-native-host.md)). Freezing a pack's intrinsics after its load (an SES-style
    lockdown) would close it, at the price of code that writes over an inherited property.
  - **A node's state across a reload.** A reloaded pack's nodes restart clean; a `snapshot` / `restore` pair would
    carry it.
  - **The authoring loop, the pack format and downloads, a pack's UI half** (the design's section 8), **unloading a
    pack**, and **loading one into more than one Engine**: a render job's Engine has the built-in pack only. A pack
    compiled from source keeps its debug information, about a quarter more memory than stripped code; whether a
    downloaded pack's is stripped is the pack format's to say.
  - **A pack's memory limit counts garbage.** "Past the limit" is past it in reachable and uncollected bytes
    together: an allocation past it is refused at once, with no collection first, and QuickJS's own cycle GC runs
    only as an object is made, once the heap has grown half again since the last one. So a pack whose reachable heap
    is above about two thirds of its limit can be faulted for garbage, and a profile window that pins the auto-GC off
    (`disableAutoGc`) gathers garbage until the limit faults a node. The default limit leaves room (64 MiB, where the
    largest test pack peaked at 2.55 MB).
  - **A node whose settings check fails refuses every document placing it.** The planner checks every placed node's
    settings at every plan, and a node whose check fails (its `parseSettings` or `layout` throws, runs past the
    deadline, refuses the settings or reaches its pack's memory limit, or its pack's control-side promise jobs do not
    settle) makes the document refused, as settings it refuses always have. So while such a node is placed no other
    edit to the graph lands either, the running document published again among them, nor a reload of another pack
    whose nodes the running document places beside it (refused as "the running document does not plan with the
    reloaded pack: " and that node's error); removing the node (every node of its pack, for a chain that never
    settles) or reloading its pack clears it ([02-native-host.md](02-native-host.md), the script host). A per-node
    "invalid" state, the document planning without the node, rather than a refused document is a choice for the
    authoring work.
  - **A retired generation waits for the host's reclaims.** Under an audio thread, a generation a reload replaced is
    freed at the host's next reclaim after its nodes' records come back, which they do once a reclaim has freed the
    plan that held them and a block has released them. The plugin and SDL reclaim only inside `setGraph` and
    `loadScriptPack` while their audio runs, so the generation the last reload replaced, its two
    runtimes, stays until later calls have done that; an idle reclaim in the plugin and SDL would free it sooner.
  - **A record the release ring back cannot take keeps its pack for the host's life**, as it leaks its record rather
    than free it on the audio thread; nothing has filled that ring. A profile build's absolute live heap leaves out
    what the load of any pack loaded beside the built-in one allocated (its deltas are exact).
- **Live knobs as system node settings and params, built in Phase C2**
  ([modular-graph-phase-c2-design.md](../docs/modular-graph-phase-c2-design.md)): each system's knobs are its node's,
  a setting in the published document (a Game Boy's model, display group and LSDj capture, a NES's region, accuracy
  switches and APU latency) or a param (every system's gain, a NES's expansion volume), its value in the document and
  its live edits through `setParam`; node.hpp's `rebuild_key` lets the system nodes prepare statelessly, so a knob
  edit never makes `setGraph` wait; and the side path (`ConfigField`, `Engine::applyConfigField` and its casts, the
  `applySystemSetting` / `applyRoleConfig` / `setSerialOutCapture` RPCs, the build spec's capture) is gone. A Game
  Boy's capture rides the document that hands its decoder the mode, computed from the mode that decoder runs, so an
  LSDj mode switch is atomic (retiring the master-sync design's first accepted limit, its section 10) and a cart the
  MI.MAP controller forces captures nothing (closing the B3b item below). Strict parity held bit for bit at its five
  commits; the later gain snap's 28 re-recorded cases are its section 10's. What it leaves (its sections 9 and 10):
  - **DAW automation of node params.** The plugin's parameters are not mapped onto `setParam`; params make it
    possible, and it is its own work.
  - **The plugin's sync latency report** (`__rp_syncLatencyMs`) still follows the stored `lsdj-sync` mode, not the
    one the MI.MAP controller forces.
  - **The construct-time fields and the facts a build reports: closed by C3.** SMS `enableFm`, NES `sdRoot`, the
    FIFO profile and its overrides, the N8 link and its round trip and `channelExportMode` are their system node's
    settings now, and the node builds the core from them; the export mode and a Game Boy's link group come from the
    system's core config in TS, where `buildGraph` reads them for its wiring.
  - **The knobs that allocate on the block thread.** A Game Boy's model change restarts the core, which allocates, as
    it did on the command ring, and a NES's region change resets its core, which allocates too. So "no allocation"
    holds for a Game Boy's knobs, the ones it applies in place, a NES's setters but its region, and every node's params
    ([02-native-host.md](02-native-host.md)). A warmed block itself allocates nothing since C4, Mesen's emulation
    included, and since the input-queue stage a block carrying host input does too: the ingress queues are bounded and
    sized when the core is built. The exceptions are the 0.5 s state publish and the N8 firmware's SD path, listed
    under C4 below.
  - **A system node ignores a settings field it does not know**, as every C++ node does: a misspelt knob plans and
    does nothing. `BuildGraphGoldens.test.cpp` holds the goldens' system nodes to their schemas' fields; a strict
    reader would refuse it at plan time instead.
  - **Enum labels have two spellings on the C++ side.** A system node's enum settings read the role configs' labels,
    the enumerator's name with its first letter lowercased (`"cgbC"`, `knobs::Label` in
    [SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp)), where node.hpp's `enum_param` labels are
    kebab-case (`"cgb-c"`), so a knob moved from a setting to an enum param would change the word a document carries.
- **System nodes build their own cores, built in Phase C3**
  ([modular-graph-phase-c3-design.md](../docs/modular-graph-phase-c3-design.md)): a system node's settings name its
  system, its ROM, its seeds and its construct-time fields, the bytes as resources (`putResource` /
  `releaseResource`, pinned through `rp.host.resources/1`), and the node builds its core at activation through
  `rp.host.systems/1`; the old build call, the parked cores, the construct blob and its decoders are gone, and a
  construct-time edit rebuilds the system at once, carrying its battery. Strict parity held bit for bit at its five
  commits, nothing re-recorded. What it leaves (its section 10):
  - **A stale tap record can still match a fresh core** allocated where the armed tap was freed: `armedTap_` compares
    the tap's address (a NES's tap is the core object, a per-class constant offset from the core pointer, so tap
    equality is core equality within one class), and the record survives a drop and reclaim. TS never gets there (its
    ids only go up, and every edit that drops a core re-arms), and the suite pins only the new-id replacement. C4
    restated this at the tap's address and left it as it is.
  - **A node's first block is silent**, so the gain's first-value snap is observable only through a savestate seed
    taken mid-note, which is the case that pins it.
  - **The unnamed Master System stem is `system-<id>`**: unique within a Project, not across the several Projects
    one process holds, so two projects' unnamed system 1 still name one battery. TS always sends a real `romName`.
  - **A stored value the role schema would repair leaves the node's default** rather than the repaired value: there
    is no blob to carry it, so `systemKnobs` drops it and `adopt` notes it.
  - **Each system holds one more full copy of its ROM.** The `rom` resource a node pins for the system's life is on top
    of the vector the build hands the core and Mesen's own copies: a 32 MB GBA ROM is +32 MB per system, per plugin
    instance. And the plugin and SDL reclaim only inside `setGraph` and `loadScriptPack`, so a removed system's core,
    ROM and seeds stay until the next one of those (the core always lived that long; the bytes are new). Phase C's
    P3 (below) is the piece that would let a core borrow the pinned bytes.
- **C4 built: the tap and the build check name no console, and no allocation in a warmed block**
  ([modular-graph-phase-c4-design.md](../docs/modular-graph-phase-c4-design.md), six commits): `SystemBase`'s dead
  surface is gone, a Mesen core collects its audio in a fixed ring sized from the build request's `maxBlockFrames`, the
  tap is a `ChannelTap` capability any core may offer, and the node's build compares `kind()` with its core's `kKind`
  instead of casting. A warmed core's whole block allocates and frees nothing, Mesen's per-frame video and input
  plumbing included, and a block carrying host input does too: every ingress queue is a bounded ring sized when the
  core is built, and a push past its bound is dropped and counted. Strict parity held bit for bit at every commit,
  nothing re-recorded; the narrow-host defect the B0 design records was fixed afterwards, in a follow-up of its own
  that re-recorded the two groups it pinned (section 10 of the C4 design). The one console name `Engine` still made was
  the N8 cart mirror's (`SystemKind::MesenNes`, NES by construction), which C5 removed; what is left is the tap's
  1-to-3 mode bound, a number TS owns. What it leaves (its section 12):
  - **P3, cores borrowing their node's pinned ROM bytes**, and **P4, the snapshot registry's double copy** (section 3
    above), and **P7's remainder, the full capability split of `SystemBase`** and one node file per console, which C5
    built three interfaces of (`memory/1`, `sram/1`, `rom/1`) and left video, buttons, debug and `state/1` of.
  - **The N8 firmware's SD path still allocates**: `std::string` and `std::filesystem` in the command handlers, a
    bigger directory listing, and `_txQueue`'s unbounded growth when a ROM writes CMD_USB_WR and no host drains it.
    Bounding the queue would change the "never drop a byte the device would deliver" contract. Nothing in the block
    tests drives an SD command, so it is ROM-dependent.
  - **The 0.5 s state publish allocates** inside Mesen's streaming serializer on the block thread (every 24 k frames at
    48 kHz). It is the one allocation a long run makes that neither a ROM command nor an armed serial-out capture
    triggers, so every `[alloc]` case on an Engine-built core charges a whole interval uncounted first. The same capture
    is what a freshly built core's first block pays: `enableStateSnapshot` arms it, and it cannot move to activation
    without moving `NesApu::EndFrame` and parity, so the first block is pinned to exactly one capture (C4's section 12).
  - **A thread's first nested notification send allocates**: the top-level dispatch copy is a `thread_local` object,
    so a console's first frame allocates nothing, but a listener that sends its own notification takes a pool entry on
    first use. This host registers no listeners.
  - **The input-drop counters are not surfaced beside the graph's.** Each core reports `inputAccepted()` /
    `inputDropped()` and the N8 FIFO's `coreTransportStats()` adds `droppedBytes`, but `rp_process` carries no
    instance-drop pointer, so nothing reports them where `GraphHost` counts its per-instance output drops. An
    input-drop service on the node seam is its own change.
  - **Two output-side allocations remain**: a NES's `_txQueue`, a `std::vector` a debug `drainCoreBytes` RPC swaps
    empty, so a non-link ROM's next `CMD_USB_WR` after a drain grows it; and a Game Boy's `serialOutLog_`, cleared
    each block but grown to the largest block's capture while MI.OUT capture is armed. Neither is on the steady-state
    path the `[alloc]` cases hold.
  - **The tap's mode bound is the engine's, not the capability's**: `Engine::setGraph` still validates 1 to 3, the NES's
    numbering, because TS owns it and the arm travels as a byte. `maxMode()` on `ChannelTap` is the later move.
  - **A split routing on a host too narrow for it still arms the tap.** The block is the mix's and the core steps on
    the mix's clock, so nothing the host hears depends on it, but the capture, its resamplers and the per-block
    discard are paid for. Not arming a split that does not fit is a CPU-only change (`buildGraph` knows the lane
    budget) and is nobody's bug.
  - **The cores this fork does not run** (PCE, SNES, WS) still copy a frame's snapshot once more per emulated frame.
  - **A Game Boy's press queue drops the tail of a quiescent flood.** Its 1024 entries hold sixteen blocks of a
    saturated buttons input, and its presses are scheduled 10 ms apart and carried across blocks, so unlike a Mesen
    console's they cannot coalesce. A session that applies pushes inline between blocks (the CLI, an offline render)
    can queue more than that; what is past the bound is dropped and counted, and the tail can be a release, leaving
    its button held. A rule that keeps a release, or headroom reserved for one, is its own change.
  - **The suite keeps its console-only casts** (`EngineGraph`, `SystemNode`, `HostIo`, `EngineSampleRate` tests read a
    config field, `linkPeers_` or a ring counter); no tap read casts any more.
- **C5 built: hardware as nodes, and the engine names no device**
  ([modular-graph-phase-c5-design.md](../docs/modular-graph-phase-c5-design.md), seven commits): the N8 cart and the
  Launchpad are nodes (`rp.n8.nes`, `rp.launchpad.surface-in` / `-out`) that bind the host-owned device through
  `rp.host.devices/1`, the operations an emulated system and a cart share are asynchronous capability jobs
  (`rp.system.memory/1`, `.sram/1`, `.rom/1`), and the cart mirror, the controller channel, `stageSystemMidi`, the
  `__rp_n8*` operation hooks and `N8Host`'s SD worker are gone. Strict parity held bit for bit at every commit,
  nothing re-recorded. What it leaves (its section 11):
  - **P7's remainder**, above: video, buttons and debug are still `SystemBase` virtuals, `state/1` is still the
    system-id RPC, and `SystemNodes.cpp` still holds all four consoles' nodes.
  - **A cart's battery is reachable only at the N8 menu**, through the SD card's save file (`EDN8/gamedata/<rom>/
    bram.srm`): a dump takes about a minute for 64 KB, a running game refuses the job (a direct cart-memory read
    wedges it and leaves the menu deaf), and a restore goes before a `rom.upload` so the game loads it at its next
    boot.
  - **`rom.header` / `rom.title` are what this host last booted**, stale after a power-cycle or a game started from
    the cart's own browser; the cart has no query for the image it runs.
  - **No project-level fallback forwards DAW MIDI to the cart without a NES system**: the cart is placed by the
    "drive the real cart" setting now, and the start menu's N8 card rows are gone (a card job addresses a node, and a
    project with no system places none). Driving a MIDI cart with no NES in the project has no UI path.
  - **The harness's controller seam needs a surface link**: `stageControllerIn` and `renderBlocks`' controller field
    work only where a host installs `HarnessSurfaceLink` (the test host), and a harness-facet host with none refuses
    them rather than staging into nothing.
  - **The CLI's n8 tools keep their own raw serial path**, the battery dump among them: it is the one cart-memory
    read left, and the capabilities deliberately offer no peek, poke, sniffer or FIFO reads.
  - **A cart job pauses streaming**: pushes during it are dropped and counted, the node re-sends the note-offs it
    lost, and a `rom.upload` leaves streaming stopped until the host reconnects. A job that a running game will refuse
    never pauses: its submission reads the cart's config block first, so a game's notes keep flowing.
  - **MIDI streamed to a cart at its menu reaches the menu's own command parser** (pre-C5, unverified on hardware):
    the menu takes commands over the same FIFO, so a MIDI data pair such as `0x2A 0x73` (`*s`) sent while the menu
    shows can ask it to start an app. It holds for raw MIDI after a connect at the menu and for framed MIDI after a
    console reset, where the stale window is re-sent every 100 ms until it re-syncs.
  - **The quiet rule assumes the N8 menu writes nothing unprompted on the back-channel** (unverified on a cart):
    unsolicited bytes would keep `lastHeardNs` fresh and make a menu with stale frames read as never quiet.
  - **A heavy stream into a menu can starve the probe**: the window's 48-frame re-sync re-stamps the oldest frame's
    first send, so past about 190 messages a second (48 per 250 ms) into a menu no turn is quiet and a submission is
    refused for a link that never went quiet.
  - **A retained generation of a script pack, the 0.5 s state publish and the N8 firmware's SD path** are the
    allocations left on the block thread, as C1/C4 list them above; C5 added none.
- **Left by LSDj's serial-out node, as they are** (its design's sections 11 and 12):
  - **The core's serial byte timing moves with the block split.** The frame SameBoy completes a captured byte at
    moves by tens of frames from one split to another (up to 32 in `lsdj-mastersync` as the decoder's prototype
    measured it, the whole stream shifting and its clock gaps the same in every split; the test logs the gaps, not
    the shift), which predates the decoder's move. The decoder adds no split dependence of its own, so the tests
    hold each message to its byte, not to one absolute time shared by every split. A SameBoy matter, its own change
    if wanted.
  - **MI.OUT decodes about half the NoteOns a song plays.** dsp-bench's MI.OUT window sends 20 NoteOns in 400
    blocks (9.3 s), where its phrase's eight notes at tempo 128 come to about 40. Likely MI.OUT's rate-dependent
    decode, one of the defects parity pins ([06-build-test.md](06-build-test.md)); not looked into.
  - **No DAW check records LSDj's host MIDI out.** What MI.OUT and master sync send is held at `Engine::midiOut`
    (the native tests, parity's MIDI-out hashes), never as a DAW records the plugin's `writeMidiEvent`: the Reaper
    legs (`reaper:lsdj-*`) drive LSDj from the DAW and analyse the audio. A leg that records the plugin's MIDI out
    would close it.
  - **The SDL standalone sends host MIDI at block time.** Its RtMidi out takes no timestamp, so `audioCb` sends
    each block's `Engine::midiOut` at once as the block is rendered, its frames dropped, where the plugin hands the
    DAW each message at its frame (`writeMidiEvent`). Scheduling it is the standalone's own work.
- **Found while building B3b, left as they are:**
  - `defineNode` keeps to ES2015's library (`Set`, `Object.entries`, `includes`), so the script-node bundle
    holds no zod but is not yet MicroQuickJS-ready; `settingsSchema.ts` already keeps to ES5's.
  - `routeBlockInto` in [midiRouting.ts](../packages/retroplug/src/midiRouting.ts) has no caller since B3b:
    the routing node routes each message with `routeTarget`, the same arithmetic, and `routeBlock`, its pure
    twin, stays as the reference the routing node's test holds every mode to. It can be deleted.
  - `DirectGraph.setPipeline` hands a node its role's config as given, where a store hands the config its
    schema parsed, and a node's settings fill their own defaults: a direct caller's `lsdj-sync` with no `mode`
    is MIDI sync, where the kernel read a missing mode as nothing.
  - **Readers that decide "is this system LSDj" (or mGB) by a role's presence, not its placement.** In the rare
    case a ROM path changed between an LSDj and an mGB build, a system holds both `mgb` and `lsdj-sync`, and the
    pipeline places one (the one the ROM calls for). The graph is right; these readers are not:
    `trackerIntegrationFor` ([trackerIntegration.ts](../packages/retroplug/src/tracker/trackerIntegration.ts), by
    `markerRole` presence), so the LSDj menu's sync cyclers
    ([menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts)) edit a left-out `lsdj-sync` on an mGB cart;
    `hasRole(..., "lsdj-sync")` in `ui/App.tsx`, `ui/screens/grid/useLsdjRuntime.ts`,
    `ui/screens/hd/useLsdjHdSession.ts` and `projectStore.ts` (which tests `roles.some((r) => r.kind ===
    "lsdj-sync")`); and `projectParameterMap`
    ([parameterMap.ts](../packages/retroplug/src/parameterMap.ts)), which still exposes mGB's CC lanes for an LSDj
    cart carrying a stale `mgb`. The fix is `placedRole(...)`
    ([graph/protocolNodes.ts](../packages/retroplug/src/graph/protocolNodes.ts)) in those readers. Not fixed.
  - **The controller's MI.MAP override changed only the document: closed by C2.** `controllerSyncOverride`
    ([graph/controller.ts](../packages/retroplug/src/graph/controller.ts), applied in `buildGraph`) runs an LSDj's
    nodes in `midiMap` for a cart reading MidiMap (in `off` for one reading another SYNC), and `serialOutCaptureFor`,
    beside it since C2, computes the Game Boy's capture from that forced mode, so a forced cart captures nothing. Only
    `__rp_syncLatencyMs` still follows the stored mode, listed with C2's leftovers above.
  - A few test-native case names still say "the kernel" (in `dsp-lsdj-midisync`, `dsp-risa-sync`,
    `dsp-sms-sync` and `plugin-controlplane`), where they mean the protocol nodes. Renaming
    a case is harmless but was kept out of B3b, which changed no test's identity it did not have to (the one case
    `dsp-serial.test.ts` renamed, because its name described the deleted kernel, is the exception).
- **Found while building B3a, left as they are:**
  - `rp.host.midi-out` appends one `std::vector` per message to the engine's host output
    (`Engine::midiOut_`, the `rp.engine.host-io/3` service in
    [Engine.cpp](../packages/native/src/host/engine/Engine.cpp)), an allocation on the block thread for
    every message out; a flat arena, as the staging has, would remove it. The surface's output node hands each
    message straight to its link, so it allocates nothing.
  - The script node `Block` ([defineNode.ts](../packages/retroplug/src/graph/defineNode.ts)) has no
    resource accessor and no link peers yet.
  - With commits to script nodes never making `setGraph` wait (their prepare is stateless), the
    GraphHost's command ring (256 slots, 255 usable, shared with params and the engine's commands) is what
    bounds edits while no block runs: a host that stops calling `process` while the plugin stays active
    queues one plan per edit, each holding its buffers, and once the ring is full `setGraph` answers busy.
    The publisher keeps its model, so the next publish sends its document even if unchanged and the latest
    model lands. Coalescing queued, unadopted plans is the alternative, should it bite.
  - The adoption wait itself is global: one commit to a type without `RP_TYPE_STATELESS_PREPARE` holds back
    an edit that touches only stateless nodes (a gate per instance is possible). And every `node.hpp` node
    without `needs_rebuild` already prepares statelessly, so for C++ nodes the flag could be derived rather
    than declared.
  - A Game Boy that clocks its serial port itself, a CGB on its fast clock, can send more in a block past
    about 12000 frames (6000 in double speed) than its fixed `serialOut` holds; the rest is dropped and
    counted ([SystemNodes.cpp](../packages/native/src/host/graph/SystemNodes.cpp) `kSerialOutCapacity`).
  - `rp.lsdj.serial-out`'s `midiOut` (the kernel node's before B3b, then `rp.lsdj.sync`'s) is sized for a
    decoded MI.OUT stream at its densest up to 8192-frame blocks (1024 messages, a captured byte every 8 frames),
    while `serialOut` is sized for 65536: in a larger block the decoded stream can pass 1024 messages, and the
    rest is dropped and counted ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts) `MIDI_OUT`).
  - `rp.host.midi-in`'s sort at the source merges two frame runs in linear time, which is what real
    plugin hosts stage (the DAW's events, then the automation CCs at frame 0); a staging of three or more
    runs falls back to an in-place insertion sort, quadratic in what was staged, on the block thread. The
    staging has no count cap either (past its reserved room it grows), so a host that stages a great many
    messages out of order pays for every one, though the input node's queue keeps 2048
    ([Engine.cpp](../packages/native/src/host/engine/Engine.cpp) `Staging::takeSortedInto`). A cap at what
    the input node carries, or a merge of k runs, would bound it.
  - A feedback source whose queue capacity changes while an audio thread runs gets a fresh, empty front:
    the block in flight is lost, and `droppedMessages` never counts it, since the carry and its count run
    only while quiescent ([Planner.cpp](../packages/native/src/graph/Planner.cpp) `front`). No port changes
    its capacity live today, and no document `buildGraph` builds holds a feedback wire: the Game Boy's `serialOut`,
    fixed, was the one source until LSDj's decoder moved after it.
  - `retroplug-engine-graph-test` replaces the global `operator new` with a counting allocator
    ([CountingAllocator.cpp](../packages/native/test/graph/CountingAllocator.cpp)) that allocates through
    `_aligned_malloc` on Windows, so a core or QuickJS freeing what `new` made with `free()` would crash
    there; no Windows CI run has exercised it yet.
  - `retroplug-ts-stripper-bundle` fails in a build directory outside the repository:
    [tools/build-ts-stripper.mjs](../tools/build-ts-stripper.mjs) runs in the binary dir and sets no
    esbuild `absWorkingDir`, so its `typescript-5` alias resolves from there (unlike
    [tools/build-ui.js](../tools/build-ui.js), which sets it to the repo root).

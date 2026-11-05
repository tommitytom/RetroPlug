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
    plan that held them and a block has released them. The plugin (with its editor open) and the SDL standalone
    reclaim at every frame of their idle tick, so there it goes within a few frames; a plugin whose editor is closed
    reclaims only inside `setGraph` and `loadScriptPack` while its audio runs, so the generation the last reload
    replaced, its two runtimes, stays until later calls have done that.
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
    sized when the core is built. The exceptions are a Mesen core's 0.5 s state publish and the N8 firmware's SD path,
    listed under C4 below; a Game Boy's publish of the same model allocates nothing (a model switch's restart block
    also grows its capture scratch).
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
    instance. A removed system's core, ROM and seeds go at the next reclaim: within a frame in the SDL standalone and
    in a plugin whose editor is open (their idle tick reclaims, C5), and at the next `setGraph` or `loadScriptPack` in
    a plugin whose editor is closed. Phase C's
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
  - **A Mesen core's 0.5 s state publish allocates** inside its streaming serializer on the block thread (every 24 k
    frames at 48 kHz), and a Mesen cart with a battery adds a fresh vector for its SRAM read at the same cadence
    (`SnapshotRegistry::publishSystem`'s `sramScratch_ = saveSramBytes()`, which would become a fill into a buffer sized
    at claim). Those are the allocations a long run makes that neither a ROM command nor an armed serial-out capture
    triggers, so every `[alloc]` case on an Engine-built Mesen core charges a whole interval uncounted first; no
    counted case reaches the SRAM one, since every NES fixture cart is battery-less.
    The same capture is what a freshly built core's first block pays: `enableStateSnapshot` arms it, and it cannot move
    to activation without moving `NesApu::EndFrame` and parity, so the first block is pinned to exactly one capture
    (C4's section 12). The publish's own read path is free: it reads the core's snapshot slot straight into the
    registry's own, which `SnapshotRegistry::claim` sized to match on the control thread, so a warmed publish only
    copies. A Game Boy's publish of the same model (whose capture reuses its scratch) allocates nothing at all, and a
    model switch's restart block also grows that scratch by the state delta; C4's `[alloc]` suite counts two of a Game
    Boy's publishes and a NES's registry publish through an Engine.
  - **The engine-level threaded cases are smoke runs, not race detectors.** A system joining and leaving while an audio
    thread runs blocks is one (`EngineGraph.test.cpp`'s C3 case, and the D2 follow-up's GBA beside a publishing Game
    Boy). Against a snapshot registry whose claim freed a buffer the publish wrote, the GBA case reported nothing in
    three runs under each sanitizer, while two ad hoc runs at 256-frame blocks did catch the bug; C3's case was not
    counted against it, and alternates one Game Boy with an empty plan, so no claim there grows beside a publishing plan
    at all. Why the GBA case stays silent is unproven: the thread sanitizer sees ordering between the setGraph path and
    the block thread there (a probe written each block stopped racing at `GraphHost::reclaim`), and the pairing release
    was not found, so "an Engine hides the race" is a reading, not a finding. What holds the claim is the registry
    driven from two threads alone (`a snapshot claim touches nothing the block thread's state publish writes`), which
    fails under the thread sanitizer when the claim and the publish share memory, the slot bytes the seqlock copy writes
    included (`transport/SeqlockCopy.hpp`).
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
  `rp.host.devices/2` (`/1` until E2), the operations an emulated system and a cart share are asynchronous capability
  jobs (`rp.system.memory/1`, `.sram/1`, `.rom/1`), and the cart mirror, the controller channel, `stageSystemMidi`,
  the `__rp_n8*` operation hooks and `N8Host`'s SD worker are gone. Strict parity held bit for bit at every commit,
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
    never pauses: its submission reads the cart's config block first, so a game's notes keep flowing, and the
    submitting thread (the DAW's main thread in the plugin) waits for that read, up to 1 s.
  - **A handover can cut a key the new cart node strikes before the old one is reclaimed**: the old node's held notes
    are released behind every byte pushed when its destroy runs, the new node's first blocks among them, so a note the
    new node starts on a key the old one still held, between the swap and the reclaim, is released with them. Where an
    idle tick reclaims (the SDL standalone, the plugin with its editor open) that is the frame or so after the swap;
    in a plugin whose editor is closed it runs to the next graph publish, the editor opening or a deactivate.
  - **A plugin whose editor is closed has no idle reclaim**: DPF gives a plugin no idle callback of its own, so a plan
    a DAW's `setState` replaced while the transport plays keeps its nodes (a cart node and the notes it holds on the
    cart, a core, the resources they pin) until the next publish, the editor opening or a deactivate. The harness hosts
    (test host, CLI, Node addon) reclaim when their session calls `drainReleased` or `stopAudio`.
  - **The DPF JACK standalone's whole control handoff can run on JACK's thread** (pre-C5): a JACK buffer-size or
    sample-rate change runs `PluginDSP::deactivate`, the change and then `activate` from DPF's notification callback
    (`DistrhoPluginJACK.cpp`'s `jackBufferSizeCallback` / `jackSampleRateCallback` -> `setBufferSize(n, true)`), not
    on the main thread every other control path of the plugin runs on, and the editor's `uiIdle` reclaims on that main
    thread every frame. So `deactivate` hands the engine back on JACK's thread: `setAudioThreadOwns(false)` clears the
    ownership bit, from which every push flushes inline on whichever thread makes it (an edit landing beside the JACK
    thread's drain), and then `drainInto` drains the command ring there, `reclaimReleased` takes the release ring
    there, and the script host's `reclaim` runs a pack's GC and free there (the ownership flip runs a `collect()` of
    its own too) - a second consumer of the ring, and two threads in `JS_RunGC`/`freePack` is undefined behaviour, not
    a corrupt ring. `activate` can likewise grow the plan
    on that thread when the new buffer exceeds the plan's maximum (`setMaxBlockFrames` -> `GraphHost::reactivate`,
    which replans and publishes), and re-declare the DAW parameters there. The fix is to marshal that whole handoff to
    the control thread - the flush, the ownership flip, the drain, the release-ring reclaim and the script host's
    GC/free, and the activate on the other side with its replan and parameter re-declaration - or to treat a
    buffer-size or rate change as a message that thread applies, so the quiescent engine has one owner. Rare and
    manual (a JACK buffer-size change in the standalone only; the formats a DAW loads - CLAP, VST3, AU - call both on
    the host's main thread, and VST2's block-size opcode is the host's thread too). Only `activate`'s
    `setMaxBlockFrames` is a no-op for a block within the plan's maximum, which is why the path went unnoticed; C5's
    per-frame reclaim widened its window from an edit to any frame.
  - **MIDI streamed to a cart at its menu reaches the menu's own command parser** (pre-C5, unverified on hardware):
    the menu takes commands over the same FIFO, so a MIDI data pair such as `0x2A 0x73` (`*s`) sent while the menu
    shows can ask it to start an app. It holds for raw MIDI after a connect at the menu and for framed MIDI after a
    console reset, where the stale window is re-sent every 100 ms until it re-syncs.
  - **The quiet rule assumes the N8 menu writes nothing unprompted on the back-channel** (unverified on a cart):
    unsolicited bytes would keep `lastHeardNs` fresh and make a menu with stale frames read as never quiet.
  - **A heavy stream into a menu can starve the probe**: the window's 48-frame re-sync re-stamps the oldest frame's
    first send, so past about 190 messages a second (48 per 250 ms) into a menu no turn is quiet and a submission is
    refused for a link that never went quiet.
  - **A retained generation of a script pack, a Mesen core's 0.5 s state publish and the N8 firmware's SD path** are
    the allocations left on the block thread, as C1/C4 list them above; C5 added none.
- **D1 built: the kit baseline and the DSP library**
  ([modular-graph-phase-d1-design.md](../docs/modular-graph-phase-d1-design.md), six commits): the kit path's DSP is
  `packages/dsp/native/src/dsp/` as block-streaming kernels (`retroplug-dsp-kernels`, plus `retroplug-dsp-resample`
  for r8brain), and `lsdj/Effects`, `KitUtil` and `RisaDmcCodec` run those kernels. The graph has them as
  `rp.dsp.gain`, `.biquad`, `.dither` and `rp.dsp.resample` (`retroplug-dsp-nodes` / `retroplug-dsp-resample-nodes`,
  the resampler a package and target of its own so a Pico build can leave it and r8brain out). TPDF dither is seeded
  from a hash of the sample it sees, so a kit compiles to the same bytes, and `retroplug-audio-test`'s
  `[kit-baseline]` held 39 compiled banks to a golden (the test-native runners hold them now; the next bullet). Strict
  parity held bit for bit at every commit, and the baseline did not move by a hash through the kernel move. What it
  leaves:
  - **D2**: audio buffers in and out of a document, a run-to-completion driver over `GraphHost`, normalise in a graph,
    the two-rate kit chain, and kit compiles as graphs behind `compileKit` / `compileDmc`, byte-identical to the
    baseline (as built, the TypeScript entry points of those names; the harness RPCs went with native's compiler, the
    next bullet). D1's build fixes the seed rule (hash the whole pre-trim buffer and pass `rp::dsp::inputSeed` in the
    node's `seed`), normalise (call `normalizedGain`, a reciprocal multiply), and the order (effects at the source
    rate, resample, quantise, `frameAlign`, nibble pack).
  - **D2 has to widen the nodes or wire around them.** The nodes' param ranges are narrower than a kit spec can be
    (`rp.dsp.gain` 0..4 where a normalised `gain / peak` runs to about 7.4 for a quiet source and negative for a phase
    flip; `rp.dsp.biquad` 20 Hz to 20 kHz, Q 0.1 to 10, -24..24 dB where a kit's `FilterEffect` can be 10 Hz at Q 20 or
    a 30 dB shelf), and a document's or `setParam`'s out-of-range value is clamped into the range silently, where a
    wired param is not clamped. D2 needs wider ranges, settings-level values or a control wire, and the baseline now
    carries the cases to hold it to: a quiet source normalised at all three rates (and through a dither), a negative
    gain, a 10 Hz high-pass at Q 20, and a 30 dB high shelf.
  - **D3** (built since: the D3 bullet below): the CLI `render` and the UI background renders moved to engine jobs on
    the offline pool; D1 changed neither path.
  - **Residuals.** The kit baseline is a 4-bit view and nearly blind to a one-ulp move in Sierra Lite or JJN (the
    `[dsp]` cases carry that). `rp.dsp.resample` and a recorder on one buffer in one plan are unsupported (the planner
    refuses a port reader beside a writer, and a settings ref is a binding it does not see), and a rate change resumes
    where the output had reached, so a fresh r8brain run-in is heard from there rather than a gap. Dither's per-channel
    state is about 5 KB, and two `rp.dsp.dither` nodes with the same settings draw the same noise: the seed step
    separates one node's channels, not two nodes. A kit filter above its source rate's Nyquist compiles to constant
    codes today (the biquad's state goes NaN and the quantiser reads every NaN as code 0), where `rp.dsp.biquad`
    recovers, so D2 has to decide between the kernel call and the node for such a kit. Both guards also differ from the
    kernel on any non-finite sample a decoded 32-bit float WAV can carry (nothing between `SampleCache` and
    `KitCompiler` checked `isfinite`): a NaN or an infinity latches a kernel where its node restarts, so D2 decides
    for both rules (as built, its compiler refuses such a source and any pass a guard stepped into; the next bullet).
    Before the biquad's guard fires its kernel writes rail-to-rail output, so a held above-Nyquist setting
    is full-scale bursts between the guard's silent blocks, not silence. A `rp::dsp::Resampler` destroyed after
    `R8bBase.cpp`'s statics is a use-after-free in r8brain's `unref` (it locks a static mutex and writes into
    cache-owned objects), so D2's driver and D3's renders keep every resampler inside an `Engine`'s life, as D1's hosts
    do by tearing every Engine down before exit. The plugin still could not compile a kit then (the RPCs were
    harness-only), which D2 owned and did: the next bullet.
- **D2 built: kit compiles as offline graphs**
  ([modular-graph-phase-d2-design.md](../docs/modular-graph-phase-d2-design.md), seven commits): an offline job
  seam runs a graph document to completion on a host of its own, on an engine-owned worker pool (`[offline]`);
  `rp.dsp.normalize`, dither's `seedFrom` and `rp.dsp.dpcm` join `rp.dsp`; and `src/kit/` compiles LSDj and risa kits
  as chains of those jobs, packing the banks in TypeScript. The 39 baseline banks and 12 edge banks are the
  compiler's, held by `test-native/kit-baseline.test.ts` and `kit-compile.test.ts`; native `KitCompiler`, the two
  codecs, `KitUtil`, `SampleUtil` and the harness `compileKit` / `compileDmc` RPCs are gone, so the plugin compiles
  kits too. Strict parity held bit for bit at every commit, and no parity or kit golden was re-recorded (the `rp.dsp`
  descriptor goldens were, in stages 1 and 3). What it leaves:
  - **D3** (built since: the next bullet): the CLI `render` and the UI background renders on the pool as engine jobs
    (kind `render`: a bare QuickJS runtime and an `Engine` of their own on a worker), not as graph jobs, so a job's
    registry, transport and MIDI input needed no extension. A graph job's transport stays fixed at 120 bpm, ppq 0, not
    playing, and takes no MIDI.
  - **A plugin kit row** is possible and not built: it imports `src/kit` and runs it on the stores' backend, not
    `audioDriver` (the driver's compile would turn on the offline poll's native wait and block the UI frame).
  - **Residuals.** `src/lsdj/` is test-reference code only now (`Effects`, `KitBank.hpp`); `OffsetLookup.hpp` is dead
    (nothing includes it) and a candidate for the same cleanup as enkiTS. Nothing in production calls
    `rp::lsdj::processEffect`, so do not wire a caller back to it thinking it is the kit path. A
    job's document may name only the built-in C++ packages (no script nodes), and a user graph of script nodes would
    need a job-side script host. `deps/enkiTS` is vendored under `deps/` and nothing builds or links it; a later cleanup
    decides whether it goes. Off `linux-x64-gnu13` the kit banks are held only by self-agreement and the structural
    asserts, since the native comparison that used to make the edge banks exact everywhere went with the compiler.
  - **Interactive kit patching has a latency budget it does not meet** (user, 2026-11-02). A kit patcher UI must let a
    knob change be heard in the running emulator almost at once, as the user's earlier LSDj patcher did. The TypeScript
    compile over offline jobs costs about 23 ms typical and 83 ms worst for a 15-slot kit where native was 1.7 and
    4.9 ms, nearly all of it buffers crossing the JS thread between passes (about 1 MB a slot for a 1 s source, 15 MB a
    kit). When a patcher UI lands, the fix is to chain a slot's passes inside one job or keep the intermediates native
    behind handles so only the final buffer crosses, and to recompile only the slot a knob touched.
  - **Engine-level threaded cases are weak race detectors.** A case driving the `Engine` from two threads passed with
    a shared-buffer race present, while the registry-level cases caught it; why is open (see the C4 bullet above, where
    the happens-before edge ThreadSanitizer appears to see in `setGraph` is a reading, not a finding). Worth an
    investigation.
  - **Each Engine keeps up to 8 normal-priority offline workers parked for life** once it has compiled anything. In a
    DAW, N instances can hold 8N threads, and a compile briefly puts 8 busy threads beside the audio thread; a plugin
    kit row should lower the ceiling or retire idle workers.
  - **Confirm the removed transitive includes build under MSVC.** The tree has not been built with MSVC since D2
    deleted headers that used to arrive through `BackendTypes.hpp` and `retroplug-core`; run `build.bat` (or the
    Windows CI job) before trusting the platform that has not run here.
- **D3 built: renders as engine jobs**
  ([modular-graph-phase-d3-design.md](../docs/modular-graph-phase-d3-design.md), five commits): the pool runs a second
  kind of job, a render, on a `RenderHost` and an `Engine` of its own, with progress and console lines in the drain,
  TypeScript cancel for both kinds (`engineRun` / `offlineRun` handles) and a worker budget that keeps one worker for
  kit compiles; an engine job's engine runs continuous 1024-frame blocks, so a render's samples do not depend on how
  the library chunks its pulls; `retroplug-cli render` and the UI's System > Render submit the same job, and
  `RenderJobRegistry`, the `__rp_*Render` hooks and the CLI's in-process render path are gone. The render baseline
  (`render-baseline`, 13 cases) records the bytes; no parity or kit golden was re-recorded. Two vendored fixes for
  parallel cores came with it: Mesen's `SimpleLock` atomics and `cmake/patches/sameboy-process-statics.patch`. What it
  leaves:
  - **CI cannot see most of the render path.** The only fixture-free sounding renders are smsggdj with an authored song
    (4 cases) and mGB as a Super Game Boy (its boot jingle), so CI holds 5 of the baseline's 13 cases and nothing of a
    NES, a split render or a Game Boy's stems; the LSDj and risa cases run only with fixtures. MIDI-driven renders (an
    mGB or BlipToaster render fed a MIDI file) would close it and are not built. The C++ `[engine-jobs]` cases are the
    only fixture-free per-channel coverage today (mGB through `rp.mgb.midi`).
  - **An auto-detect render's length is quantised to its 100 ms poll**, and acts between the library's calls (presses,
    smsggdj's load, detection reads) land at the next block boundary after the call. Block-granular probes or an
    autonomous recipe (the D3 design's section 8) would remove both.
  - **An interrupt that lands in a pending job** (`RenderHost.cpp`, the job loop's `r < 0` branch) is also printed as a
    script error before the loop re-checks `interrupted_`; today's worker is synchronous, so only a script working in
    promise jobs reaches it.
  - **An interrupt after the script reported `done`** keeps its `outputs` under `cancelled` (only `message` is cleared);
    reachable only if the cancel has waited its grace by the time the worker's post-result JS runs.
  - **A failed `.part` sync reads as a failed move** in `render.ts`'s error ("could not replace X"), which points at a
    locked target rather than a disk error.
  - **`foldsCase` trusts a single probe** (the first listed entry with a cased letter): a broken symlink or a name whose
    case swap is not a simple fold reads a folding filesystem as case-sensitive, the pre-F behaviour.
  - **A battery cart whose live SRAM reads null** still boots the sibling `.sav` silently in `ui/host/render.ts`; a
    running battery core should always answer `readSram`, so refusing as the unwritable-snapshot case does is open.
  - **`tools/author-*-rplg.js` default their ROMs to `../resources`** relative to the repo root, so a plain
    `pnpm reaper:all` fails in a worktree (honour `RETROPLUG_RESOURCES_DIR`); until then author the fixtures with
    explicit paths and run `RP_SUITE_NO_BUILD=1 pnpm reaper:all`.
  - **`retroplug-host` swallows a top-level throw**, reporting only "tests did not complete (tjs.exit never called)",
    which hides a fixture script's real error.
  - **`tools/run-sanitizer.sh` has no render leg.** The thread and address checks of the pool are `[engine-jobs]` built
    in `build-tsan` / `build-asan` by hand (`retroplug-render-host-test`'s three legs also run clean there); a script
    leg would make them routine. `retroplug-render-host-test` is a manual smoke, not a CI step.
  - **Wedged jobs.** A render's script that misses its cancel is interrupted after two seconds
    (`RenderHost::kCancelGrace`, possibly leaving its `.part` files), but a core wedged inside one native call is not:
    it hangs the CLI where the old bounded loop gave up (a second Ctrl-C kills it), and an instance's teardown, which
    joins its engine's renders on the thread that removes the instance or quits the host, waits for it. With the
    editor closed nothing drains, so a render that finished then is listed as rendering until the editor pumps again
    (its WAV is on disk).
  - **The behavioural render tests run the in-process library.** `audio-render`, `risa-render`, `sms-render`,
    `start-truncation`, `risa-autodetect` and `render-guard` call `runRenderJob` on an exact-blocks engine, a path no
    production caller uses; for engine jobs their assertions reach users only through the render baseline's hashes and
    `engine-run`.
  - **emu2413's VRC7 tables** are a lazily built process static behind a plain flag (the one left; the NES database's
    and the GBA dispatch tables' flags are acquire/release): two VRC7 carts built at once as the process's first race.
  - **A UI render's snapshot** (`.render-src-<key>.*` in the config directory) is deleted when its job settles, so an
    instance or process that ends before the job settles leaves it behind: a closed DAW project or a removed plugin
    with a render queued or running, or a render finished while the editor was closed and the project then closed
    (nothing drains then), as well as a crash. Nothing removes them. The cheaper real fix is left: the worker deleting
    its own snapshot once the system is built (a spec flag only the UI sets), the settle hook then covering only jobs
    that never booted.
  - **The SameBoy camera half of the statics patch has no red-green test** (no Game Boy Camera ROM exists in the repo or
    the fixtures), and the exact render bytes are held only on `linux-x64-gnu13`. The patch uses GNU `__atomic`
    builtins, which GCC and Clang (clang-cl on Windows) have; the tree has not been built with MSVC since D2.
  - **A GBA render case** is missing: without a BIOS the repo's GBA ROM renders exact silence.
  - **Cores print straight to the process.** Mesen's load log goes to fd 1 while the job's console lines arrive through
    the drain, so in the CLI the two can interleave differently than before.
- **E1 built: a portable node core and the package boundary**
  ([modular-graph-phase-e1-design.md](../docs/modular-graph-phase-e1-design.md), five commits): `node-sdk`,
  `graph-runtime`, `core` and `dsp` are workspace packages with a manifest each, and every product's
  `rp_register_packages()` is generated (the `[packages]` golden); portable nodes' settings are `rp::text`, `rp::list`,
  `rp::choice` and numeric resource ids; `node.hpp` builds for the device (`RP_DEVICE`) and reads msgpack
  (`RP_SETTINGS_MSGPACK`) with a reader of our own; `retroplug-device-mode-test` is the eleventh `test:plugin` binary;
  and `pnpm device:check` links the RP2350 image of every portable `rp.core` and `rp.dsp` type under a trap on the heap,
  on exceptions and on RTTI, a CI step. Nothing was re-recorded. What is left, in order:
  - **E2: the N8 pack** is built (below).
  - **E3: `exportPlan` for `rp2350`, the static-arena executor and the box as a graph host**, gated on the Pico baseline
    and a C++ node in the box's plan. It must also settle `rp.core.delay`'s default, which needs 384 KB an instance
    (74% of the RP2350's RAM) and sizes the check's arena: a device default or a planner cap. The device's stack in
    `query_memory` and `create` (the `Settings` plus up to three 264-byte error buffers) is unmeasured, as are instance
    sizes on ARM (the image is never run), and the float tolerance against the desktop is E3's decision. An integer above
    INT64_MAX in a signed settings field is wrapped by the desktop's JSON reader and refused by the device's decode, so
    `exportPlan` writes each blob from the desktop's parsed settings (`rp.sdk.settings-json/1`, then `settingsToMsgpack`),
    not from the document's JSON.
  - **E4: MicroQuickJS on the Pico**, a script host in the arena and `defineNode` made portable.
  - **The rest of the package split** (the main design's section 11): `sameboy`, `mesen` (per console), `lsdj`, `mgb`,
    `risa`, `smsggdj`, `bliptoaster` and `launchpad` as packages, the TS halves of `core` and `dsp`, per-product esbuild
    bundles, downloadable packs.
  - **Windows and MSVC have never built E1's targets.** The `<target>-device` object libraries are in the default build
    under `/W4 /WX`, so a Windows problem breaks `build.bat` itself: a `build.bat` and `test.bat` run is owed. Device
    mode no longer relies on an inline namespace (MSVC does not extend one through a later `namespace rp::graph`, CWG
    2061); the device-mode test renames its objects' `rp` with `/Drp=rp_devmode` instead. The code review's MSVC
    snippets compiled `rp::choice::make<>` and `rp::text`'s literal constructor as consteval in default member
    initializers, `enums.hpp`'s template-lambda variable templates and `__FUNCSIG__` in a consteval function clean
    under `/W4 /WX`; not built on MSVC: the copied `/bigobj`, `std::strcpy` in `DeviceCheck.cpp` and the rename over
    the MSVC STL's headers. The device objects keep MSVC's `/EHsc`, so only the GCC and Clang builds prove the
    no-exceptions build.
  - **libc++ runs two of the three refused-source checks** (the heap-header check reads libstdc++'s include guards and
    is skipped on macOS); there the `static_assert`s and the link trap are the guards.
  - **Enum limits.** An `rfl::config::enum_range<E>` override is not honoured and a custom `rfl::Reflector<E>` is not
    seen by the device decode (`rfl::internal::has_read_reflector<E>` would refuse it in one line; no node has one);
    each portable enum costs 55 to 95 ms of compile time a translation unit.
- **E2 built: the N8 pack and one Edio** ([modular-graph-phase-e2-design.md](../docs/modular-graph-phase-e2-design.md),
  five commits): the device ABI in `node-sdk` (`rp_device.h`, `rp_jobs.h`, `rp_stage.h`: a fixed 8-entry registry,
  `rp.host.devices/2`, an opaque `rp_job` over the engine's record, the four-entry `rp.host.jobs/1`), `packages/n8` (the
  portable `retroplug-n8`, `device: true`, and the desktop `retroplug-n8-desktop` with its provider and RPC facet), the
  Edio written once (C++, no exceptions, an abstract `Transport`; `edio.ts`, `n8Menu.ts`, the desktop C++ Edio,
  `SerialRpcService`, the four `__rp_*N8` hooks and `pico/n8-host/edio.c` are gone), generated providers
  (`rp_product_providers.hpp`, one `rp::Providers` per product instance), and the box firmware in C++ on the pack under
  the link trap over the whole image (`pnpm device:box`, a CI step; a static SOF timer on hardware alarm 2 and a wrap of
  `alarm_pool_create_on_timer`; a 4 KB stack). The Pico gate passed on the rig with BlipToaster. Nothing was re-recorded
  but `[packages]`, where `rp.n8.nes` moved under the n8 package with every product's ids unchanged. What it leaves:
  - **E3's box link must keep `CartLink`'s promise** that `push()` never blocks, allocates or takes a lock: the node
    declares no `threads` and a Pico target plans it, so the link behind it decides what the box can do.
  - **The device check keeps the SDK's 2 KB stack.** Its protocol chain is about 1.9 KB by the hand sum and its types
    chain (E1's settings decode) about 2.3 KB; the check's successor wants `PICO_STACK_SIZE=0x1000` as the box has, and
    a `-Wstack-usage` check would replace the hand sum, which has no limit at run time.
  - **`UsbCdcTransport` has no host-side fake**: its remount, unmount and drain rules are proven on the rig and by
    reading, with no host test.
  - **The Launchpad's nodes still declare `threads`** for their link, the one exception to "the flag is what the node
    itself needs", until they have a pack (provider, facet in place of `bindLaunchpadHooks`).
  - **The N8 menus, the CLI sessions and `buildGraph`'s `cart-<id>` placement stay in `packages/retroplug`** until menus
    move onto extensions; `chrImage.ts` and `risaSyncTranslator.ts` stay there too, as each imports `retroplug` modules.
  - **The gate's latency comparison is partial.** The `[stats] lat=` figure is per window, not cumulative, and the SysEx
    window peaks at 20.9 ms (a 40-byte SysEx is about 13 ms on the wire); no baseline window was kept for it.
  - **Windows, macOS and arm64 have never built E2.** The new `/W4 /WX` surface on MSVC is `retroplug-n8`,
    `retroplug-n8-desktop`, `retroplug-n8-device`, `DeviceAbi.c` compiled as C, the generated providers header in three
    products, `N8Rpc.cpp`'s `std::filesystem` (POSIX names only on Windows now), `SerialTransport.cpp`'s POSIX half and
    `FakeN8.hpp` in `retroplug-host`; AppleClang and libc++ and the arm64 job have seen none of it. `build.yml` runs only
    on pull requests, main and a dispatch, so the first run of the `device:box` step on a runner is that run too. A
    `build.bat` and `test.bat` run and the macOS and arm64 builds are owed.
  - **A pack's facet is mounted whole in every product that lists it** (decided 2026-11-05; design section 6): the
    plugin and SDL channels carry the N8 session methods and the CLI the link half, and only a convention keeps CLI
    scripts on sessions. Scoping a product (an `rpc` list with a scope per entry) is for when packs become downloadable.
  - **Template gaps a second pack meets** (design 13.8): method names are global and a repeat replaces silently; a pack
    cannot add a capability beyond `rp.system.memory/sram/rom`; the test host has no seam for a pack's test provider;
    the TS half needs its `ts/` in `packages/retroplug/tsconfig.json`'s `include`; a provider cannot be downloaded.
  - **The pack boundary has three standing exceptions** (design 13.6): `retroplug-core` includes `retroplug-n8`'s roots
    for `N8ReliableLink.hpp`, `plugin.cmake` links `retroplug-n8-desktop` by name, and the test host's `main.cpp` names
    the provider.
  - **The box image keeps the SDK's 2 KB `.heap` reservation** (no allocator is linked; `PICO_HEAP_SIZE=0` is E3's).
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
    only while quiescent
    ([Planner.cpp](../packages/graph-runtime/native/src/graph/Planner.cpp) `front`). No port changes
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

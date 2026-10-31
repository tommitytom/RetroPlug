# Phase C4: the tap and the build check name no console

**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** built: section 7's six commits, plus the
follow-up fixes section 10 records; what building them changed is in the sections it changed, and what it found is
section 10

After C3 a system node builds its own core, but the engine still knows one console by name and one core still allocates
on the audio thread. `Engine` finds a NES core with `dynamic_cast<MesenNesSystem*>` to arm its per-channel tap, a system
node `dynamic_cast`s the core it built to its own type, and every Mesen core collects its audio in a `std::vector` that
`resize`s on the block thread. `SystemBase` also still carries about ten members nothing calls. This phase removes all
of that: the dead surface goes, Mesen's audio collects in a fixed ring sized before the first block, the tap becomes a
capability any core may offer, the node's build check stops casting, and the ingress queues a block's host input lands
in are bounded rings sized when the core is built. It is Phase C's fourth piece
([modular-graph-design.md](modular-graph-design.md) section 8: "Console-specific code moves into each node; every
`dynamic_cast` in section 6.5 goes; `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a
preallocated ring"), after C3 ([modular-graph-phase-c3-design.md](modular-graph-phase-c3-design.md)). C4 is a part of
that bullet, chosen by the user (section 2), plus a second decision taken while it was built (section 9) and a third
taken after this design's first draft (section 11); the rest is section 12's.

## 1. Before C4

- **`SystemBase`** ([SystemBase.hpp](../packages/native/src/system/SystemBase.hpp)) carries members with no production
  caller: the battery-path accessors `savSuffix` / `setSavSuffix` / `savPath` / `setSavPath` (the paths are TS's,
  `savPaths.ts`), the `fastBoot()` getter (fast boot reaches the core as a node setting), `clearSram` (a blank battery
  is a seed), `loadStateBytes` from outside (a savestate is a seed; the core's own seed path calls it internally),
  `midiOut()` / `midiOut_` (the old core-side MIDI out; the serial-out decoder replaced it), and a live memory-snapshot
  mechanism nothing enables (`enableMemorySnapshot`, `disableMemorySnapshot`, `memorySnapshot`,
  `publishMemorySnapshots`, `snapshots_`), whose publish every core's `finishBlock` still runs over nine empty slots.
  `kind()` has no production caller either.
- **`MesenAudioDevice`** ([MesenAudioDevice.hpp](../packages/native/src/system/mesen/MesenAudioDevice.hpp)), the
  `IAudioDevice` every Mesen core (NES, GBA, SMS / Game Gear) gives Mesen's mixer, is a `std::vector<int16_t>` of
  interleaved stereo: `PlayBuffer` appends with `resize` (an allocation whenever the high-water mark rises: at the start
  of a core's life and after a change of flush cadence such as a sample-rate change), and `drain` takes a block's frames
  off the front with `erase`, a memmove of the remainder every block. Its fill (`availableFrames()`) is the block clock:
  the cores' step loops run until a block's frames are available, and the NES's N8 FIFO schedules host bytes against
  the same count. The NES per-channel tap has a second, Mesen-side ring, `NesSoundMixer::_streamBuf`
  (`deps/mesen/Core/NES/NesSoundMixer.cpp`, grown by `resize` when the tap is armed), and the NES node's per-block
  scratches (`stereoAccum_`, `chanAccum_`) are sized with `assign` on the block thread. The profile benches' allocation
  windows count QuickJS's allocator only, so none of this is seen by a test.
- **The tap.** `Engine::syncTap` arms and restores a NES core's channel export mode for TS's single-NES split routings
  (`buildGraph`'s `tapArm`, sent as `request.tap`). The plan's `SystemTable` keeps a `MesenNesSystem* nes` per slot for
  it, filled by `dynamic_cast` in `Engine::buildSystem` and in `publishGraph`'s pre-fill for a reused node, and
  `ArmedTap` holds the armed core by address. The N8 cart mirror needs no core pointer (a system id and a node-type
  string).
- **The build check.** `SystemNodes.cpp`'s node build `dynamic_cast`s the `SystemBase*` `rp.host.systems/1` handed back
  to its own core type, destroying it and refusing on a mismatch (a host or settings bug).
- Inside the Mesen wrappers, about twenty casts reach Mesen's own console types (`NesConsole`, `GbaConsole`,
  `SmsConsole`): the core's business, not the engine's.

## 2. Decisions taken

The user's, on 2026-10-31: C4 is P1 (the dead surface), P2 (the audio ring), P5 (the tap as a capability) and P6 (the
build check), from a survey that split the main design's bullet into seven landable pieces. Left for later (section 12):
P3, cores borrowing their node's pinned ROM bytes; P4, collapsing the snapshot registry's double copy; P7, the full
capability split of `SystemBase` (main design section 6.4) and one node file per console.

A second decision, taken while P2 was built: **Mesen's frame and input plumbing is C4's too**, in a commit of its own
(section 9). P2's zero-allocation case found a Mesen console allocating 13 to 18 times per emulated frame inside Mesen -
three for the control manager's port snapshot, two for each device's raw state, twelve for a `RenderedFrame` and its
`InputData` copies, one for a notification's listener copy - so "a warmed block allocates nothing" could not hold while
those stayed, and none of them is the audio path. The measurement is section 9's and what fixed them also section 9's.

A third decision, taken after this design was first written: **a core's ingress queues are C4's too**, in a commit of
its own (section 11). The `[alloc]` cases warmed with a button press and then staged nothing inside their counted
windows, so "a warmed core's block allocates nothing" held only while the node handed its core no host input: every
queue a block's input lands in grew on the block thread, and a host playing a MIDI-heavy project is exactly that steady
state. The extended case found 7 to 11 allocations in the NES's first counted window (5 for 12 presses alone), so every
one of those queues is now a bounded, preallocated ring sized when the core is built from what one block's doors can
deliver (`system/InputCapacity.hpp`); a push past its bound is dropped and counted (section 11).

## 3. P1: the dead surface, as built

Every member section 1 lists went, with each core's overrides and the calls that only fed them (the memory-snapshot
publish in every `finishBlock`), and so did the four core configs' `savSuffix` / `savPath` fields, which nothing in
native assigned (the paths are TS's): the only readers were the deleted accessors. `loadStateBytes` stays as a
**private** virtual, so `seedSramAndState` keeps one dispatch point for the three Mesen restore bodies without keeping
the member on the public surface; SameBoy's override went entirely, since its seed path restores inline. `kind()` stayed
and P6 gave it its first production caller. The deleted publish walked all **nine** `MemoryType` slots every block over
an always-empty array (section 1's survey said seven; `NametableRam` and `ExtWorkRam` came after it), so P1 takes a real
if tiny per-block cost off all four cores. No test read a deleted member: none moved, and none was deleted.

## 4. What was built: a fixed audio ring

`MesenAudioDevice` is a fixed-capacity ring of interleaved stereo `int16_t` with a head and a count: `PlayBuffer`
writes at the tail, `drain(dest, frames)` reads from the head in the one or two ranges a wrap makes, and
`availableFrames()` is the count, so it keeps its exact meaning (frames written and not yet drained) and the step loops
and the N8 FIFO's scheduling gate on the same number. No `resize`, no `erase`.

**Capacity** is the plan's maximum block frames plus one Mesen flush: `maxBlockFrames + kMaxPlayBufferFrames`, with
`kMaxPlayBufferFrames = 0x10000 / 2 = 32768` frames, about 160 KiB a Mesen system at the engine's 8192-frame floor and
384 KiB at the 65536 ceiling. The bound is the mixer's own output buffer, not a property of a console or a sample rate:
`SoundMixer::PlayAudioBuffer` resamples into `_sampleBuffer` (0x10000 `int16`, stereo: 0x10000 / 2 frames) and is given
exactly that as its output cap, the whole buffer rather than half of it, and the one other `PlayBuffer` source (the
pitch-adjust path, below 100 % emulation speed) is capped by a smaller buffer at 16384. A step loop that gates on
`availableFrames() >= framesNeeded` therefore stops with at most one flush over a block, at every sample rate.
`reserveAudio` sizes the ring, grows only, and carries what is queued across in at most two `memcpy`s;
`capacityFrames()` and `overflowFrames()` exist for the tests.

**What a system holds besides the ring.** Sizing at activation is what makes an arm allocation-free, so a NES holds its
tap's five rings, the mixer's two resample scratches and its own mix and per-channel scratches whether or not the tap is
ever armed: at the 8192-frame floor the ring is 160 KiB and those about 530 KiB (five tap rings of block plus 12800
frames, 205 KiB; two resample scratches, 100 KiB; the five per-channel scratches, 160 KiB; the mix scratch, 64 KiB),
about 690 KiB a NES in all, and at the 65536 ceiling the ring is 384 KiB and those about 2.6 MiB, about 3 MB. A SMS or
a GBA has the ring and one mix scratch: about 224 KiB at the floor and 896 KiB at the ceiling.

**Overflow** drops the NEWEST frames, the ones that do not fit, copies the prefix that does, counts the rest in
`overflowFrames_` and asserts in a debug build. The queued frames are the ones the pending block's sample indices and
the N8 FIFO's already-scheduled bytes refer to, and they are what `availableFrames()` has already told the step loop it
has, so overwriting them would move every later sample earlier in the block. The capacity makes a drop unreachable now
that section 10's narrow-host defect is fixed: that gated a mix-drained block on the tap's rings, so the mix ring kept
one flush's overshoot every block and filled.

**The block size comes with the build.** `SystemBuildRequest` carries `maxBlockFrames`, a system node sets it from its
plan's maximum in `activate` before `build`, and each backend reserves it before `onActivate`, so the ring is the plan's
from construction and the activation-time reserve is a no-op. A build outside a plan (a test, a tool) leaves it 0 and
the core sizes for `kDefaultBlockFrames` (8192, the engine's default plan size, which a test holds equal to
`Engine::kDefaultMaxBlockFrames`). A host that grows its block later reaches the same reserve through the reactivation
`Engine::setMaxBlockFrames` performs while the graph is quiescent. Memory a plan does not need is not kept: a 64-frame
plan's ring is 128.25 KiB where an 8192-frame plan's is 160 KiB.

**Everything else a block touches is sized at activation.** Each Mesen core sizes its per-block scratches there
(`stereoAccum_` two floats a frame, every `chanAccum_[k]` one) and at a larger reserve; the NES's tap rings
(`NesSoundMixer::_streamBuf`, an in-tree patch) are sized by `SetCaptureCapacity(maxBlockFrames)` to
`maxBlockFrames + MaxSamplesPerFrame` (12800, the most one `CaptureStreams` call appends), and a reset or a re-arm
clears their queues but keeps their storage; and SameBoy's two scratch sets are sized the same way. A core's ingress
queues are sized the same way too, from what one block's inputs can deliver, so a block's host input grows nothing
(section 11).
**An arm sizes nothing**:
the two discards that used to size a scratch from the capture mode drain in block-sized chunks instead, so
`setChannelExportMode` no longer grows anything. It still calls `SetChannelCapture`, which calls `SetCaptureCapacity` on
every arm, so "sizes nothing" rests on the activation having already made that call a no-op: an arm before activation
records the mode and returns, since there is no mixer yet, and activation sizes the rings from the block the build
carried (the default outside a plan). `setChannelExportMode` passes that same recorded block, so after activation the
call always sees a size the core already has, however a host grew it. The plumbing commit (section 9) took the arm's
last allocation too:
`blip_reset(m, size)` replays `blip_new`'s order exactly (the default factor, then `blip_clear`), `SetCaptureCapacity`
creates all five stream blips with the rings, and an arm re-initialises one with `blip_reset` and then calls
`blip_set_rates`, which is the state a fresh `blip_new` would reach at the same phase; a disarm keeps the blips for the
next arm. Preallocating the blips *and* rate-setting them at capacity time shifted the tap's first samples by a
sub-sample phase (`blip_clear` seeds the offset from `factor / 2`), which parity caught, so this keeps the phase and the
allocation count both.

**The promise** is the audio path's and, since section 9's commit, the whole block's: a warmed core's block allocates
and frees nothing, emulation included, and since section 11's commit that holds with a block's host input staged into it
too. Each queue that input lands in is bounded and sized when the core is built, so a press, a MIDI run, a serial byte
or a raw message is refused and counted rather than grown. What still allocates inside a long run is Mesen's periodic
savestate capture (0.5 s, its streaming serializer, charged outside every test window) and the N8 firmware's
SD-command path (section 9).

## 5. P5 and P6, as built: the tap a capability, and the build check

**P5.** `SystemBase` gained `virtual ChannelTap* channelTap()`, null by default, in a "Per-channel audio tap" section
beside `debugTarget()`'s capability pattern. `ChannelTap`
([ChannelTap.hpp](../packages/native/src/system/ChannelTap.hpp)) is the interface in the global namespace, with
`SystemBase` and `ChannelStream`, because the engine reads it as a core capability: `exportMode() const`,
`setExportMode(std::uint32_t)` and a virtual destructor, and nothing else. A mode is
the backend's own numbering (a NES's 0 is the mixed output and 1 to 3 are its split exports), and an arm must neither
allocate nor restart the core: a routing flip must not reset a playing game. `setExportMode` runs between two blocks on
the block thread, or on the control thread while the engine is quiescent.

**The NES is its own tap.** `MesenNesSystem` implements `ChannelTap` and `channelTap()` returns `this`: the mode is
`MesenNesConfig::channelExportMode` and the arm is `setChannelExportMode`, so there is no state to own. That is what
keeps `ArmedTap`'s address comparison sound: a secondary-base `ChannelTap*` is the core object's address plus a
per-class constant, so within one class tap equality is core equality. The record's pointer is compared and never
followed, so it may outlive the core it names; C3's residual (a core allocated where the armed tap was freed would
match) keeps its shape at the tap's address (section 12).

**`SystemTable`.** A slot is `{ id, SystemBase* core, ChannelTap* tap }`, the tap filled from `channelTap()` where the
two casts were (the plan's build and `publishGraph`'s pre-fill for a reused node), so a slot whose core offers none
holds null and an arm reaches nothing. `Engine` no longer includes `MesenNesSystem.hpp` (it includes
`system/ChannelTap.hpp` and forward-declares `ChannelTap`), and TS does not change: the arm still names a system id and
a mode.

**The mode range stays with the engine, as a deliberate residual.** `Engine::setGraph` still refuses a tap mode outside
1 to 3, which is the NES's numbering rather than the capability's. TS owns the numbering and the arm travels as a byte,
so a second console's tap would be validated against the NES's range; moving the bound onto the capability
(`maxMode()`) is the later move, and needs no core change.

**P6.** Each core class carries `static constexpr SystemKind kKind`, which its `kind()` returns, and the node's build
compares what `rp.host.systems/1` handed back with `Core::kKind`, then destroys it and refuses with the same message
on a mismatch, or `static_cast`s it on a match. `kKind` is on the core class rather than on the node type so there is
one source of truth: the value the node compares with is the value the core answers by construction, so the two can
only differ through an edit to the class, which refuses every plan of that console at once. The cast's soundness is the
invariant `SystemFactory` and `MesenBackend` already rest on, kinds being unique per core class: the config alternative
names the console, the factory builds that console's class, and each class reports a kind no other does.

After both, no unchecked cast to a concrete core is left outside `packages/native/src/system/`: `packages/native/src`
has no `dynamic_cast` outside it, one `static_cast` in `SameBoySystem.cpp` (inside it, from GB user data), and the
node's own checked `static_cast<Core*>`.

**The one console check the engine still makes is the N8 cart mirror's, and it is NES by construction.**
`Engine::setGraph` reads a `rp.n8.cart-mirror` node's settings as a system id, then refuses one whose document node is
not `rp::host::systemNodeType(SystemKind::MesenNes)` (`Engine.cpp`), because the N8 mirror is a NES's cart reader and
no other console's. That is a fact about the device, not a cast and not an arming, so it stays: what C4 did is make
the tap and the build check name no console, not put a console's name nowhere in `src/host/`. The per-console node
definitions in `src/host/graph/SystemNodes.cpp` still name their cores (`struct Nes : SystemNode<Nes, MesenNesSystem,
...>` and its three siblings), which is the seam C3 put them there for rather than a check the engine makes. The tap's
mode bound 1 to 3 is the other console name `Engine` keeps (section 12).

## 6. Tests, as built

- **Parity** held bit for bit at every commit, nothing re-recorded: the ring changes where samples wait, not which
  samples are made. The narrow-host follow-up then re-recorded exactly the two groups the fix moved, section 10.
- **The allocation cases** (`SystemNode.test.cpp`, `[alloc]`), each red before its fix:
  - *the ring* (`[mesen][audio-ring]`): fill, drain, `availableFrames`; a wrap whose tail runs off the end and continues
    at the front, the whole drained sequence checked frame by frame; a reserve that keeps the buffered frames and a
    smaller one that does not shrink; a write past capacity that drops the newest and counts them (release builds only:
    a debug build asserts there, by design); and a warm ring writing and draining 64 x 1024 frames with zero allocations
    and zero frees. It also pins `kMaxPlayBufferFrames == 0x8000` and the default block size against the engine's.
  - *the audio path* (`[system-node][alloc]`): a Mesen core drains without allocating at every ragged size (33, 1, 222,
    1, 7, 4096, 2, 8192), the first block at each size being the counted one, for NES mixed, NES with its pins wired,
    NES with the tap armed over the mix, SMS and GBA. Every GBA section, here and below (`[video]`'s row included),
    reads a sibling fixture ROM and `SKIP`s, counted, where the resources tree has none, as CI's does, so the gap is
    visible rather than a silent pass.
  - *the narrow host* (`[mesen][tap][alloc]`, section 10): a lone NES whose tap is armed over the mix, which is what a
    split routing gets on a host too narrow for it, run 2000 one-frame blocks past the capacity the old drift filled,
    with the mix ring's fill within one block plus one flush every block and nothing dropped, a counted 512-frame drain
    on that path with emulated frames in it, and a block-for-block comparison with an unarmed NES that must match in
    audio and in work RAM.
  - *the whole block*: a warmed core's whole block allocates and frees nothing, emulation included: NES mixed, NES with
    its tap armed, NES through the Engine (the whole graph block, so the system node's per-block work is inside the
    window), SMS, Game Boy (the mix and the four stem pairs) and GBA. Each direct-core section warms through the real
    triad with a button press (a device's state capacity is its constructor's, so no poll sizes one), and **each of
    their counted windows stages a block's host input inside it**, the way a node applies its inputs before the core's
    `prepareForBlock`: notes, clocks, a raw run and a settings-length SysEx into a NES (and a section with the reliable
    link on, whose ROM capability byte is flipped so the framing path really runs), the transport's levels into a SMS,
    MIDI runs and a press burst into a Game Boy in both stream shapes, and presses into every console. The Engine
    section warms through eight graph blocks with no press and stages its bytes between blocks through the invoker,
    which applies a push inline while the audio thread owns nothing, so its counted window carries the node's
    application of what the host staged rather than the staging, and the accepted byte count read after the window is
    what holds that the staging reached the core. The press counts and run lengths are past what a library deque
    allocates its first node for, so a queue that grew on the block thread shows in a window even when it would not
    grow every block. Each counted window proves it emulated (the node-level ones by the frames the capture holds, by
    the same measure the step loop gates on, or SameBoy's audio frame count; the Engine one by its work RAM moving at
    the plan's largest size), so a window whose step loop stopped cannot be vacuously free, and each holds that what it
    staged landed: `inputDropped()` unchanged, and the non-press door's own count exact (a NES's 12 x 271 bytes, a
    SMS's 12 x 4 levels, a Game Boy's 12 x 72 serial bytes, a GBA's 2 + 12 x 12 presses).
  - *the live arm*: a tap armed on an activated NES sizes nothing on the block thread and its audio path stays free. It
    drains more than the armed ring holds, so head and tail wrap mid-drain. One section arms a core it built and
    activated directly, with no node's activation to reserve for it, which is where `onActivate` sizes the tap's rings.
    It SKIPS where `countsCAllocations()` is false (a sanitizer build, Windows, macOS), since a stream's blip, a C
    block, is what it would catch.
  - *re-activation*: a node reactivation grows the ring to the new block, through `GraphHost::reactivate` (what
    `Engine::setMaxBlockFrames` does).
  - *the plan size*: a node sizes its core's block buffers to the plan it was built by, not the engine's default, and a
    direct build keeps the default.
- **The counting allocator counts C allocations on request.** `test/graph/CountingAllocator` replaced `operator new`
  only; it now also interposes `malloc` / `calloc` / `realloc` / `free` and forwards to glibc's own, so a blip's or
  Mesen's C block is seen. That is opt-in per window (`beginCountingAllocations(true)`), and it is off in a sanitizer
  build (ASan and TSan own the allocator: the first attempt segfaulted TSan at startup), on Windows (the static CRT,
  where defining `malloc` collides with `libcmt`) and on macOS (no glibc name to forward to); `countsCAllocations()`
  says whether the binary sees them. Forcing it on changes exactly one existing case: the script block path's 448
  allocations are QuickJS's own, which that case measures through QuickJS's counters. The audio-path and whole-block
  helpers call `beginCountingAllocations(true)` unconditionally and do not skip where `countsCAllocations()` is false:
  on Windows and macOS they prove the operator-new half of the promise, where the live-arm case skips because a C block
  is its one subject.
- **The `[video]` hashes.** Three 4096-frame blocks per console, the frame the core published hashed by FNV-1a over its
  width, height and XRGB8888 bytes, recorded with the video-path files restored to the commit before the plumbing and
  equal after it: NES 14252490387203307000 three times (the boot screen, before bliptoaster draws), SMS
  16816052646246439624, 16816052646246439624, 668230188373870873, Game Boy 8259550387572866035, 10074299620259861403,
  13782190143996438851, GBA 3100562949282106931, 3100562949282106931, 2667698829920294451. The hashes are per console
  and per ROM; a real video change re-records that console's row, as parity re-records a group. The NES's three are its
  boot screen, before bliptoaster draws, so the console whose per-frame plumbing changed is held to one image; a later
  change that wants the drawn path should hash frames after bliptoaster starts drawing.
- **Mesen's shared classes, directly** (`MesenShared.test.cpp`): a cleared controller device's state stays empty (two
  of the four devices that read emptiness as data depend on it; the other two are section 9's, not this file's) and a
  hub's composite state round-trips a pressed button through a second hub; a notification's listener copy survives a
  listener destroying another in a nested send, the manager and its copy holding only `weak_ptr`s; and a send hands
  its references back when it returns, so the next manager's send frees nothing after another console's listener has
  died.
- **The tap as a capability:** `NesSplitRouting.test.cpp`'s nine cases read it through `channelTap()`, the goldens'
  "each NES ends in the mode it was built in" reads it (and fails on a NES slot that offers no tap, rather than skipping
  the check), "the tap is a capability only a NES offers" holds a NES's tap and its own accessors to one mode and the
  Game Boy, Master System and GBA to null, and the ScriptPacks reload case reads the tap rather than casting.
- **The kind check:** "a node refuses a core built with another console's kind" is a 12-way matrix (each of the four
  consoles against the three other kinds), requiring the refusal, `activate-failed`, the same-node message, no built
  core and the core given back. It also pins kind uniqueness: six requirements that the four cores' `kKind`s are
  pairwise different, and each row skips the column at its own index rather than the one holding its own value, so were
  an enumerator ever duplicated the requirement names it and the matrix still tries it. The seam is `NodeRig`'s
  `reportKind`, which wraps the real core in a `KindWrapper` that owns it, so the refusal's destroy frees the core and
  the rig stays truthful: the real host cannot produce the mismatch.
- `kind()` is no longer dead: the node's check is its production reader.
- **The input queues**, added after this design's first draft (section 11): `FixedQueue.test.cpp` in
  `retroplug-graph-test` holds a push past the bound and the count, a consumed prefix's compaction keeping the live
  order, `FixedByteRing`'s wrap over more than its capacity, and `rp::SortScratch`'s stability on equal keys, its
  order against a reference stable insertion sort over two thousand random runs, and its bound: a full reversed queue
  (the Master System's 74752 levels) costs at most n * (ceil(log2 n) + 1) entry moves and allocates nothing, where the
  in-place insertion sort it replaced cost n * n / 2; a case reads `ports.ts`'s four
  declarations ([nodes/ports.ts](../packages/retroplug/src/script/nodes/ports.ts)) and compares
  them with `InputCapacity.hpp`, so either side drifting is visible; and a `[input-drops]` case has a section per core
  queue (NES, SMS, GBA and Game Boy presses, a Game Boy's serial bytes and runs, an SMS's levels, a NES's N8 runs and
  bytes) that pushes past the bound, holds the refused count and the accepted count exactly, and then runs a block so
  the core still fills; a Mesen press queue is the one with no refusal path (it holds a transition a button, and its
  bound covers the button range), which the case says instead. That case also reads the transition a button's queue
  entry holds, through the core's own `peekPendingPress` (`SameBoySystem::peekSerialByte`'s shape): a burst leaves
  `inputAccepted()` and `inputDropped()` the same whether the coalescing replaced the queued entry or kept it, so the
  surviving entry's value is the thing to hold. Each section then reads the pad's own bit after the block through
  `buttonDown` (`BaseControlDevice::IsPressed` by the console's control manager), so a block that emptied the queue
  without applying it, or applied only the first entry, fails rather than passing on the queue read alone. The audio suite has the FIFO's own cases (the
  permissive fifo_a past `_rxBound`, the wire past a block's input), the NES's carried-run rebase, and the reliable
  link's 48-frame window giving up the window and re-syncing.
- **ThreadSanitizer and AddressSanitizer** ran over every case touched (the `[alloc]` and `[input-drops]` tags, the
  `[control-state]` / `[notification]` / `[video]` cases, the tap and kind cases, the audio suite's `[channelsplit]`
  cases) with no report, and the profile benches stayed at zero allocations a block.

## 7. Landing, as built

Six commits, each green:

1. **P1:** the dead surface: `refactor(system): SystemBase drops the members nothing calls`.
2. **P2:** the audio ring, the scratches, `_streamBuf`, and the allocation cases:
   `fix(mesen): a Mesen core collects its audio in a fixed ring, so its audio path allocates nothing`.
3. **The Mesen plumbing** (section 2's second decision): `fix(mesen): a frame's video and input plumbing reuses its
   buffers, so a whole block allocates nothing`.
4. **P5 + P6:** the tap capability and the build check:
   `refactor(engine): the NES tap is a capability any core may offer, so the engine names no console`.
5. **The input queues** (section 2's third decision): `fix(system): a core's input queues are preallocated rings, so a
   block carrying host input allocates nothing`. Its own commit, before the docs commit, which describes all six.
6. **Docs:** spec/02 (the core surface, the tap, the ring, the allocation promise and the input bounds and counters),
   spec/06, spec/07, spec/08 (native allocations are tested now), AGENTS.md's engine-graph paragraph, the main design's
   sections 6.2, 6.4, 6.5 and 8, and this design's status, section 10 and section 11.

Then the acceptance run. As C3: a prototype on a local branch (`proto/c4-cores`) on DeepSeek, each stage reviewed and
fixed; the plan generated from it and replayed to identical trees; an Opus review of the plan before execution;
Flash-Next implementers checked by tree identity; Opus code reviews of the two code-heavy tasks; follow-ups, an
acceptance run and an Opus final review.

## 8. Prototype questions, answered

- **Does the zero-allocation case find any allocation besides the ring, `_streamBuf` and the scratches?** Yes, five
  classes of them. Mesen's video and input plumbing, 13 to 18 allocations an emulated frame, which is neither ours nor
  the audio path and is section 9's; our own N8 firmware parser, whose per-command vectors grew inside the block (7 at
  4096 frames, 7 more at 8192), fixed there; the tap arm's three `blip_new`s, fixed there; SameBoy's lazy scratch
  `assign`s plus its dead `pendingMidi_.insert`, fixed there; and, once the counted windows staged a block's host input
  (section 11), every ingress queue a press, a MIDI run, a serial byte or a raw message landed in, fixed there. Nothing
  else turned up in NES, SMS, Game Boy or GBA.
- **Is Mesen's 32768-frame cap the true bound on one `PlayBuffer`, for every console and every sample rate?** Yes. It
  is the mixer's output buffer (`_sampleBuffer`, 0x10000 `int16`, stereo: 0x10000 / 2 frames, passed whole as the
  resampler's output cap), reached the
  same way on all three consoles, and independent of the host rate; the pitch-adjust path is capped lower. The step
  loops cannot accumulate more over a block because they stop as soon as `availableFrames() >= framesNeeded`, so the
  overshoot is at most one flush. The overflow counter stayed 0 in every case, an 8192-frame block included.
- **Does the case need `CountingAllocator` linked into `retroplug-engine-graph-test`, and how do the existing
  no-allocation cases do it?** It was already linked (`cmake/tests.cmake` puts `test/graph/CountingAllocator.cpp` into
  the binary, shared with `retroplug-graph-test`), and the cases bracket their window with
  `beginCountingAllocations()` / `endCountingAllocations()` exactly as the existing ones do. It counts per thread, which
  is why a window is the blocks the calling thread runs. The C half had to be added for the arm's blips and for Mesen's
  C blocks, and is opt-in (section 6).
- **Which tests read a P1 member, and what each moves to?** None did. The only `kind()` readers still compile because
  `kind()` stayed; no C++ or TS test read the sav accessors, `clearSram`, `loadStateBytes`, the `fastBoot()` getter, the
  core-side `midiOut()` or the memory-snapshot quartet. So no test moved and none was deleted.

## 9. The Mesen plumbing stage, as built

Section 2's second decision, one commit of its own between the ring and the tap. On the commit before it, over 4 x
8192-frame blocks of real emulation: **738 allocations** for a NES and 738 for a SMS, about 18 an emulated frame, on the
audio thread. On it, the same windows are 0 allocations and 0 frees. Attributed by source with a temporary
return-address hook (removed before the commit):

| Mesen site | per frame | fix |
|---|---|---|
| `BaseControlManager::GetPortStates` | 3 (vector growth) | fills the manager's own `_portStates` and returns it by `const&` |
| `BaseControlDevice::GetRawState` | 2, one a device | a new `GetRawState(ControlDeviceState&)` fills the caller's state, whose vector keeps its capacity; the returning form delegates to it, so its other callers are untouched |
| `RenderedFrame`'s `InputData` | 12 | `RenderedFrame::Reset` + `SetInputData` assign in place, a frame member per PPU (`BaseNesPpu`, `SmsVdp`, `GbaPpu`), `VideoDecoder::_convertedFrame`, the persistent frames refilled rather than assigned, and the by-value `RenderedFrame` parameter made `const&` |
| `NotificationManager::SendNotification` | 1 | the copy it iterates (it must be a copy: a listener may unregister mid-notify) is reused per thread and per nesting depth, pooled as `unique_ptr<vector<weak_ptr<...>>>`, so growth moves pointers and never the buffer a send is iterating; each send clears its buffer on the way out (capacity kept), so a dead listener's control block is not freed by the next send |

`VideoRenderer::UpdateFrame`'s `_lastFrame = frame` was on the list and turned out to allocate nothing, since copy
assignment into a persistent member reuses its vectors' capacity; it needed no change and still measures zero.

**The `ClearState` contract is kept, which is what made it delicate.** `_state = ControlDeviceState()` freed the state's
vector and left it EMPTY, and an input poll clears every device before re-reading it. Four devices read emptiness as
data: `ControllerHub::UpdateStateFromPorts` appends `[len][bytes]` to what a clear leaves, and `BarcodeBattlerReader`,
`DatachBarcodeReader` and `FamilyBasicDataRecorder` treat `size() > 0` as "the tape plays". Sizing the state to its
maximum (64 bytes) in place therefore broke them, the Epoch reader throwing `std::length_error` from every poll. So the
state stays EMPTY (`_state.State.clear()`) and its capacity is reserved to `ControlDeviceState::MaxBytes` in the
device's constructor, off the block thread, before any read can grow one inside a block; `GetPortStates` reserves the
same capacity on each snapshot entry's state before filling it, so a port that appears late (a mapper or role device
the control manager registers a few frames in) does not grow five snapshot destinations inside a block. A device no ROM
has read yet therefore carries an empty state, as upstream's did, where the first version's padding gave it 64 NULs.
A savestate blob grows by about 62 bytes a device as a result; old savestates still load, since Mesen reads a
vector's length from the blob. The redundant `clear()` in `SetTextState` went.

**What the same commit fixed in our code.** The N8 FIFO's parser reserved its per-command vectors inside the block:
`_params` is now reserved to the protocol's largest command frame (`3 + 0xFFFF`) at construction and `_writeBlock` to
`ACK_BLOCK_SIZE`. The arm's three `blip_new`s went to `blip_reset` (section 4). SameBoy's scratch sets are sized from
`blockFrames()` at activation and in `reserveAudio`, so `prepareForBlock` only zeroes them, its lazy growth kept as the
fallback for a direct core run at a bigger block than it reserved; and its dead `pendingMidi_` and `onMidi` override are
gone (P1's finding: nothing read them, and the `insert` could allocate on the block thread), so a MIDI-framed event to a
Game Boy now reaches `SystemBase`'s no-op.

**The SD path, what was cheap and what remains.** `execFileRead` and `performDma` share one `_ioScratch` member instead
of allocating a vector per command, and `_rxQueue` is a vector-backed FIFO (a head index, compacted as it is read,
reserved to twice its live bound, section 11) rather than a `std::queue`, whose deque allocates its first node inside
a block. What still allocates there, and why:

- `readParamString` returns a `std::string` a command, and `toHostPath` builds a `std::filesystem::path` plus strings:
  the allocation is inherent to those types, and an SD command is a ROM-initiated once-a-command event, not per frame.
  `_openFilePath`, `_openFile`'s buffer and `create_directories`' path are the same class.
- `execDirLoad` fills `_dirRecords` (cleared but not shrunk between listings, so only a bigger listing grows it), and
  each `EdioDirRecord::name` is a `std::string` from a `directory_entry`.
- `_txQueue` (CMD_USB_WR with no sink) grows while a ROM writes and no host drains it, unbounded on purpose: a ring
  would change the "never drop a byte the device would deliver" contract.
- `_ioScratch` is the SDK's largest read (`FILE_READ_MAX`), and both readers take the file a chunk of that size at a
  time, so a length taken from a u32 cannot grow it, and no read gives memory back (a second allocation) in the block
  that asked for it.

None of them is reachable from the block cases (the in-repo NES ROM never issues an SD command), so the whole-block case
is ROM-dependent for the SD path.

## 10. Found while building

The findings that changed a section are in it: the ring's overflow policy and the block size in the build request in
section 4; the tap's interface, the NES being its own tap and the mode range staying with the engine in section 5; the
allocation cases, the C-counting options and the `[video]` hashes in section 6; the plumbing's four Mesen sites, the
`ClearState` contract and the SD path in section 9; and the ingress queues, their bounds and the in-place sort in
section 11. The follow-ups' fixes are here too, each tagged with the section it changed. The ones worth naming on their
own:

- **The design's "zero allocations" could not hold for a Mesen console as vendored** (sections 2, 4, 9). Mesen's video
  path allocates 13 to 18 times an emulated frame on the audio thread, so any window with an emulated frame in it can
  never be zero. The stage that found it kept the promise strict by counting a drain of a pre-filled ring; the plumbing
  commit then fixed the path, which is what makes the whole-block promise literally true.
- **C2's finding was not what it looked like** (sections 4, 8). Its "8 allocations over two 512-frame blocks" that the
  design attributed to the first block after a setting change are the video path's per-frame cost, present in every
  block; C2's idle-versus-edited comparison was equal because both sides paid it. This design's section 1 has no such
  item left.
- **The blips' phase is audible** (section 4). `blip_clear` seeds the blip's fractional offset from `factor / 2`, so
  *when* a blip is created and rate-set moves the tap's first samples. A "pure" preallocation is therefore an audio
  change, and parity caught it (`parity/edits-structure`, `pinSplit-then-stereo`, lane 0: the same RMS and a different
  digest) before it landed; the capture mode's arm and disarm also run on every plan change while tapped.
- **The periodic publish is a trap for any `[alloc]` case** (sections 4, 6). It fires inside `finishBlock` every 0.5 s
  and allocates thousands of times in Mesen's serializer, and it is only visible once a case drains more than 24 k
  frames. The cases on an Engine-built core charge a whole interval uncounted before each counted stretch.
- **The C interposer must stay off under sanitizers** (section 6). Under TSan, defining `malloc` and `free` segfaulted
  the binary at startup, a block the sanitizer's allocator made being freed through glibc's. So the sanitizer runs hold
  the operator-new half of every case, and the arm's blip count is only visible in a plain build.
- **`ClearState` mattered as much as `EnsureCapacity`** (sections 6, 9), and the first version of the fix broke the
  controller hub and the two barcode readers and the Family BASIC recorder, which is why the case is in the suite. A
  later pass moved the state's capacity reserve into `BaseControlDevice`'s constructor, where no control-manager poll
  can be what sizes it, and dropped `GetPortStates`' padding of the snapshot entry: a device no ROM has read carries an
  empty state, as upstream's did.
- **The counted allocation every dozen blocks with the tap armed over a mix** turned out to be a video frame: the step
  gate was then the tap's ring, which `finishBlock` discards, so each 1-frame block emulated a whole flush window and a
  video frame landed every dozen (sections 6, 9). With the gate on the drained ring (below), such a block emulates its
  own frames and the video frame lands at the emulator's own pace.
- **The ring's overflow was reachable through the narrow-host defect, so the defect is fixed rather than recorded
  again** (sections 4, 6; the B0 design's fifth defect). A lone NES under a split routing on a host too narrow for it
  arms the tap (`buildGraph`'s `tapArm`, `Engine::syncTap`) while the node wires the mix, and the core gated its step
  on the tap's rings: the block drained one block from the mix ring and left each flush's overshoot there. The vector
  ring had turned that into ever-growing latency; the fixed ring fills after about 17 s at 48 kHz in 512-frame blocks
  and then drops audio every block (a debug build asserting in `PlayBuffer`). The user's decision was to fix it in its
  own commit, with the parity cases that pin it re-recorded, as a pinned defect is fixed, not to record it again. The
  step gates on the ring the block drains: the node tells the core the block's lane count before it steps
  (`SystemBase::setBlockLanes`, from the same `lanes()` lookup `finishBlock` reads), and the NES gates on its tap's
  rings only when the block drains them. A mix-drained block is then the unarmed core's block, bit for bit, in the
  console's frames as well as the host's, and the mix ring keeps at most one flush's overshoot, as the plain mix path
  does. The engine still arms the tap on a host too narrow for the split, so that capture work is wasted; not arming a
  split that does not fit is a separate, CPU-only change. Eleven parity cases moved, every one of them the defect's
  path, and they reach it two ways: a split routing on a host too narrow for it, the mix wired and the tap armed
  anyway (`routing-split`'s five, `nes-channelSplit-o2/o4`, `nes-pinSplit-o2` and `nes-stereoPinSplit-o2/o4`), and a
  NES whose mesen role carries a non-mix `channelExportMode` under a mix wiring, whose capture is on from construction
  at any host width (`render-shapes`' `perChannel-nes-pinsPlusRef`, `-stereoModPins-role` and
  `-individualMono-role`, built through the role under the default Stereo routing); that group's
  `perChannel-nes-pins`, `perChannel-nes-channels` and `perSystem-nes-channelSplit` reach the first way, their boot
  segment rendering a split routing at 2 channels before their own shape render. The plugin could reach the second
  way, its 8 outputs included, so an earlier record's "always passes 8 outputs and could not hit it" was wrong. A
  third place nothing recorded: the render library builds a NES split export with its capture on and settles 1500 ms
  through `renderAudio` with the mix wired, so CLI `render --split pins|channels` and System > Render's NES exports
  start from a different, now unarmed-equal console state (a follow-up's `nes-capture-mix-drain.test.ts` settles an
  armed export and a plain mix NES in turn and compares the next second sample for sample). The case that holds the fix
  runs the narrow path past the old ring's capacity in one-frame blocks with the fill bounded and nothing dropped,
  counts a real block's drain on that path, and compares it block for block with an unarmed NES, on sound and on work
  RAM.
- **Without the block size in the build request, a memory saving was lost** (section 4). A core built at the 8192
  default and then reserved down keeps the larger ring for its life, so a DAW running 64-frame blocks would pay for a
  plan it never runs; the request's `maxBlockFrames` is what sizes it to the plan from construction.
- **The promise's counted windows carried no host input** (sections 4, 6, 11). The cases warmed with a press and then
  staged nothing, so every queue a block's input lands in could grow on the block thread with no case seeing it. The
  extended case found 7 allocations and 7 frees at the NES's first counted window (11 and 7 with the reliable link on),
  1 and 4 through the Engine, and 4 and 2 on a Game Boy carrying presses and serial bytes, all of them queues; the fix
  is section 11's. A later pass made each counted window hold that what it staged landed: `inputDropped()` unchanged
  across every window, each non-press door's count exact (`acceptedBytes()` 12 x 271 on a NES, `levelsAccepted()` 12 x 4
  on a SMS, `serialInAccepted()` 12 x 72 on a Game Boy, and presses 2 + 12 x 12 on a GBA), a Game Boy's stems half run
  on a fresh core, so the presses the first half scheduled cannot fill the queue the second half stages into, and the
  Engine section's acceptance read after its staging, in a comment that says the staging is the invoker's and happens
  between blocks.
- **libstdc++'s `std::deque` allocates its first node eagerly** (sections 6, 11). A one-block burst of tens of entries
  hides a deque regression, so the `[alloc]` case's press counts and run lengths (64 presses, 24 messages a block) are
  sized past that on purpose; a later case that stages less will not see a re-introduced deque.
- **`std::stable_sort` allocates** (section 11). It takes a temporary buffer, so the two roles' once-a-block offset
  sort was an allocation on the block thread; the in-place insertion sort that replaced it allocated nothing but was
  quadratic, about 2.8 G swaps (8.4 G counted entry moves) for a Master System level queue full and reversed, on the
  audio thread, so the sort is now a bottom-up merge through `rp::SortScratch`, sized with the queue: at most
  n * (ceil(log2 n) + 1) moves and still nothing allocated. The scratch is a second copy of the queue, held for the
  core's life (598016 bytes for a Master System role's 74752 levels, 52224 for the NES role's 4352 runs), so a role's
  queue costs twice its entries and the docs price it that way (section 11, spec/02).
- **A notification's pooled copy outlived its listeners** (sections 6, 9). Reusing the dispatch buffer per thread and
  depth kept its capacity but also kept the last send's `weak_ptr`s, so removing one of two Mesen systems made the
  survivor's next frame free the dead listeners' control blocks on the audio thread. Each send clears its copy on the
  way out now (capacity kept), and a two-manager case holds that the next send frees nothing.
- **A press burst could still leave a button held** (sections 6, 11). A Mesen press queue bounded by a block's two
  doors would refuse the tail of a quiescent run of more than 320 transitions, and the tail could be a release. Since
  the whole queue is applied before the emulator polls its controller, a transition for a button already queued now
  replaces its entry: the queue holds one transition a button, its real bound is the button range, and nothing is
  refused. That argument needs the wire-to-button map to be injective, and it was not: an unknown wire byte fell
  through to A on a NES and a GBA, so two bytes could name one bit and the surviving entries' order could leave a
  button pressed. Each console's map answers -1 for a byte it has no button for now (the SMS's Select was the known
  one), such an entry sets nothing, and a case holds each map at that. A Game Boy's press queue cannot coalesce (its
  transitions are scheduled and meant to be seen), so a quiescent flood of more than 1024 transitions still drops the
  tail, release included (section 12).
- **The N8's file staging buffer could be made to shrink** (section 9). `execFileRead` sized `_ioScratch` from the u32
  length a ROM asked for, so one huge read grew it inside a block, and the give-back that followed was a second
  allocation on the block thread to free memory. Both readers now take the file a `FILE_READ_MAX` chunk at a time
  through a buffer that cannot grow past it, the give-back is gone, and a case reads and DMAs twice that length with 0
  allocations and 0 frees.
- **Nothing proved the tap's rings were sized at activation** (sections 4, 6). A core built and activated directly,
  never reserved, sized its ring and its scratches but not its tap's rings, so the first arm allocated them on the
  arming thread; `onActivate` sizes them now, and the live-arm case arms exactly such a core under a counted window
  (red at 12 allocations before it).
- **The suite's own traps, which the follow-ups closed** (section 6): the ring case's wrap section never compared the
  frames the wrap left (it does now: the 400 that continue at the front, then the 100 the drain did not reach); the mix
  discard's red was a hang rather than a failure, so the chunk is a seam the case reads and a zero one ends the loop;
  the kind matrix skipped by kind value, which skips the very collision it exists to catch (it skips its own column by
  index now, and six requirements pin the four kinds distinct); a Debug configure did not compile the ring case
  (`if (!NDEBUG)` is `#ifndef NDEBUG`); and the "bit identical to the unarmed mix" section passed on the defective
  code because both sides were silent, so it primes the ROM, holds notes and runs 400 blocks.
- **The N8 role's rebase must compact its arena in ascending byte order, not queue order** (section 11). The sort by
  offset can permute the live runs, and a forward compaction in queue order overwrote a run it had not copied yet, so
  two feeds that both carried a run past the block end could swap payloads; a case stages exactly that and reads each
  payload back.
- **The permissive FIFO profile still has a storage bound** (sections 9, 11). `_rxCapacity == 0` means no modelled
  depth, not an unbounded vector: fifo_a's delivered bytes are reserved to `2 * _rxBound` (the live bound plus room for
  the consumed prefix `pop` has not compacted yet), and a reply past `_rxBound` is dropped and counted even then.
- **A comment's contract was its neighbour's, and the tap record's lifetime read backwards** (sections 3, 5):
  `saveStateBytes`' note stated `loadStateBytes`' bool return, and the `ArmedTap` note said the record cannot outlive
  its core, which invites following the pointer the compare-not-follow rule forbids; the record is compared and never
  followed (section 5), and only the tap object, a core subobject, cannot outlive the core it belongs to.
- **The engine names one console still, and it is the cart mirror's check** (section 5). `Engine::setGraph` checks a
  cart mirror's system for `SystemKind::MesenNes`, which is a fact about an N8 being a NES's cart reader; a reader
  grepping the engine and its table for a console name finds that one, plus the tap's 1-to-3 mode bound, and neither is
  a cast. The per-console node definitions `SystemNodes.cpp` holds are the seam C3 made for them.
- **The kind check's red was heap corruption, not a clean failure** (section 5). With the comparison removed, the node
  casts the wrong core and runs the Game Boy's application through a `KindWrapper`, so the failure is an immediately
  unsound call rather than a later one.
- **`MesenNesSystem` now has a second base**, so `ChannelTap`'s vtable is part of its layout (section 5). Nothing
  serialises the class or measures its size; the only ABI surface is the plugin's own, rebuilt with it.
- **`Engine`'s validation still belongs to the NES** (section 5, section 12): the tap is a capability but its mode bound
  is not, which only a second tap would show.
- **The suite keeps its console-only casts** (section 6): `EngineGraph.test.cpp`, `SystemNode.test.cpp` and two others
  still `dynamic_cast` to read a console-only member (a config field, `linkPeers_`, a ring counter), which is the core's
  own business rather than the engine's. What the tap capability changed is that no tap read casts any more.
- **`cmake --build build` does not relink the Catch2 binaries** (a build trap, not a finding of one section): they are
  `EXCLUDE_FROM_ALL`, so a library edit needs the test target built by name before a run, or a stale binary shows a
  fixed bug as unfixed.

## 11. The input-queue stage, as built

Section 2's third decision, one commit of its own between the tap and the docs. It answers the promise's one hole: the
`[alloc]` cases warmed with a press and staged nothing inside their counted windows, so a warmed core's block was free
only while its node handed it nothing. Extended to stage a block's input into every window (the Engine's staged
just before its whole-graph block, since the invoker applies a push inline while the audio thread owns nothing), the
case failed at its first counted window on every console: 7 allocations and 7 frees on a NES's mixed block, 11 and 7
with the reliable link on, 7 and 7 with the tap armed, 1 and 4 through the Engine, 1 and 1 on a SMS, 4 and 2 on a Game
Boy carrying presses and serial bytes. On the commit, every section is 0 and 0.

**What each queue became.** Every ingress queue is a fixed ring (`util/FixedQueue.hpp`: `rp::FixedQueue<T>`, one
reserved vector and a head index, `push` refusing past `capacity()` and counting; `rp::FixedByteRing`, a byte ring
consumed front-first; `rp::SortScratch<T>`, a stable bottom-up merge sort through a scratch buffer sized with the
queue, a second copy of its entries: 598016 bytes for a Master System role's 74752 levels, 52224 for the NES role's
4352 runs) sized when the core is built:

- **The three Mesen consoles' presses** were a `std::vector` grown by `push_back` (5 allocations for 12 presses). Now a
  `FixedQueue` of `kPressInput` 320 = `kButtonsInput` 64 + `GraphHost::commandCapacity()` 256, the two doors a block
  can press through (the buttons input, and `Engine::applyCommand(PressButton)` on the command ring), with a
  `static_assert` holding the ring's 256 to the host's. `prepareForBlock` already emptied the queue every block. It
  holds one transition a button: the whole queue is applied before the emulator polls its controller, so a transition
  for a button already queued replaces its entry, which makes the button range (a byte) the queue's real bound and
  means no burst can fill it. That is what keeps a release from being dropped behind a burst: a quiescent host (the
  CLI, an offline render) applies every push inline, so the command ring bounds nothing between two renders, and a
  plain FIFO's dropped tail could hold a release and leave its button pressed. The coalescing keys on the wire byte,
  so a console's map may name one bit for at most one of them: each of the three answers `-1` for a byte that console
  has no button for (the Master System's Select is the known one), and such an entry sets nothing. Were two bytes to
  name one bit, the entry that survived would stand in the other's place and the block would apply that order, which
  is a press surviving its own release; a case holds each console's map at that.
- **A Game Boy's presses** were a `std::deque`, whose first node is allocated eagerly. Now a `FixedQueue` of 1024,
  sixteen blocks of a saturated buttons input, because a press is scheduled 10 ms into the future and carried across
  blocks; a press past it is dropped and counted. Unlike a Mesen console's, these transitions cannot coalesce (each is
  scheduled and meant to be seen), so a quiescent session that pushes more than 1024 transitions between two blocks
  drops the tail, and the tail may carry a release (section 12).
- **A Game Boy's serial-in** was a `std::deque<PendingByte>`, a byte an entry. Now runs in a `FixedQueue` (a message's
  bytes share its frame, so they extend one run) over a 147456-byte `FixedByteRing`, two blocks of `serialIn` or one
  `serialOut` block; the front byte a bit shift reads is the ring's head, so nothing moves and nothing grows.
- **The NES N8 role's two feeds** were `std::deque<PendingByte>` for the raw feed and, under the reliable link,
  `std::deque<PendingMsg>` with a fresh `std::vector` per message. Now one run queue of 4352 descriptors over one
  74752-byte arena, shared by both feeds and both modes: a run's bytes stay where they were appended and only the
  descriptors sort, so a long SysEx is one descriptor and one copy.
- **A Master System's sync levels** were a `std::deque<PendingLevel>`. Now a `FixedQueue` of 74752 levels,
  `kMidiInput.bytes + kBytesInput.bytes`, because a message longer than a `MidiEvent` reaches the raw door at up to
  72 KiB; a level past it is dropped and counted.
- **The reliable link's sender window** was a `std::deque<Frame>` plus a `std::vector` a frame plus a `std::vector`
  collecting the message being parsed. Now 48 inline frames of `3 + kMaxMessage` bytes, the parsed message a
  `kMaxMessage + 2` array (one longer was never framed before either), the pending sync two bytes.
- **The N8 FIFO's two stages**: its wire queue was a `std::queue` (a deque) and fifo_a's delivered bytes a vector that
  could grow past their bound. Now the wire is a `FixedQueue` of a block's host input (74752 bytes), and fifo_a's
  bytes are reserved to `2 * _rxBound` (`_rxBound` = 78849, a block's input plus `FILE_READ_MAX` plus one), because
  `pop` only compacts the consumed prefix once its head passes half the vector's size.

The bounds are `system/InputCapacity.hpp`'s: what the outputs that feed a system node's inputs declare (a script
node's `midi` and `serialIn` 4096 messages / 72 KiB, its `bytes` 256 / 1 KiB, its `buttons` 64; a Game Boy's
`serialOut` 8256 / 8256). The four script declarations are the numbers `script/nodes/ports.ts` gives, and a case reads
both sides to hold them equal; the Game Boy's `serialOut` capacity is native (`SystemNodes.cpp`), so no `ports.ts`
number ties it. A core cannot read its node's inputs when it builds (a wire carries its source's queue, and an input is
bound only after `activate`), so the constants are the contract rather than the plan's own capacities.

**What a refusal costs.** A push past a bound is dropped, not grown: `FixedQueue::push` answers false, the queue counts
it, and each core reports `inputAccepted()` and `inputDropped()` (`SystemBase`), which sum its queues. A Mesen press
queue has no refusal to reach: its coalescing bounds it by the button range, inside `kPressInput`. A run past the
N8 role's bound is dropped whole and counted as its bytes; a level past the SMS bound likewise. The reliable link is
the one exception: a full window gives up the window (its unacked frames) and re-syncs, the policy it already had, so
it refuses nothing and has no dropped count.

**The FIFO's permissive profile now has a storage bound.** `_rxCapacity == 0` meant no depth limit and a vector that
could grow; it now means no modelled depth, since a reply is still refused past `_rxBound` and counted in
`_droppedBytes`. The wire's own staging is a block's host input for the same reason: it stands for the host's outbound
buffer, and a host that outruns a modelled wire by more than a block is now a counted drop.

**What did not change.** Every queue's accepted behaviour is the same: the same bytes reach the cores in the same order
at the same frames, the N8 role's sort and drain order are unchanged, and the link frames the same bytes with the same
sequence numbers. Parity held bit for bit with nothing re-recorded, and the `[video]` hashes are the ones they were.
The stage's one real bug is section 10's rebase finding: a case stages two runs past the block end and fails as
`217 == 144` before the fix.

**Where a drop is visible.** `inputAccepted()` / `inputDropped()` on the core, and
`coreTransportStats().droppedBytes` for the FIFO. Neither is surfaced in the host yet: `GraphHost`'s per-instance drop
accounting takes them nowhere, because `rp_process` carries no instance-drop pointer, so they are read by a test and by
the core itself (section 12).

## 12. Left as it is

- **A Game Boy's press queue drops the tail of a quiescent flood** (sections 6, 11): 1024 transitions, sixteen blocks
  of a saturated buttons input. Its presses are scheduled 10 ms apart and carried across blocks, so unlike a Mesen
  console's they cannot coalesce, and a session that applies pushes inline between blocks (the CLI, an offline render)
  can queue more than that before a block drains them; what is past the bound is dropped and counted, and the tail can
  carry a release, leaving its button held. A press a block cannot lose would need a coalescing rule of its own or a
  headroom reserved for releases, which is its own change.
- **The GBA's counted cases are fixture-only** (section 6): its ROM is a sibling resource, so the `[alloc]`,
  `[input-drops]` and `[video]` sections `SKIP` on a run without the resources tree (as CI's) and prove nothing
  there. The skip is counted, so the coverage gap is visible.
- **P3, the ROM borrow:** a system holds its ROM in its node's pinned resource and again in its core (up to 32 MB for
  a GBA); a true borrow needs a regenerated vendored SameBoy patch, and an SMS re-reads its staged file on reset.
- **P4, the snapshot double copy:** the registry copies each core's published frame and savestate again; that copy is
  the seam the rule "the control plane never touches a live core" rests on.
- **P7, the capability split and per-console node files:** best done when C5's hardware nodes need the interfaces.
- **The N8 firmware's SD path still allocates** (section 9): `std::string` and `std::filesystem` in the command
  handlers, a bigger directory listing, and `_txQueue`'s unbounded growth. Bounding the queue would change the "never
  drop a byte the device would deliver" contract, and a case that drove SD commands would have to account for the rest.
- **The 0.5 s state publish allocates** inside Mesen's streaming serializer, on the block thread, every 24 k frames at
  48 kHz. It is the one allocation a long run of an Engine-built core makes that neither a ROM command nor an armed
  serial-out capture triggers, and it is why every `[alloc]` case on such a core charges an interval first.
- **The cores this fork does not run** (PCE, SNES, WS) still copy a frame's snapshot once more per emulated frame;
  `RenderedFrame`'s by-value constructor says so. Only the NES, SMS / Game Gear and GBA paths are copy-free.
- **The tap's mode bound is `Engine`'s, not the capability's** (sections 5 and 10): the later move is `maxMode()` on
  `ChannelTap`, beside the mode it validates.
- **`C3`'s stale-tap-address limitation keeps its shape** at the tap's address (section 5): the record survives a drop
  and reclaim, and a core whose tap landed where the armed tap was freed would be restored onto. TS cannot get there.
- **The input-drop counters are not surfaced yet** (section 11): `inputAccepted()` / `inputDropped()` on a core and
  `coreTransportStats().droppedBytes` on the FIFO are read by tests and by the cores, not reported beside `GraphHost`'s
  per-instance output drops, since `rp_process` carries no instance-drop pointer and an input-drop service on the node
  seam is its own change.
- **Two output-side allocations remain** (section 2's third decision's scope was inputs): the N8 `_txQueue` is a
  `std::vector` a debug `drainCoreBytes` RPC swaps empty, so a non-link ROM's next `CMD_USB_WR` after a drain grows it,
  and a Game Boy's `serialOutLog_` is cleared each block but its capacity grows to the largest block's capture while
  MI.OUT capture is armed. Neither is on the steady-state path the `[alloc]` cases hold.

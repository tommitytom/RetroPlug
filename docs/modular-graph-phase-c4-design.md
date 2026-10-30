# Phase C4: the engine names no console

**Date:** 2026-10-31 · **Branch:** `feature/modularize` · **Status:** designed; the prototype answers section 8

After C3 a system node builds its own core, but the engine still knows one console by name and one core still allocates
on the audio thread. `Engine` finds a NES core with `dynamic_cast<MesenNesSystem*>` to arm its per-channel tap, a system
node `dynamic_cast`s the core it built to its own type, and every Mesen core collects its audio in a `std::vector` that
`resize`s on the block thread. `SystemBase` also still carries about ten members nothing calls. This phase removes all
of that: the dead surface goes, Mesen's audio collects in a fixed ring sized before the first block, the tap becomes a
capability any core may offer, and the node's build check stops casting. It is Phase C's fourth piece
([modular-graph-design.md](modular-graph-design.md) section 8: "Console-specific code moves into each node; every
`dynamic_cast` in section 6.5 goes; `SystemBase` shrinks to the capability interfaces; `MesenAudioDevice` gets a
preallocated ring"), after C3 ([modular-graph-phase-c3-design.md](modular-graph-phase-c3-design.md)). C4 is a part of
that bullet, chosen by the user (section 2); the rest is section 9's.

## 1. Before C4

- **`SystemBase`** ([SystemBase.hpp](../packages/native/src/system/SystemBase.hpp)) carries members with no production
  caller: the battery-path accessors `savSuffix` / `setSavSuffix` / `savPath` / `setSavPath` (the paths are TS's,
  `savPaths.ts`), the `fastBoot()` getter (fast boot reaches the core as a node setting), `clearSram` (a blank battery is
  a seed), `loadStateBytes` from outside (a savestate is a seed; the core's own seed path calls it internally),
  `midiOut()` / `midiOut_` (the old core-side MIDI out; the serial-out decoder replaced it), and a live memory-snapshot
  mechanism nothing enables (`enableMemorySnapshot`, `disableMemorySnapshot`, `memorySnapshot`,
  `publishMemorySnapshots`, `snapshots_`), whose publish every core's `finishBlock` still runs over seven empty slots.
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
build check), from a survey that split the main design's bullet into seven landable pieces. Left for later (section 9):
P3, cores borrowing their node's pinned ROM bytes; P4, collapsing the snapshot registry's double copy; P7, the full
capability split of `SystemBase` (main design section 6.4) and one node file per console.

## 3. P1: the dead surface

Every member section 1 lists goes, with each core's overrides and the calls that only fed them (the memory-snapshot
publish in every `finishBlock`). The core's internal seed path keeps its own `loadStateBytes` use (a private member or
an inlined restore: the prototype's call). `kind()` stays: P6 reads it. A test that reads a deleted member moves to what
replaced it (the node setting, the seed, the serial-out decoder) or goes when it only tested the deleted member.

## 4. P2: a fixed audio ring

`MesenAudioDevice` becomes a fixed-capacity ring of interleaved stereo `int16_t`: `PlayBuffer` writes at the tail,
`drain(dest, frames)` reads from the head, and `availableFrames()` keeps its exact meaning (frames written and not yet
drained), so the step loops and the N8 FIFO behave exactly as before.

**Capacity** is the plan's maximum block frames plus one Mesen flush (Mesen's mixer emits at most 32768 stereo frames
in one `PlayBuffer`, the cap of its own fixed buffer): about 160 KB a Mesen system at the engine's 8192-frame floor. A
system node is handed `maxFrames` in `activate`; it gives it to its core (`reserveAudio(maxFrames)`) when it builds the
core, and again when a host grows its block: `Engine::setMaxBlockFrames` reactivates the graph (quiescent, control
thread), and the node's `activate`, which returns early for a core it already holds, resizes there. A write past
capacity, which the bound makes unreachable, never allocates: the ring counts it (a counter tests read) and a debug
build asserts.

**The rest of the audio path:** `NesSoundMixer::_streamBuf` (the in-tree Mesen's per-channel tap ring) is bounded and
preallocated the same way, and the NES node's per-block scratches are sized at activation to `maxFrames`.

**The test.** A `retroplug-engine-graph-test` case (the suite's `CountingAllocator`, as the existing no-allocation
cases use) warms a NES (mixed, and with the per-channel tap armed), a GBA and an SMS, then runs ragged blocks up to the
maximum and requires zero allocations on the block thread. It fails before P2. It holds steady state: the first block
after a setting change, where C2 saw Mesen allocate inside its own emulation, is outside the promise. Another
allocation the test turns up is reported with its source; one in our wrappers or the audio path is fixed, one deep in
Mesen's emulation is recorded.

## 5. P5: the tap as a capability; P6: the build check

**P5.** `SystemBase` gains `virtual ChannelTap* channelTap()`, null by default; `ChannelTap` is a small interface,
`exportMode()` and `setExportMode(mode)`, which `MesenNesSystem` implements (the pattern `debugTarget()` already uses).
`SystemTable`'s slot holds `ChannelTap* tap` instead of `MesenNesSystem* nes`, filled from `channelTap()` where the two
casts were; `ArmedTap` remembers the tap it armed, compared by address as today (monotonic system ids keep that
sound). `Engine` stops including `MesenNesSystem.hpp`. TS does not change: the tap arm still names a system id and a
mode.

**P6.** The node's build compares the built core's `kind()` with the kind its type builds and then `static_cast`s; a
mismatch is refused exactly as now.

After both, no cast to a concrete core is left outside `packages/native/src/system/`.

## 6. Tests

- **Parity** holds bit for bit at every commit, nothing re-recorded: the ring changes where samples wait, not which
  samples are made. A moved case means the ring's semantics are wrong.
- **`retroplug-engine-graph-test`:** section 4's zero-allocation case (red before P2); a ring case (fill, drain,
  wrap, `availableFrames()` across a wrap, a resize on reactivation keeping what was buffered, the overflow counter);
  the tap read through `channelTap()` (`NesSplitRouting.test.cpp`, the goldens' "each NES ends in the mode it was built
  in"); a non-NES core offering no tap; a build whose kind mismatches its node refused (through a test seam: the real
  host cannot produce one).
- **ThreadSanitizer and AddressSanitizer** on every case touched; the profile benches still at zero allocations;
  both consumer suites (BlipToaster, NESVJ) from scratch copies, their repositories untouched.

## 7. Landing

Four commits, each green:

1. **P1:** the dead surface.
2. **P2:** the audio ring, the scratches, `_streamBuf`, and the zero-allocation case.
3. **P5 + P6:** the tap capability and the build check (one seam: the engine and the node).
4. **Docs:** spec/02 (the core surface, the tap, the ring, the allocation promise), spec/07, spec/08 (native
   allocations are tested now), AGENTS.md's engine-graph paragraph, the main design's sections 6.4, 6.5 and 8, and
   this design's status and section 10.

Then the acceptance run. As C3: a prototype on a local branch (`proto/c4-cores`) on DeepSeek, each stage reviewed and
fixed; the plan generated from it and replayed to identical trees; an Opus review of the plan before execution;
Flash-Next implementers checked by tree identity; Opus code reviews of the two code-heavy tasks (2 and 3); follow-ups,
an acceptance run and an Opus final review.

## 8. Prototype questions

- Does the zero-allocation case find any allocation besides the ring, `_streamBuf` and the scratches (Mesen's
  emulation itself, the N8 FIFO, the GBA or SMS paths)? Where, and is it ours?
- Is Mesen's 32768-frame cap the true bound on one `PlayBuffer`, for every console and every sample rate the engine
  supports?
- Does a test of the zero-allocation case need `CountingAllocator` linked into `retroplug-engine-graph-test`, and how do
  the existing no-allocation cases do it?
- Which tests read a P1 member, and what each moves to.

## 9. Left as it is

- **P3, the ROM borrow:** a system holds its ROM in its node's pinned resource and again in its core (up to 32 MB for
  a GBA); a true borrow needs a regenerated vendored SameBoy patch, and an SMS re-reads its staged file on reset.
- **P4, the snapshot double copy:** the registry copies each core's published frame and savestate again; that copy is
  the seam the rule "the control plane never touches a live core" rests on.
- **P7, the capability split and per-console node files:** best done when C5's hardware nodes need the interfaces.
- **Mesen allocating in the first block after a setting change** (C2's finding).

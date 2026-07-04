# The LSDj subsystem

## Status

**Proposed (relocations) + as-built notes.** The sav codec, kit compiler, and
kit-patch role all ship today and work; this doc proposes moving the parts that
aren't DSP off the audio thread and out of C++, and is honest about which of
those moves is cheap (kit patching, already the plan) and which is a genuine
lift with a real tradeoff (the version-aware sav codec).

## Why

LSDj is the reason `packages/native/src/lsdj/` is one of the larger native
subtrees, but almost none of it is realtime DSP:

- The **sav codec** ([codec/](../packages/native/src/lsdj/codec/)) turns 128 KiB
  SRAM images into a reflect-cpp model and back. It is invoked from the CLI
  harness (`savFromJson`) and the (planned) sav inspector UI — **never from the
  audio thread**. The emulator is handed raw SRAM bytes; it never sees the model.
- **Kit compile** (samples → 16 KB bank) *is* perf-sensitive (r8brain resample +
  enkiTS fan-out), but it's cold, UI-invoked, and off the audio thread already.
- **Kit patching** — deciding which kits go where and writing them into the ROM —
  is orchestration. Today a per-block audio-thread role (`LsdjKitPatchRole`)
  applies pending patches, which is more DSP entanglement than the job needs.

Per the [thesis](README.md), each of these wants a different disposition. This
doc sorts them.

## Design

### The sav codec — a TS candidate, but the biggest lift

The codec is a **version-aware binary codec on a reflect-cpp model** that is the
single source of truth:

- `decodeSav` / `encodeSav` ([codec/SavCodec.cpp:36,84](../packages/native/src/lsdj/codec/SavCodec.cpp#L36))
  handle the 128 KiB image (header, 'jk' magic, block-allocation table, 32-slot
  stored-project archive), plus a 32 KiB early-SRAM working-song-only fallback.
- `decodeSong` / `encodeSong` ([codec/SongCodec.cpp:288,410](../packages/native/src/lsdj/codec/SongCodec.cpp#L288))
  read/write the 0x8000-byte song body, dispatching on the format version at
  `0x7FFF` (the old-format `else` branches for fmt < 16 — command remap, wave
  play-mode rotation, synth/vibrato layout — carry most of the complexity).
- `compressProject` / `decompressProject`
  ([codec/Compression.cpp:23,80](../packages/native/src/lsdj/codec/Compression.cpp#L23))
  are LSDj's RLE + default-wave/instrument run codec.
- The model ([model/](../packages/native/src/lsdj/model/)) is **semantic** — enums
  are logical 0-based order, sub-byte fields use bounded `rfl::Validator` aliases
  ([model/Types.hpp:22](../packages/native/src/lsdj/model/Types.hpp#L22)) so the
  generated zod carries the range — and `FixedArray<T,N>`
  ([model/FixedArray.hpp](../packages/native/src/lsdj/model/FixedArray.hpp))
  reflects as a length-lenient `std::vector<T>` so a JSON fixture only specifies
  the cells it sets.

**Why it's a candidate.** It's pure byte↔model transformation with no DSP touch
and no audio-thread caller. The JSON/zod/TS rails already exist: the same model
drives `rfl::json` ([SavSerialization.hpp](../packages/native/src/lsdj/SavSerialization.hpp))
*and* the generated `SavTypes.ts` / `SavSchema.ts` (zod) via
`tools/gen-sav-ts.js` from the model's JSON Schema. TS already consumes the model
shape; only the byte codec is native. And the owner's own read is that the
reflect-cpp binary-codec approach "has its own set of issues" — a hand-written
bit-cursor codec ([codec/SavView.hpp](../packages/native/src/lsdj/codec/SavView.hpp))
bolted onto a reflection library is an awkward pairing.

**Why it's the biggest lift in this subtree — state the tradeoff honestly:**

| Consideration | Detail |
| --- | --- |
| It's not a leaf config module | Unlike `RecentFilesJson` etc., this is a real binary codec with version-branching decode/encode across ~13 format revisions. |
| The oracle is C++, test-only | Correctness for fmt ≤ 16 is proved by a **differential oracle** (`retroplug-lsdj-diff-tests`) that compiles vendored **liblsdj** (C) and asserts our decode matches it field-for-field ([test/LsdjDifferentialTests.cpp](../packages/native/test/LsdjDifferentialTests.cpp), [test/CMakeLists.txt:239](../packages/native/test/CMakeLists.txt#L239)). A TS port loses that oracle unless it's kept as a native cross-check harness or the corpus round-trip is trusted alone. |
| Round-trip must stay byte-identical | Encode passes unmodeled regions through from a template; 549 fresh-corpus savs round-trip byte-for-byte. A reimplementation must reproduce that exactly. |
| No RT deadline | The one point in its favour: nothing here is on a realtime clock, so a QuickJS implementation's speed is a non-issue. |

**Recommendation: defer the final call.** The codec is the strongest *fit* for TS
(pure transform, rails exist) but the *weakest* cost/benefit right now (it works,
it's tested by an oracle that doesn't port, and it's a big surface). Sequence it
**after** the leaf orchestration modules ([03](03-cpp-ts-boundary.md)) have proven
the boundary. If it moves, keep `retroplug-lsdj-diff-tests` as a native oracle the
TS codec is validated against, rather than throwing the ground truth away.

### Kit compile — stays native, as a primitive

`compileKit(name, samples) → 16 KB bytes`
([KitCompiler.cpp:66](../packages/native/src/lsdj/KitCompiler.cpp#L66)) is
genuinely perf-shaped: it fans one enkiTS task per sample (resample via r8brain
`CDSPResampler24` + 4-bit nibble-pack), joins, and assembles the bank
([KitUtil.hpp](../packages/native/src/lsdj/KitUtil.hpp)), backed by a content-hashed
`SampleCache` ([SampleCache.hpp](../packages/native/src/lsdj/SampleCache.hpp)) so
re-compiling a kit that shares samples skips re-decoding. This is exactly the
kind of cold-but-heavy binary work the [minimal native contract](README.md) keeps
in C++ — it belongs in the primitive set as `compileKit(samples) → bytes` that TS
calls, **not** a role and **not** something to reimplement in QuickJS. It's cold
(UI-invoked, never realtime), so the synchronous-from-the-caller model is fine.

### Kit patching — moves to TS orchestration; the role is eliminated

Today, kit patching is split across three layers:

1. UI/RPC compiles the kit (`PluginRpcService::compileAndPatchKit`,
   [PluginRpcService.cpp:1475](../packages/native/src/PluginRpcService.cpp#L1475))
   and stashes per-sample metadata on the config.
2. The 16 KB bytes cross to the DSP as a `PatchKitCommand`
   ([CommandQueue.hpp:193](../packages/native/src/transport/CommandQueue.hpp#L193)),
   drained at the top of `run()`
   ([PluginDSP.cpp:565](../packages/native/src/PluginDSP.cpp#L565)).
3. `LsdjKitPatchRole::onProcessBlock`
   ([roles/LsdjKitPatchRole.cpp:47](../packages/native/src/system/sameboy/roles/LsdjKitPatchRole.cpp#L47))
   applies pending patches by writing the bank into the **live emulator ROM** via
   `getMemory(Rom, ReadWrite)` + `OffsetLookup::kitBankForSlot`
   ([OffsetLookup.hpp:29](../packages/native/src/lsdj/OffsetLookup.hpp#L29)).

The role exists only so a ROM write lands on the audio thread between `GB_run`
steps. But **which** kits and **what** bytes is pure policy — it belongs in TS.
The proposed shape:

- **TS decides + computes.** TS holds the kit set (per-slot samples/effects),
  calls the native `compileKit` primitive for the bytes, and computes the target
  bank offset (`slot → bank * 0x4000`, the `OffsetLookup` table).
- **TS ships the result via a memory-write / load primitive.** Instead of a
  bespoke `PatchKit` command + per-block role, the patched bank crosses as a
  `writeMemory(system, Rom, offset, bytes)` (or an initial-ROM-load) primitive —
  a ROM write applied on the audio thread the same way any queued mutation is.
- **`LsdjKitPatchRole` (the per-block audio-thread role) is deleted.** There is no
  per-block work: a kit patch is a discrete, cold event, not something to poll for
  every block.

The one **irreducible DSP touch** is the **load-time, non-destructive** patch: on
project load a kit's compiled bytes must be laid into the loaded ROM before LSDj
runs (the base ROM stays untouched; the patched bank is re-applied each load).
This is precisely the `setState`/project-load orchestration the **control-plane
runtime** ([04](04-scriptable-runtime.md)) owns — the always-available TS runtime
runs the load, calls `compileKit`, and writes the bank as part of constructing the
instance — so it is *not* C++-bound to a role either. Today that recompile-on-load
already runs off the audio thread in C++ (`recompileMissingKits`,
[ProjectKitRecompile.hpp:47](../packages/native/src/lsdj/ProjectKitRecompile.hpp#L47));
it's the natural thing to hand to the control-plane runtime.

**As-built caveat — the kit UI isn't surfaced.** Kit patching is fully wired
end-to-end (compile → command → role → ROM → project round-trip) but **not
exposed in the menu**: the editor UI was incomplete and the owner shipped other
functionality first (a `grep` for "kit" over [ui/](../ui/) finds nothing). So the
relocation lands on a feature with no user entry point yet — which makes it a
*good* time to move it, since there's no UI to break, and the TS-side kit editor
can be built directly against the new primitive rather than the old command path.

## C++ vs TS

| Piece | Today | Proposed | Native contract it needs |
| --- | --- | --- | --- |
| Sav codec (decode/encode/compress) | C++ reflect-cpp binary codec | **Candidate for TS; defer** — pure transform, rails exist, but version-aware + oracle is C++-only | none new (already off audio thread); keep `retroplug-lsdj-diff-tests` as a native oracle |
| Sav model + JSON/zod/TS | C++ SSOT → generated TS | unchanged (SSOT stays until/unless codec moves) | `gen-sav-ts` codegen (exists) |
| Kit compile (samples → bytes) | native `KitCompiler` (r8brain + enkiTS) | **stays native**, exposed as a primitive | `compileKit(samples) → bytes` (exists) |
| Kit patch policy (which/where) | RPC + `PatchKitCommand` + per-block role | **moves to TS** | `writeMemory`/ROM-load primitive; `OffsetLookup` slot→bank |
| Kit load-time ROM patch | C++ `recompileMissingKits` + role on load | **control-plane runtime** ([04](04-scriptable-runtime.md)) | instance construction + memory write |
| `LsdjKitPatchRole` (audio-thread) | per-block `onProcessBlock` | **deleted** | — |

## Migration / build steps

Independently shippable, roughly in cost order:

1. **Expose the kit write as a plain memory/load primitive.** Add
   `writeMemory(system, Rom, offset, bytes)` (or fold kit banks into initial ROM
   load) alongside the existing `getMemory`/`readStateSnapshot` surface. Route the
   current `compileAndPatchKit` through it. Behaviour-identical; no UI change.
2. **Move kit-patch policy to TS.** TS holds the kit set, calls `compileKit`,
   computes bank offsets, and issues the write primitive. Build the (missing) kit
   editor UI directly against this path.
3. **Delete `LsdjKitPatchRole` + `PatchKitCommand`.** Once (1)/(2) land, the
   per-block role and its command have no callers. Remove them; `RoleConfig`'s
   `lsdj-kit-patch` alternative becomes plain persisted kit state, not a runtime
   role.
4. **Hand load-time kit application to the control-plane runtime**
   ([04](04-scriptable-runtime.md)). Replace the C++ `recompileMissingKits` seam
   with the runtime running compile + ROM-write during instance construction.
5. **(Deferred) evaluate the sav codec port.** Only after the leaf orchestration
   modules ([03](03-cpp-ts-boundary.md)) prove the boundary. If it goes, keep the
   liblsdj differential oracle as a native validation harness.

## Open questions

- **Does the sav codec earn a port at all?** It's the best *fit* and the worst
  *ROI*. It may be right to leave it native indefinitely as a "perf-adjacent
  binary codec" and only expose model JSON to TS — the same way kit-compile stays.
- **What replaces the liblsdj oracle if the codec moves to TS?** Keep the C++
  differential test as a fixture-driven oracle the TS codec is diffed against, or
  trust the byte-identical corpus round-trip alone? The former is safer.
- **ROM-version awareness for kit offsets.** `OffsetLookup` currently hardcodes the
  two bundled builds (banks 8..23); user-supplied LSDj ROMs need a version table
  ([OffsetLookup.hpp:9](../packages/native/src/lsdj/OffsetLookup.hpp#L9)). Where
  does that table live once patching is TS — native (next to the sniffer) or TS?
- **Kit editor UX.** The UI was never built; designing it against the new
  primitive (rather than porting a non-existent one) is greenfield.

## Links

- Sav codec: [SavCodec.cpp](../packages/native/src/lsdj/codec/SavCodec.cpp) ·
  [SongCodec.cpp](../packages/native/src/lsdj/codec/SongCodec.cpp) ·
  [Compression.cpp](../packages/native/src/lsdj/codec/Compression.cpp) ·
  [SavView.hpp](../packages/native/src/lsdj/codec/SavView.hpp)
- Model + rails: [model/](../packages/native/src/lsdj/model/) ·
  [FixedArray.hpp](../packages/native/src/lsdj/model/FixedArray.hpp) ·
  [SavSerialization.hpp](../packages/native/src/lsdj/SavSerialization.hpp) ·
  [tools/gen-sav-ts.js](../tools/gen-sav-ts.js)
- Kit compile: [KitCompiler.cpp](../packages/native/src/lsdj/KitCompiler.cpp) ·
  [KitUtil.hpp](../packages/native/src/lsdj/KitUtil.hpp) ·
  [SampleCache.hpp](../packages/native/src/lsdj/SampleCache.hpp) ·
  [OffsetLookup.hpp](../packages/native/src/lsdj/OffsetLookup.hpp)
- Kit patch (to be eliminated):
  [roles/LsdjKitPatchRole.cpp](../packages/native/src/system/sameboy/roles/LsdjKitPatchRole.cpp) ·
  [ProjectKitRecompile.hpp](../packages/native/src/lsdj/ProjectKitRecompile.hpp) ·
  `PatchKitCommand` ([CommandQueue.hpp:193](../packages/native/src/transport/CommandQueue.hpp#L193)) ·
  DSP drain ([PluginDSP.cpp:565](../packages/native/src/PluginDSP.cpp#L565))
- Oracle: [test/LsdjDifferentialTests.cpp](../packages/native/test/LsdjDifferentialTests.cpp) ·
  [test/CMakeLists.txt:239](../packages/native/test/CMakeLists.txt#L239)
- porting: [10-lsdj-kit-patching.md](../porting/10-lsdj-kit-patching.md) ·
  [21-sav-inspector.md](../porting/21-sav-inspector.md) ·
  [22-lsdj-legacy-sav-formats.md](../porting/22-lsdj-legacy-sav-formats.md)
- Siblings: [03-cpp-ts-boundary.md](03-cpp-ts-boundary.md) (the boundary + native
  contract) · [04-scriptable-runtime.md](04-scriptable-runtime.md) (control-plane
  runtime that owns load-time patching) · [05-roles-cross-core.md](05-roles-cross-core.md)
  (why the kit-patch role generalizing is moot once it's deleted) ·
  [06-midi-routing-scripts.md](06-midi-routing-scripts.md) ·
  [current-state.md](current-state.md)

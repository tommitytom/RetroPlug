# Step 10 — LSDJ kit patching

**Status:** Pending.

## Goal

Let users replace LSDJ's sample kits with their own. Upload audio files via
the UI, compile them into LSDJ's binary kit format, and patch them into the
running emulator's ROM. Per-kit dirty tracking keeps the UI in sync.

## Depends on

- [Step 09](./09-lsdj-arduinoboy.md) (full LSDJ role infrastructure).

## Architecture introduced

This is the most complex single step in the migration. The legacy
implementation spans
[old/src/lsdj/KitUtil.cpp](../old/src/lsdj/KitUtil.cpp),
[old/src/lsdj/SampleUtil.h](../old/src/lsdj/SampleUtil.h),
[old/src/core/SampleCache.cpp](../old/src/core/SampleCache.cpp), and several
async-task scheduling pieces. Port carefully.

- **`LsdjKitPatchRole`** at `src/system/sameboy/roles/LsdjKitPatchRole.{hpp,cpp}`.
  Separate role from `LsdjSyncRole` — sync and kits are orthogonal concerns.
  Both can attach to one LSDJ system.
- **`LsdjKitPatchConfig`** — alternative in `RoleConfig`. Holds:
  `std::vector<LsdjKitConfig> kits` where each `LsdjKitConfig` has slot index,
  name, and `std::vector<LsdjSampleConfig> samples`. Each sample has a path
  (or embedded bytes), name, pitch, volume, dither flag.
- **`SampleCache`** — UI-thread (or a worker thread it owns) cache that
  resamples audio files to GB-rate (~11468 Hz, 4-bit nibbles), compiles to
  LSDJ kit format. Hash-based dedupe so editing one sample doesn't recompile
  the whole kit. Port the resampling pipeline; r8brain (already vendored) does
  the heavy lifting.
- **Memory-patch path: rpcpp `patchKit(systemId, kitIndex, bytes)`**.
  Compiled kit bytes flow from UI to DSP via rpcpp; DSP injects them into
  the running emulator's ROM via `MemoryAccessor` write.
- **Dirty-kit tracking.** Each kit has a content hash; UI keeps the
  most-recently-applied hash per slot. UI compares against the current cache;
  any mismatch triggers a recompile + repatch.
- **UI: `<KitEditor/>` extension.** Built as a TS extension on the framework
  from step 12 — but a minimal version may need to land in step 10 as a
  built-in panel since extensions are step 12. Keep the implementation thin:
  list of kit slots, drag-drop audio files to load samples, tweak per-sample
  pitch/volume, hit "Patch" to send to DSP.

## Tasks

1. Port `KitUtil` (sample → 4-bit nibble compression, kit binary layout). The
   GB DAC quirks are subtle; preserve old comments.
2. Port `SampleCache` with a worker-thread executor (txiki.js exposes libuv;
   reuse, or std::thread).
3. Implement the `patchKit` rpcpp method: looks up the system, finds the
   `LsdjKitPatchRole`, patches the ROM bytes via `MemoryAccessor`. Resilient
   to mid-block patches: queue on a "wants-patch" flag, apply at top of next
   `run()`.
4. Build a minimal kit-editor React panel. Audio file IO via tjs.
5. Integrate the LSDJ ROM-version-aware kit memory layout from
   [old/src/lsdj/OffsetLookup.h](../old/src/lsdj/OffsetLookup.h).

## Verification

- Load LSDJ. Open kit editor. Drag a `.wav` onto kit slot 0, sample 0. Hit
  "Patch". Listen — the new sample plays in LSDJ's PCM channel.
- Reload project: kits restored. Sample bytes either in-project or
  reference-by-path (decide; recommend in-project to match step 04's "save
  the bytes" stance).
- Edit one sample: only that sample (or that kit) recompiles.

## Risks / open questions

- **In-project sample storage.** Recording-grade samples can be hundreds of
  KB each. With 16 kits × 16 samples × 4 KB = 1 MiB of resampled data;
  source audio is 10× that. Decide: store source (large) or store
  compiled (smaller, lossy round-trip)? Old project stored both.
- **Async compile UX.** Patching a 16-sample kit is ~50 ms of CPU on a
  worker. The UI should show progress. Build it lean from the start.
- **Patches mid-playback.** Editing a kit while LSDJ is playing causes a
  brief audio glitch in that channel. Acceptable — match old behavior.
- **Old-project import.** Legacy RetroPlug stored kits in its project format.
  Out of scope for now; add an import path later if there's demand.

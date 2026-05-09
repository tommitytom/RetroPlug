# Step 14 — Sample matcher UI

**Status:** Pending.

## Goal

Bring back the sample-matcher view from the legacy project: an LSDJ-style
sample editor that lets users tune sample pitch/volume against a reference,
preview against the live emulator, and audition samples in context. Built as
a TS extension on top of the framework + kit-patching path.

## Depends on

- [Step 12](./12-ts-extensions.md) (extension framework).
- [Step 10](./10-lsdj-kit-patching.md) (kit patch path).

## Architecture introduced

- **Extension `ui/extensions/lsdj-sample-matcher/`**. Activates per-system
  with `LsdjKitPatchRole`.
- **Reference comparison.** Two waveform displays side-by-side: the
  user-supplied source audio and the GB-rate compiled version. Both rendered
  to LVGL via the framework's image-from-pixels path.
- **Live preview.** When the user changes pitch/volume on a sample, the
  extension calls `plugin.patchKit(...)` (existing rpcpp method from step 10)
  and the running LSDJ instance plays the new sample. Throttle to ~5 Hz so
  scrolling a slider doesn't spam patches.
- **Pitch/volume math.** Port the legacy sample-matching helpers from
  [old/src/lsdj/SampleUtil.h](../old/src/lsdj/SampleUtil.h) and
  [old/src/lsdj/KitUtil.cpp](../old/src/lsdj/KitUtil.cpp). They compute the
  GB DAC-correct rendering for given sample data.
- **Audition controls.** A single-shot "play this kit slot" button that sends
  a `Command::TriggerKitPlayback(systemId, kit, slot)` to the DSP. The DSP
  drives LSDJ to play that specific sample (write to LSDJ's "play kit slot"
  RAM byte; offset table from step 08 covers this).

## Tasks

1. Port the audio resampling + GB-format renderer (mostly TS-side this time;
   the C++ version is in the cache from step 10, but the matcher wants a
   lower-quality realtime preview path on the UI thread).
2. Build the React UI: kit-slot list, per-slot waveform editor, pitch/volume
   sliders, audition button.
3. Wire to the existing `patchKit` rpcpp method.
4. Implement the "trigger kit slot" command path.
5. Style to roughly match LSDJ's UI conventions for familiarity.

## Verification

- Open the matcher on a loaded LSDJ instance.
- Drag-drop a `.wav`. The waveform displays. The compiled (GB-rate) version
  shows alongside it.
- Adjust pitch slider; the compiled waveform updates live and the running
  emulator plays the latest version.
- Save project, reload — matcher state restored (which kit slot is selected,
  current pitch/volume per sample).

## Risks / open questions

- **CPU cost of live preview.** Every slider drag re-resamples on the UI
  thread. Throttle aggressively; prefer "while-dragging shows source-rate
  approximation, on-release commits the GB-rate render".
- **Sample-matcher state persistence.** Should pitch/volume tweaks live in
  the project, in the kit slot, or in matcher-extension storage? Match the
  legacy project — it stored them on the kit, so they round-trip on save.
- **Reference audio storage.** A user matching a kick drum may want to keep
  the source `.wav` referenced from the project. Same in-vs-out-of-project
  question as step 04. Default: in-project.

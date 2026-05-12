# Step 04 — Project state (DPF setState/getState)

**Status:** Done.

## Goal

Implement DPF's state hooks so the project survives a DAW save/load cycle. Wire
`Plugin::getState`/`setState` to (de)serialize `ProjectConfig` via reflectcpp.
Promoted earlier than the original roadmap because users will lose work without
this, even on a single-instance plugin.

## Depends on

- [Step 03](./03-rom-picker.md) (so there's something worth saving).

## Architecture introduced

- **DPF state slot.** `Plugin` constructor declares a single state with key
  `"project"` and default empty value. DPF persists it as a string per host
  project save.
- **Serialization.** reflectcpp's JSON codec turns `ProjectConfig` into a
  string. ROM bytes are encoded as base64 inside the JSON. (Compression — gzip
  via miniz, already in deps — applied if the JSON exceeds, say, 256 KiB. With
  a 1 MiB LSDJ ROM, base64'd: ~1.4 MiB. Compression brings it back under 1 MiB
  and is worth the cycles since save/load is cold path.)
- **Lifecycle.**
  - `setState("project", string)`: called *before* `activate()` when a host
    loads a project. Deserialize → `ProjectConfig` → rebuild `Project::systems`
    via `addSystem(config)`. ROM bytes are decoded from base64 and passed to
    each `SameBoySystem`.
  - `getState("project")`: called by the host on save. Walk runtime systems,
    call `Project::snapshotConfig()` to gather current state (savestates,
    dirty kit RAM, role configs). Serialize.
- **Schema versioning.** A top-level `"schemaVersion": "1.0"` field on
  `ProjectConfig`. Bump on breaking changes. Treat unknown variant alternatives
  ("kind") as forward-compatible no-ops.

## Tasks

1. Declare the state in `LVGLPluginDSP` constructor:
   `Plugin(parameters, programs, states=1)` and override `initState` /
   `getStateHint`.
2. Add a reflectcpp serialization helper next to `ProjectConfig` — header-only
   wrapper so both DSP-side serialize and rpcpp's `getProjectConfig` reuse it.
3. Implement `getState`: snapshot config, serialize, return.
4. Implement `setState`: deserialize, rebuild `project_`. Handle deserialize
   failure (corrupt save) by logging and falling back to empty project.
5. Decide on ROM-bytes-in-state: base64 with optional gzip when over a
   threshold. Document the format.
6. Wire `ConfigChanged` events so the UI cache refreshes after `setState`.

## Verification

- Save a project in carla with a loaded LSDJ ROM, master gain at -10 dB.
- Close, reopen carla, reload the same project.
- Plugin instantiates, ROM is the same, master gain is at -10 dB, audio plays.
- Inspect the saved project file: should see a `"project"` state field
  containing the serialized `ProjectConfig`.

## Risks / open questions

- **State size.** DPF state strings have host-imposed limits in some formats.
  LV2 sets are typically fine; VST2 has a 64 KiB string limit on some hosts —
  but DPF works around this. Verify against carla, ardour, reaper.
- **ROM bytes vs ROM path.** Saving the path is much smaller but breaks if the
  ROM moves. Saving the bytes is robust but bloats the project file. **Default
  to bytes** with an opt-out via `SameBoyConfig::romPath` only mode. Match the
  old project's behavior here — check what it did.
- **Schema evolution.** Plain `std::variant` tagged-union round-tripping
  breaks if a "kind" string is renamed. Lock the spelling now (`"sameboy"`,
  `"mesen"`, `"lsdj-sync"`, etc.).
- **Migration of legacy projects.** No hard requirement — old RetroPlug
  projects use a different format. Document that old projects don't import.

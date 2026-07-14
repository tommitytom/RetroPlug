# 05 — Data & Persistence

How RetroPlug2 turns a live session into bytes on disk and back:
the project model and the `.rplg` file, the plugin's DAW state chunk, the three
per-user config files, the persistence policy (version stamps + raw-JSON
migrations), the LSDj `.sav` codec, and the SRAM auto-save policy.

This doc is the concrete expression of the thesis (see
[01-architecture.md](01-architecture.md)): **TypeScript owns the framing —
the model, the JSON shape, path resolution, version policy — and native owns
only the bytes: compression (zip/unzip) and file I/O.** Native never decides what
a project *is*; it slurps resolved paths and compresses byte buffers TS hands it.
(The LSDj `.sav` codec was the one exception; it is now pure TS too — see below.)

## The project model

TS is the single source of truth for the project. It **builds** the config from
the live systems + settings, **serializes** it (rebasing asset paths portable),
and **parses** it back. The model lives in
[projectConfig.ts](../packages/retroplug/src/projectConfig.ts).

| Type | Fields | Notes |
|---|---|---|
| `ProjectConfig` | `schemaVersion: string`, `settings`, `systems[]` | The root; `schemaVersion` is a legacy *string* (`"1"`) |
| `ProjectSettings` | `layout`, `midiRouting`, `audioRouting` (string enums; see [settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)), `zoom 0-6` | Three string enums + a `zoom` magnitude (`0` = inherit the user default). Native's numeric enums are recovered at the RPC/kernel boundary |
| `SystemThin` | `platform`, `romPath?`, `savPath?`, `savSuffix?`, `embeddedRom?`, `settings?`, `roles?` | One serialized system |

**"Thin"** means the model carries no binary blobs (ROM/SRAM/savestate/kit
bytes are never in the config) and omits any field sitting at its default —
`buildConfig` drops zero-gain, non-overridden savPath, and default settings so a
fresh/uncustomized project stays terse ([projectConfig.ts:135](../packages/retroplug/src/projectConfig.ts#L135)).
`core` is deliberately **not stored**: it is re-derived from `platform` on load
(nes/gba → mesen, else sameboy), mirroring how the runtime kind was always
re-sniffed rather than trusted from disk. Omitted rich fields restore to native
defaults via forward-tolerant reads (below), so a thin file round-trips
faithfully.

## The project file: THIN `.rplg` vs EXPORT `.rplg.zip`

A project comes in two on-disk shapes over **one** config model — a **thin**
`.rplg` (raw JSON) and an **exported** `.rplg.zip` (PKZIP). **Zip-based projects
always use the `.rplg.zip` extension; thin projects use `.rplg`.** Both are driven
by [projectStore.ts](../packages/retroplug/src/projectStore.ts); the
blob key contract is the pure module
[projectBinaries.ts](../packages/retroplug/src/projectBinaries.ts).

| Shape | Extension | On disk | Written by | Contents |
|---|---|---|---|---|
| **THIN** | `.rplg` | raw JSON, paths only | `save(path)` | `project.json` bytes, asset paths rebased **relative** to the file's folder (portable) |
| **EXPORT** | `.rplg.zip` | PKZIP archive | `export(path)` | the same `project.json` + one entry per live system's SRAM/savestate blob |

Blob entries are keyed by config **index**, not id: `systems/{i}/sram` and
`systems/{i}/state` ([projectBinaries.ts:17](../packages/retroplug/src/projectBinaries.ts#L17)).
The store's `systems()` order matches `buildConfig`'s, so index `i` addresses
both alike. On export, TS gathers each blob through the snapshot read door
(`backend.readState` / `backend.readSram` — see [01-architecture.md](01-architecture.md)),
**frames every entry itself**, and hands the entry list to native's `zip`; native
only deflates (miniz). Import is the inverse: `unzip` → `partitionEntries` splits
`project.json` from a key→bytes blob map, and each blob seeds its system's
emulator at reconstruct time.

**Path portability.** Serialize rebases each `romPath`/`savPath` to a
forward-slash relative path when it sits at/under the file's folder, keeping it
absolute otherwise (no fragile `../` chains); load rebases back to absolute
([projectPaths.ts](../packages/retroplug/src/projectPaths.ts)). Native
supplies only `canonicalize` (`weakly_canonical`) for the realpath-hard
to-relative test.

**Load routing + lifecycle.** The Load / Locate file dialog offers both `*.rplg`
and `*.rplg.zip`; routing is by **extension**, not content — a `.rplg` is always
parsed as thin raw JSON (`loadThin`), a `.rplg.zip` as a PKZIP (`loadZip`)
([projectStore.ts](../packages/retroplug/src/projectStore.ts)). A `.rplg` is
**never** loaded as a zip: if its bytes aren't pure JSON (e.g. a zip masquerading
as `.rplg`, the reverted design) the load returns `{kind:"error"}` rather than
silently coercing to an empty project.
All paths converge on `beginLoad`, which: refuses a newer schema stamp (→
`{kind:"incompatible"}`), absolutizes paths, then runs a blob-aware
missing-files scan. A project with missing assets is held as `pendingLoad` for
`relink(item, newPath)` (which auto-finds folder-mates) before `commit` rebuilds
the systems. The outcome is one of:

```
LoadOutcome = loaded | incompatible | missing | error
```

## Plugin state: DPF getState/setState + autoload

The plugin has no editor-side persistence of its own — get/setState and the
headless autoload hook all bounce to JS project globals, so base64 and `.rplg`
framing happen entirely in TypeScript.

| DPF seam | JS global | Behaviour |
|---|---|---|
| `getState("project")` | `__rp_saveProjectB64()` | export the project as a base64 `.rplg` chunk |
| `setState("project", v)` | `__rp_loadProjectB64(v)` | load an in-memory chunk; **empty string = no-op** (don't wipe a seeded project) |
| `RETROPLUG_AUTOLOAD_PROJECT=path` | `__rp_loadProjectPath(path)` | headless seed (reaper `-renderproject`): load a project FILE from disk via `load()` — a thin `.rplg` or an export `.rplg.zip` |

State is a single `"project"` key, host-readable + host-writable
([PluginDSP.cpp:103](../packages/native/plugin/PluginDSP.cpp#L103)).
The C++ boundary stays **string-only**: base64 is done in JS
([pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts))
because a DPF state chunk is NUL-terminated UTF-8 while the export archive it
carries is binary PKZIP. `exportBytes()` produces the chunk with paths left **absolute**
(`baseDir=""`) and none of the recents/currentPath/dirty side-effects a host
`save`/`export` has, so `loadBytes` round-trips it with no rebase
([projectStore.ts:162](../packages/retroplug/src/projectStore.ts#L162)).

**State-source decisions.** In a **DAW**, the host chunk is authoritative: the
DAW persists `getState()` in its project and replays it via `setState()` on
reload, so the chunk wins over anything on disk. In the **standalone** (the JACK
build) there is no host chunk, so the plugin starts **empty** unless
`RETROPLUG_AUTOLOAD_PROJECT` seeds it. A standalone that reopens the last-saved
project from disk on launch is *not yet built* (see below).

## Config models on disk

Three per-user, machine-global files live under `configDir()` (per-OS:
`XDG_CONFIG_HOME`/`~/.config/retroplug`, `%APPDATA%\RetroPlug`, or
`~/Library/Application Support/RetroPlug`; overridable via
`RETROPLUG_USER_CONFIG_DIR` —
[HostRpcService.cpp:23](../packages/native/src/HostRpcService.cpp#L23)).
Each on-disk shape matches the legacy native file so a user's existing configs
still load.

| File | Model | Stamp const | Shape |
|---|---|---|---|
| `config.json` | `UserConfig` ([userConfig.ts](../packages/retroplug/src/userConfig.ts)) | `USER_CONFIG_SCHEMA = 1` | `{ schemaVersion, activeKeyboardBindings, activeGamepadBindings, defaultZoom 1-6, sramAutoSave }` |
| `bindings/<name>.json` | `BindingMap` ([bindingMap.ts](../packages/retroplug/src/bindingMap.ts)) | `BINDINGS_SCHEMA = 1` | `{ schemaVersion, name, keyboard, gamepad, keyboardActions, gamepadActions }` (one profile per file; the `*Actions` sections — Open Menu / Cycle Instances — seed to defaults when missing) |
| `recent.json` | `RecentEntry[]` ([recentList.ts](../packages/retroplug/src/recentList.ts)) | `RECENT_SCHEMA = 2` | `{ schemaVersion, entries: [{ path, name }] }`, most-recent-first, capped at 10 |

One deliberate rename: the TS layer's `sramAutoSave` field is native's
`sramMirror` key ("mirror" reads from the plugin's side; "auto save" fits both
plugin and standalone). The string enum values (`Off` / `OnProjectSave` /
`Continuous`) still match native's spellings. The three config **stores** and the
`__rp_*` UI seam are documented in [03-ts-layer.md](03-ts-layer.md); this doc
covers only their on-disk shapes.

## Persistence policy (the precise statement)

Persistence is TS-owned (native does no version checking — the TS constants are the
single source of truth). Config forward-compat is **versioned migrations on the raw
JSON**, backed by two mechanisms:

1. **Version stamp + refuse-newer / migrate-older.** Every serialized JSON root carries
   a `schemaVersion`, checked on load against a TS constant:

| Root | TS const |
|---|---|
| `.rplg` / DAW chunk | `K_PROJECT = 3` ([projectConfig.ts](../packages/retroplug/src/projectConfig.ts)) |
| `config.json` | `USER_CONFIG_SCHEMA = 1` |
| `bindings/*.json` | `BINDINGS_SCHEMA = 1` |
| `recent.json` | `RECENT_SCHEMA = 2` |

   A file stamped **newer** than the build is **refused** (a format from the future
   can't be safely read); one stamped **older** is **migrated** up (below). Refusal
   differs by root: the project load returns `{kind:"incompatible"}` (the
   project-incompatible modal); config/bindings/recent return `null`/`[]` and the store
   **keeps its previous in-memory value** (also on malformed/non-object files).

2. **Migrations (raw-JSON, latest-schema-only).** Keep only the LATEST zod schema per
   root — never a per-version copy. On a breaking (non-additive) change, bump the root's
   version constant and add ONE raw `(obj) => obj` step to that root's migrations map,
   keyed by from-version, in [migrate.ts](../packages/retroplug/src/migrate.ts). On load
   `migrateRaw` applies the ordered chain from the file's stamp up to current, on the RAW
   object, **before** the (latest) zod schema validates. Steps must be idempotent-safe (an
   unstamped file floors to current). The project **v1→v2** (`PROJECT_MIGRATIONS[1]`)
   backfills each system's `core` from `platform`; **v2→v3** (`PROJECT_MIGRATIONS[2]`)
   rewrites the integer enum settings — the project `layout`/`midiRouting`/`audioRouting`
   and the per-system role-config enums (`model`/`highpass`/`region`/`channelExportMode`/
   lsdj `mode`) — to their string values ([settingsEnums.ts](../packages/retroplug/src/settingsEnums.ts)).

Additive-only changes still need no step: the strict zod schemas fill a missing field from
its `.default()` and **clamp** out-of-range scalars (an unknown string enum falls to its default)
([configSchema.ts](../packages/retroplug/src/configSchema.ts)), so an old file that only
lacks a newly-added optional field validates as-is. The project `schemaVersion` is a
**string** (old `.rplg` carried `"1.0"`); `parseProjectVersion` takes its leading integer,
flooring empty/garbage to the baseline. The other three stamps are numbers (`readNumericVersion`).

Two things stay outside the JSON-migration model: the per-system **role-config** that
crosses to native is re-parsed there with reflect-cpp `rfl::DefaultIfMissing` (tolerant —
the one outlier), and the LSDj `.sav` is a binary codec with its own internal format
version (below).

## The LSDj `.sav` codec

RetroPlug has a hand-written, version-aware codec for the 128 KiB LSDj `.sav`
image (working-memory song at offset 0 + a 512-byte header at `0x8000` + the
RLE-compressed stored-project archive). It is **pure TypeScript**
([packages/retroplug/src/lsdj/](../packages/retroplug/src/lsdj/): `model.ts` is the
zod SSOT; `codec/{bits,regions,rle,song,sav}.ts` is the byte codec), supporting
every LSDj format version (fmt 0..22). The model — a zod schema mirroring the
reflect-cpp JSON contract exactly — is the single source of truth for the on-disk
shape, JSON, and TS types; the codec reads/writes the bytes. It runs in every
runtime with no native host (the pure-TS test tier, the plugin, a future web
build), and the model + decode are usable directly in TS.

**TS authoring.** `savFromJson(json)` (a local function in `src/lsdj`, re-exported
by [lsdjSav.ts](../packages/retroplug/src/lsdjSav.ts); also on the `Backend` seam
for the load-time seed) encodes a `.sav` from a JSON `Sav` model. It decodes
**leniently** — every field has a default so a fixture specifies only the cells it
sets and everything else takes its model default. `savFromJson("{}")` yields a
valid 128 KiB image, letting a test boot LSDj straight into authored song/sync
state and skip the 12–15 s cartridge self-test. `savToJson(bytes)` decodes the
inverse — the read direction TS never had before the port.

**Correctness rationale (frozen goldens + corpus byte-identity).**
Byte-identical round-tripping only proves *losslessness*, not that the old-format
decode branches interpret the bytes correctly. The decode semantics are pinned by
**frozen golden JSON** ([test/lsdj/golden/](../packages/retroplug/test/lsdj/golden/)):
one per liblsdj content sav spanning fmt 3..11 + 16 — which cover EVERY version-decode
branch (predicates sit at fmt 4/5/6/7/8/9/10/11/16; fmt12..15/17..22 share fmt11/16's
paths). Those goldens were certified against **liblsdj** (the known-correct reference
for song format versions ≤ 16) by a differential oracle, then frozen. The pure-TS codec
is asserted byte-for-byte against them ([test/lsdj/corpus.test.ts](../packages/retroplug/test/lsdj/corpus.test.ts))
and byte-identity round-trips all ~549 per-version corpus savs
([test-native/lsdj-codec-corpus.test.ts](../packages/retroplug/test-native/lsdj-codec-corpus.test.ts)).
The C++ codec + liblsdj were retired once the goldens were frozen — the shipping build
and the test suite both link no C++ sav codec.

## SRAM auto-save policy

Battery RAM is mirrored to the loose sibling `<rom>.sav` the way most Game Boy
emulators do, gated on the user's `sramAutoSave` preference. Native only reads
SRAM (through the snapshot read door) and writes a file; the whole policy is TS
([sramAutoSave.ts](../packages/retroplug/src/sramAutoSave.ts)).

| Mode | Behaviour |
|---|---|
| `Off` | never write the loose `.sav` |
| `OnProjectSave` (default) | flush every system at a save/quit moment (`flushOnSave`) |
| `Continuous` | also write changed SRAM on a throttled idle tick (`pump`) |

`SramAutoSaver` reads live SRAM via `backend.readSram(id)`, resolves the target
with `resolveSavPath` (embedded-ROM systems have no sibling and are skipped), and
**seeds rather than rewrites** an identical file just loaded — the first
observation compares an FNV-1a hash of live SRAM against the on-disk bytes and
writes only on a genuine change. The hash is in-process dedup only, never
persisted. The idle cadence and change detection are driven from the control
plane; the file-watch side ("watcher = C++, policy = TS") is covered in
[03-ts-layer.md](03-ts-layer.md).

## Not yet built / deferred

- **Standalone disk-wins reopen.** The standalone starts empty (or from
  `RETROPLUG_AUTOLOAD_PROJECT`); reopening the last-saved project from disk on
  launch is not implemented. See [07-migration.md](07-migration.md).
- **Kit-patch persistence.** The thin config persists platform, paths, sav
  suffix/override, embedded-ROM marker, non-default universal settings, and
  roles; kit (sample-patch) state is not yet serialized. Rich per-system domains
  are tracked in [07-migration.md](07-migration.md).

## Key files

- [projectConfig.ts](../packages/retroplug/src/projectConfig.ts) — the config model, build/serialize/parse, schema-version helpers.
- [projectStore.ts](../packages/retroplug/src/projectStore.ts) — save/export/load, the missing-scan + relink lifecycle, `exportBytes`/`loadBytes`.
- [projectBinaries.ts](../packages/retroplug/src/projectBinaries.ts) / [projectPaths.ts](../packages/retroplug/src/projectPaths.ts) — blob-key contract; path rebasing.
- [pluginControlPlane.ts](../packages/retroplug/src/pluginControlPlane.ts) — the `__rp_saveProjectB64`/`__rp_loadProjectB64`/`__rp_loadProjectPath` surface + base64.
- [PluginDSP.cpp:103](../packages/native/plugin/PluginDSP.cpp#L103) — DPF `initState`/`getState`/`setState` + `RETROPLUG_AUTOLOAD_PROJECT`.
- [userConfig.ts](../packages/retroplug/src/userConfig.ts) / [userConfigSerialization.ts](../packages/retroplug/src/userConfigSerialization.ts), [recentSerialization.ts](../packages/retroplug/src/recentSerialization.ts), [bindingSerialization.ts](../packages/retroplug/src/bindingSerialization.ts) — the config models + on-disk codecs.
- [configSchema.ts](../packages/retroplug/src/configSchema.ts) — the clamp/default zod builders (TS forward-tolerance).
- [migrate.ts](../packages/retroplug/src/migrate.ts) + [projectConfig.ts](../packages/retroplug/src/projectConfig.ts) — the raw-JSON migration framework + the version constants / `checkVersion` / `parseProjectVersion` (TS is the single source of truth; native does no version check).
- [HostRpcService.cpp](../packages/native/src/host/rpc/HostRpcService.cpp) — native file I/O, `zip`/`unzip`, `configDir`.
- [src/lsdj/](../packages/retroplug/src/lsdj/) — the pure-TS sav model (`model.ts`) + codec (`codec/`); [test/lsdj/](../packages/retroplug/test/lsdj/) holds the frozen goldens + branch-covering fixtures.
- [sramAutoSave.ts](../packages/retroplug/src/sramAutoSave.ts) — the loose-`.sav` auto-save policy (+ the LSDj semantic dirty signature).

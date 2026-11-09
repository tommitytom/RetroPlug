# Phase F4a: the non-UI registrations

**Date:** 2026-11-09 · **Branch:** `feature/modularize` · **Status:** designed (sections 1 to 4 approved by the user 2026-11-08 and 09, the four questions the draft raised answered 2026-11-09; not yet prototyped)

F4a is the first half of the fourth phase of the package split ([package-split.md](package-split.md)), after F1
([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md)), F3
([modular-graph-phase-f3-design.md](modular-graph-phase-f3-design.md)) and F2
([modular-graph-phase-f2-design.md](modular-graph-phase-f2-design.md)). F1 gave every cart a TypeScript home and a contribution
list, F3 gave the Launchpad a pack, and F2 took the consoles' native halves out of the shell. What is left in
`packages/retroplug/src` that knows a cart or a console by name is the part this phase removes: the `Platform` and `Core`
unions and the tables keyed by them, the stores' deep imports of LSDj, risa, smsggdj and BlipToaster, `render.ts`'s
platform switches, the mGB special case, and four "doors" that re-export carts so a leaf can import one path. The survey
behind it is `.superpowers/f-split/f4-survey.md` (taken at `b88e81756`; section 1 lists every line it gave that the tree
corrected), and the user's decisions are the `## F4` section of `.superpowers/f-split/decisions.md`.

Everything the user approved is binding here. Four things the brainstorm's sections and the code disagreed on were put to
the user after the draft (decisions 10 to 13); section 9 records them with the evidence, beside the risks that remain. The phase
is a registration refactor, not a feature: no audio changes, no saved-file format changes, no graph document changes, and
no baseline is re-recorded (section 7).

## 1. Goal and scope

**Goal.** The package split's goal for the control plane: a cart's or a console's TypeScript is reached through the
registrations its package contributes, and the shell holds no code that names one. After F4a `packages/retroplug/src`
imports an explicit list of contract specifiers (section 7) and nothing else from a package, and a second console or a
second tracker is a package that adds a descriptor and a few hooks, with no edit to the shell. Leaner products are not the
driver.

**Scope (one phase, the whole non-UI list).** A `consoles` contribution (section 3); the cart hooks the stores reach
through today (section 4: tracker `render`, `playback`, `sramSignature` and `songImport`; role `parameters`,
`deriveConfig` and `hostLatencyMs`; link groups; `savFromJson` out of the construct caps; the four doors gone); render off
its platform switches and the mGB embed declared in TypeScript (section 5); and the shell-source boundary rule (section 7).
It runs as one prototype in five stages (section 8).

**Not in F4a**, and where it goes:

- **F4b (the UI and the cart tools).** The menu contributions replacing `TRACKER_UI`, the console settings rows
  (`sameboyConfig`, `mesenConfig`, the NES and SMS/GG blocks), the N8 and Launchpad menus, TSX in packages, the LSDj HD
  player and the two overlays as views, and the cart CLI tools (`lsdj-rom`, `risa-rom`, `bliptoaster-rom`, `n8-load`,
  `n8-play`, `n8-bridge`, `n8-sync`, `launchpad-probe`) as a second surface on the same host contract. `ui/` and `cli/`
  join the boundary rule there. F4a edits `ui/` and `cli/` only where a signature F4a changes forces a call site
  (`loadMgb`, `compileKit`, `compileDmc`, the door imports): a rename, not a move.
- **Per-product bundles (owed).** Every product lists the same 16 packages, so the plumbing would build identical bundles.
  The first product whose list differs brings them, proved by the difference. F4a keeps the precondition true: no bundle
  reaches a cart except through the generated registries (`src/generated/packages.ts`, `protocol.ts`, `scriptNodes.ts`),
  which the boundary rule makes a check (section 7).
- **Native.** F2 closed the native side; F4a changes no C++ and no CMake except the manifest reader's embed check, which is
  TypeScript (`scripts/lib/packages.mjs`).

**What the tree has** (checked against `b88e81756`; the survey's numbers are corrected here, and every file:line below is
verified unless it says otherwise).

- **The unions.** `packages/core/ts/platform.ts` declares `Platform = "gb" | "nes" | "gba" | "sms" | "gg"` and `Core =
  "sameboy" | "mesen"` (two lines). `packages/retroplug/src/platform.ts` (165 lines) holds `DEFAULT_CORE` (29), `defaultCoreFor`
  (38), `SystemNodeType` and `NODE_TYPE` (`gg` maps to `rp.mesen.sms`, 44 to 55), `ROM_SNIFF_LEN` 0x134 (62),
  `SEGA_SNIFF_LEN` 0x8200 (68), `detectPlatform` (133) and `romHasBattery` (161). `projectConfig.ts` validates with
  `z.enum` at lines 123 and 124 and its v1 to v2 step calls `defaultCoreFor` (167). `render/types.ts:12` has its own
  `Platform` with `"other"`.
- **The consumers of the tables.** `systemsStore.ts`: `classifyRom` (128 to 134), `systemNodeTypeFor` (269, 997, 1233),
  `defaultCoreFor` (1168, 1309), the embedded rule `platform = "gb"` (1161, 1303), `loadMgb` (371 to 373), the `"sameboy"`
  literal in `inheritLinkGroup` (1055 to 1061), `gg` to `gameGear` (1237), `readRoleHeader` (83 to 90),
  `withDerivedRoleFacts` (1506 to 1520, the `"bliptoaster"` literal at 1507) and `detectBattery` (1527 to 1532).
  `graph/buildGraph.ts`: `SETTING_FIELDS` (176), `CONFIG_PARAMS` (197), `CONSTRUCT_FIELDS` (222), `streamsOf` (357), `linkGroups`
  (379), the NES tap (`tapArmOf` 399), the serial-out checks (485, 502), `drivenCartSystem` (149 to 153, platform `"nes"`),
  the cart node (`rp.n8.nes`, 524) and `gameGear` (349). `graph/systemResources.ts:161` and `graph/directGraph.ts:225`.
- **Where the shell reaches carts.** 47 import lines in 15 files outside `generated/` (65 lines in 17 with it): `audioDriver`
  4, `controller/registry` 1, `graph/buildGraph` 2, `graph/controller` 4, `kit/compile` 2, `lsdjSongImport` 3,
  `pluginControlPlane` 1, `projectConfig` 4, `projectStore` 3, `realBackend` 1, `render/render` 9, `sramAutoSave` 1,
  `systemsStore` 1, `tracker/index` 7, `trackerRegistry` 4. `cli/` has 13 lines in 8 files and `ui/` 31 lines. The survey
  said "about 60 lines in 21 files" for `src` and 14 for `cli`.
- **The deep reads.** `projectStore.ts` imports `workingSongSignature`, `savSyncMode`, `songRowTicksFromSav`,
  `anchorRowsFromState`, `anyChannelPlaying`, `ControllerAnchor` and `LsdjReader` from `@retroplug/lsdj` (24 to 28); its
  controller poll is the fields at 126 to 131, `controllerModel` (192 to 212), `refreshControllerSong` (476 to 492),
  `sampleControllerAnchor` (502 to 514) and `controllerReader` (518 to 529, gated on the `"lsdj-sync"` literal at 521).
  `graph/controller.ts` imports `LsdjSyncMode`, `RowTicksTable` and `ControllerAnchor` (8 to 10) and compares
  `LsdjSyncMode.MidiOut` / `MasterSync` in `serialOutCaptureFor` (125 to 138). `pluginControlPlane.ts:99 to 104` holds the
  33 ms. `parameterMap.ts` holds `MGB_PARAMETERS` (316) and `BLIPTOASTER_SPECS` (256), chosen by role kind in
  `parametersForRole` (319 to 328). `sramAutoSave.ts:39` holds `lsdjSramSignature` (the `jk` magic at 0x813e). `realBackend.ts:94`
  binds `savFromJson` to the LSDj codec.
- **Render.** `render/render.ts` (959 lines) imports the LSDj codec, risa's list and song ops, smsggdj's list, version and
  layout, risa's runtime and mesen's `ChannelExportMode` (19 to 41), and switches on the platform at `platformOf` (66 to 75, by
  file extension), `resolveSongSeed` (140 to 151), `currentSongName` (430 to 440), `rolesForRender` (392 to 397),
  `renderOutputPaths` (499 to 503), `buildSink` (586 to 640), `buildPlayingProbe` (761 to 788), `validateRenderOpts` (793 to 805),
  `pressPlay` (812 to 838) and `runRenderJob` (843 on). `cli/sessions/render.ts:23 to 51` switches again for `--list-songs`.
- **The doors.** `src/kit/compile.ts` + `src/kit/index.ts` (the compilers, the two format constants and the dsp types),
  `src/tracker/index.ts` (tracker types, seven cart catalogs and the `liveSav` helpers), `src/trackerRegistry.ts` (the
  resolvers and four integration re-exports) and `audioDriver.ts` (a facade that re-exports kit types, 53 to 61, and
  hard-codes the format ids `"lsdj"` and `"risa"` at 295 and 296). Importers: `kit` 2 in `src` and 2 in `test` through
  `compile`, and `src/kit` by 3 cli sessions and 4 tests; `tracker` 1 in `src`, 3 in `ui`, 10 in `test`, 3 in `test-native`;
  `trackerRegistry` 5 in `src`, 3 in `ui`, 4 in `test`, 6 in `test-native` (only `render.ts` imports an integration from it
  in `src`, but seven tests import `smsggdjIntegration` from it, which the survey missed); `audioDriver` 2 in `src`, 4 in `cli`,
  2 in `test` and 103 in `test-native` (the survey said "about 45"; nearly all of them want the driver, not the re-exports).
- **Other extension lists** the survey did not find: `savPaths.ts:77` (`ROM_EXTS`, used for the sibling ROM probe) and
  `fileSelection.ts:20, 21` (`ROM_PATTERNS`, `ROM_OR_SAV_PATTERNS`). Both repeat the six ROM extensions the console
  descriptors will own.
- **`loadMgb` callers.** 33 files in `packages/retroplug`: `systemsStore.ts`, `ui/screens/menu/menuDefs.ts:2560`, `cli/sdk-types.d.ts:391`, three cli
  sessions (`mgb-smoke`, `analyze-mgb`, `export-mgb-channels`), 27 tests and test helpers, and two files outside
  `packages/retroplug` that the survey missed: `packages/native/node/test/emu-ops.mjs:42` and `packages/native/node/README.md:86`.
  `compileKit` is in 9 files (`audioDriver.ts`, the `lsdj-rom` and `risa-rom` cli sessions, and six tests) and
  `compileDmc` in 7 (`audioDriver.ts`, `risa-rom`, `bliptoaster-rom` and four tests).

## 2. The decisions

| # | Decision | Chosen | Alternatives not taken |
| --- | --- | --- | --- |
| 1 | The cut | F4 split by layer: F4a the non-UI registrations, F4b the UI and the cart CLI tools, F4a first (user, option B) | One phase for everything (A); split by cart slice, LSDj end to end first (C) |
| 2 | Per-product bundles | Dropped from F4, recorded as owed; the first product whose package list differs brings them (user, option A) | Build the plumbing now with every product on `desktop`, proved by a fixture product (B); build it and make the plugin drop `launchpad` / `lsdj-launchpad` (C) |
| 3 | `Platform` and `Core` | Open documented `string` aliases in core; the registered console descriptors are the authority; a saved project's platform is checked against the registered set at load and refused, naming the known ones (user, option A) | Generated union types from static manifest data (B); keep the unions closed in core, like `MemoryType` (C) |
| 4 | Render | The tracker integrations (lsdj, risa, smsggdj) gain `render`; the render classifies its ROM through the ROM providers and `resolveTracker`, as the store does, and stops sniffing (user, option A) | A separate `renderers` contribution matching ROM and sav bytes (B); render hooks on the role type (C) |
| 5 | The controller poll | A `playback` member on the tracker integration; lsdj contributes it; tracker owns the neutral types; the poll timer stays in the store (user, option A) | Move the poll into the controller app's runtime half (B); the facts on the integration and the poll loop in the app (C) |
| 6 | The cart CLI tools | Move into their packages in F4b as a second surface on the menus' host contract; the shell's tool table becomes generated (user, option A) | Move them in F4a (B); leave them in the shell and record them owed (C) |
| 7 | Cart-named API | `loadEmbedded(id)` and `compileKitFormat(id, spec)` replace `loadMgb` and `AudioDriver.compileKit` / `compileDmc`; every caller in the repo updated; the SDK `.d.ts` loses `loadMgb`; mgb's TS declares its embed `{ id, label, platform }`, cross-checked by the generator against the manifest `embed` (user, option A) | Keep the cart names as one-line aliases (B); packages contribute named SDK methods (C) |
| 8 | The design's four sections | Approved by the user ("yes" four times), 2026-11-08 and 09: the `consoles` contribution, the cart hooks, render and embed with every oracle identical, the shell-source boundary rule and five prototype stages | n/a |
| 9 | Prototype model | DeepSeek Flash implements each stage, Opus reviews each stage (the repo's current routing) | n/a |
| 10 | Detection order | Each detector carries a `rank` (NES 0, GBA 1, GB 2, Sega 3); same-length detectors run in rank order, matching today (user, option A) | Reorder the product list so `mesen` precedes `sameboy` (B); retire the polyglot test and accept product order (C) |
| 11 | BlipToaster's deep read | Providers see the short header (Sega: the console's `providerHeaderBytes`); the role's `deriveConfig` runs on both paths, right after the providers on a fresh build and on a loaded project as now, and asks for the 0x4010 read through `headerBytes(short)` (user, option A) | Providers declare a header length, two writers of `chip` (B); repair the chip only at the next load, a visible regression (C) |
| 12 | Drive Real Cart | The console names its opt-out field (`realCartOptOut: "driveRealCart"` on mesen's NES); n8's `RealCart` names only the cart node (user, option A) | n8 spells mesen's field (B); move the field to an n8 role, a saved-format change (C) |
| 13 | The v2 to v3 migration's enums | The five index-ordered v2 lists frozen as literals beside the step; the sameboy, mesen and lsdj enum imports leave `projectConfig.ts`; a test asserts every frozen value is still valid in the package's current schema (user, option A) | Each package contributes its frozen lists (B); keep the imports and allowlist them (C) |

## 3. The console descriptor

**The decision (approved): core gains a `consoles` contribution kind beside `roles` and `romProviders`.** A console package
contributes the descriptors of the consoles its cores run; the shell builds one registry from the generated list, in
product order. `sameboy` contributes the Game Boy descriptor; `mesen` contributes three: the NES, the GBA, and the SMS/Game
Gear family. A descriptor is per graph node type (the tables `buildGraph` keys by node type are the node's, and `gg` and
`sms` share a node), and lists the platforms that node serves.

```ts
// packages/core/ts/consoles.ts (new; the contract only, no table)
export type Platform = string;   // documented alias (decision 3); the registered ids, e.g. "gb" "nes" "gba" "sms" "gg"
export type Core = string;       // the core-config role's kind (platform.ts's header: the role is named after its core)

export type Knob = (v: unknown) => boolean;                  // exactly buildGraph's Knob (moves here, with
export const label: (values: readonly string[]) => Knob;     //   label / flag / within / intIn, so a package can write
export const flag: Knob;                                     //   its own table)
export const within: (min: number, max: number) => Knob;
export const intIn: (min: number, max: number) => Knob;

export interface ConsolePlatform {
  readonly id: Platform;
  readonly label: string;                          // "Game Boy", "Game Gear": the names error text and menus use
  readonly extensions: readonly string[];          // lowercase, dot included: [".gb", ".gbc"]
  /** Settings the node gets for this platform alone, beside the knobs: a Game Gear's { gameGear: true }. */
  readonly nodeSettings?: Readonly<Record<string, unknown>>;
  /** True where the extension alone classifies a file with no recognised content (headerless SMS/GG homebrew). */
  readonly extensionOnly?: boolean;
  /** The core-config field that opts a system of this platform out of driving a registered real cart (decision 12). */
  readonly realCartOptOut?: string;
}

export interface ConsoleDetect {
  /** The bytes of a file's prefix this detector runs on. The registry reads each distinct length once, shortest first,
   *  and stops at the first answer (section 3, "Classification"). */
  readonly prefixBytes: number;
  /** Tiebreak among detectors that both match one prefix: lower first (decision 10). */
  readonly rank: number;
  /** The platform the content names, or null. `ext` (lowercase, dot included) is passed on the last tier only and may
   *  only break a tie the content cannot (a Sega header with an unfilled region nibble); it never overrides a match. */
  run(bytes: Uint8Array, ext: string | undefined): Platform | null;
}

export interface ConsoleStream { readonly port: string; readonly stereo: boolean }

export interface ConsoleSplit {
  /** Output file suffixes in stream order ("pulse1" ...), what a render names a split's files with. */
  readonly names: readonly string[];
  /** The core-config patch the split needs: { channelExportMode: "individualMono" } for a NES's channels. */
  readonly configPatch?: Readonly<Record<string, unknown>>;
  /** The tap mode the engine arms for a lone system under this split (buildGraph's TapArm: 1 pins, 3 channels). */
  readonly tapMode?: number;
}

export interface ConsoleDescriptor {
  readonly id: string;                       // "sameboy.gb", "mesen.nes", "mesen.gba", "mesen.sms"
  readonly core: Core;
  readonly nodeType: string;                 // "rp.sameboy.gb"
  readonly platforms: readonly ConsolePlatform[];
  readonly detect?: ConsoleDetect;           // absent: found by extension alone, or never (none today)
  /** The prefix the ROM providers are shown for this console (0x8200 for Sega, whose header is at a bank's end); absent: 0x150. */
  readonly providerHeaderBytes?: number;
  readonly hasBattery?: (header: Uint8Array, platform: Platform) => boolean;   // absent: true (never wrongly grey a save)
  /** buildGraph's three tables for the node, and the stream order of its output. */
  readonly settingFields?: Readonly<Record<string, Knob>>;
  readonly constructFields?: Readonly<Record<string, { takes: Knob; fallback: unknown }>>;
  readonly configParams?: readonly string[];
  readonly streams?: (mode: number) => readonly ConsoleStream[];     // absent: [{ port: "mix", stereo: true }]
  readonly capabilities?: {
    readonly linkCable?: { readonly field: string };                // "linkGroupId": the config field naming a cable group
    readonly serialOut?: true;                                      // the core can capture its serial out for a decoder node
    readonly channelTap?: true;                                     // the engine's split tap can arm on it (a NES)
  };
  readonly splits?: { readonly channels?: ConsoleSplit; readonly pins?: ConsoleSplit };
  readonly render?: { readonly mixLanes: "stereo" | "left"; readonly defaultPlay?: { readonly button: number } };
}
```

and, on the contribution contract (`packages/core/ts/definePackage.ts`), `readonly consoles?: readonly ConsoleDescriptor[]`
plus the n8 pack's list:

```ts
export interface RealCart { readonly platform: Platform; readonly cartNode: string }   // { platform: "nes", cartNode: "rp.n8.nes" }
// PackageContributions.realCarts?: readonly RealCart[]
```

**What each console contributes** (the data moves out of the shell verbatim; the sources are the lines section 1 lists).

- `sameboy` (`packages/sameboy/ts/consoles.ts`): `sameboy.gb`, core `sameboy`, node `rp.sameboy.gb`; platform `gb` (`.gb`, `.gbc`);
  detect rank 2 on the Game Boy logo at 0x104 over a 0x134 prefix; `hasBattery` is the cart-type set at `$147`;
  `settingFields` from `SETTING_FIELDS["rp.sameboy.gb"]`, no construct fields, no params; four stereo streams
  (`ch1` to `ch4`); `capabilities { linkCable: { field: "linkGroupId" }, serialOut: true }`; a `channels` split with
  names `pulse1 pulse2 wave noise` and no patch or tap; `render { mixLanes: "stereo", defaultPlay: { button: 7 } }` (the Start
  button, `render.ts:44`).
- `mesen` (`packages/mesen/ts/consoles.ts`): `mesen.nes` (platform `nes`, `.nes`, detect rank 0 on `NES\x1a`, `hasBattery`
  iNES flags6 bit 1, the NES setting fields, construct fields and the `expansionVolume` param, the three NES stream layouts,
  `capabilities { channelTap: true }`, splits `channels` (names `square1 square2 triangle noise dmc`, patch
  `{ channelExportMode: "individualMono" }`, tap mode 3) and `pins` (names `pulse tnd expansion`, patch
  `{ channelExportMode: "stereoModPins" }`, tap mode 1), `render { mixLanes: "left" }`); `mesen.gba` (`gba`, `.gba`, rank 1
  on the logo at 0x04, no fields, no battery rule); `mesen.sms` (`sms` `.sms` and `gg` `.gg` with `nodeSettings
  { gameGear: true }` and `extensionOnly`, rank 3 on `TMR SEGA` over a 0x8200 prefix with the region-nibble rule,
  `providerHeaderBytes: 0x8200`, construct field `enableFm`).
- `n8` (`packages/n8/ts/app.ts`): `realCarts: [{ platform: "nes", cartNode: "rp.n8.nes" }]`. `drivenCartSystem` (buildGraph 149)
  becomes "the first system whose platform has a registered cart node and whose core config's `realCartOptOut` field is not
  `false`"; `cartNodeId` and the node type come from the registration. The opt-out field stays mesen's (decision 12): its
  NES platform declares `realCartOptOut: "driveRealCart"`, the field its role schema already holds (`mesen/ts/app.ts:92`) and
  saved projects already carry, and `drivesRealCartOf` becomes the descriptor lookup the store and the menu share.

**The registry** is the shell's `src/consoles.ts` (beside `romProviders.ts`, `trackerRegistry.ts` and `kitFormats.ts`), over
a generated `CONSOLES` list and `REAL_CARTS` list in `src/generated/packages.ts`, in product order. `src/platform.ts` stays
as the module the callers know, with the same names over lookups: `defaultCoreFor`, `systemNodeTypeFor`, `detectPlatform`,
`romHasBattery`, `ROM_SNIFF_LEN` and `SEGA_SNIFF_LEN` become derived values and functions (the first and last distinct
`prefixBytes`). `SystemNodeType` becomes `string`. The tables in `graph/buildGraph.ts` (`SETTING_FIELDS`, `CONFIG_PARAMS`,
`CONSTRUCT_FIELDS`, `streamsOf`) are read from the descriptor by node type; `exportModeOf` and `linkGroupOf` read the
descriptor's fields (`channelExportMode` stays a mesen config field named by `splits`, and `linkGroupId` by `linkCable`).
The capability tests become data: `s.type !== "rp.sameboy.gb"` at 379 and 502 is `capabilities.linkCable`, 485 is
`capabilities.serialOut`, `tapArmOf` (399) and `nesMode` are `capabilities.channelTap` and `splits[...].tapMode`, and the
`isPins(routing) && type !== "rp.mesen.nes"` test at 567 is "the console has a `pins` split".

**Classification.** `classifyRom` (`systemsStore.ts:128`) stays the one place ROM bytes enter TypeScript, reading the same
two prefixes in the same order as today. The registry sorts the distinct `prefixBytes` ascending, reads each once through
`readFilePrefix`, runs every detector with `prefixBytes <= that length` in `rank` order (with `ext` on the last tier only)
and returns the first answer; if none answers, the last tier's extension-only fallback claims the file for the one
`extensionOnly` platform the extension names, else `"unknown"`. So the NES, GBA and GB detectors all declare 0x134 (the
Nintendo tier: the file's first read is shared, as today, though the NES needs 4 bytes and the GBA 36) and Sega declares
0x8200: two reads, 0x134 then 0x8200, exactly as `classifyRom` does. The detector bodies move unchanged (the iNES magic,
the GBA logo, the GB logo, the three Sega offsets with the `+0x200` copier shift and the region nibble rule). The tests
that pin it stay (`test/paths/rom-format.test.ts`, 12 cases, `test/systems/battery.test.ts`).

**Validation and the generator.** `projectConfig.ts` replaces the two `z.enum`s with `z.string().refine(...)` against the
registered platforms and cores, with the message `unknown platform "x" (known: gb, nes, gba, sms, gg)`; an entry failing
the strict parse is dropped by `parseConfig` silently, as an entry with an unknown platform is today (`projectConfig.ts:363`
keeps each `safeParse` success and reports nothing), so the message reaches only a caller that parses an entry directly. The
refinement reads the registry when it runs, not when the module loads. The v1 to v2 migration step looks the core up in the registry
and writes nothing for a non-platform string, as `DEFAULT_CORE[...]` returning `undefined` does today. The generated module
refuses a platform id registered twice, a console descriptor whose `core` has no registered `system`-category role (the
role is named after its core: `systemRoleFor`, `core/ts/systemRoles.ts`), a `RealCart` naming a platform no descriptor
registers, and a descriptor whose `nodeType` two descriptors share. `render/types.ts`'s own `Platform` union is deleted.

**Files that change:** `packages/core/ts/{platform,definePackage,systemRoles}.ts` and the new `consoles.ts`; new
`packages/sameboy/ts/consoles.ts`, `packages/mesen/ts/consoles.ts` and edits to the three `app.ts` files; `n8/ts/app.ts`;
`src/platform.ts`, the new `src/consoles.ts`, `src/projectConfig.ts`, `src/systemsStore.ts`, `src/graph/{buildGraph,
systemResources,directGraph}.ts`, `src/savPaths.ts:77`, `src/fileSelection.ts:20`, `src/render/types.ts`;
`scripts/lib/packages.mjs` and the regenerated `src/generated/packages.ts`.

## 4. The cart hooks the stores reach through

### 4.1 Tracker `render`

`packages/tracker/ts/trackerIntegration.ts` gains an optional member, implemented by lsdj, risa and smsggdj (BlipToaster
has no songs and none of this):

```ts
export interface TrackerRender {
  /** The saved songs of a battery, for --list-songs and for the not-found and empty-slot messages. */
  listSongs(sav: Uint8Array): readonly SongInfo[];
  /** Before the boot: the SRAM the fresh system boots from with `index` promoted to the working song (LSDj, risa). */
  seedSong?(sav: Uint8Array, index: number): Uint8Array | null;
  /** After the boot settle: write song `index` into the booted core (smsggdj). Throws on every failure path. */
  loadSong?(host: RenderHost, id: number, rom: Uint8Array, sav: Uint8Array, index: number): void;
  /** Press play. Returns the lead-in chunks the press rendered. Absent: the console's default gesture. */
  pressPlay?(host: RenderHost, id: number, rom: Uint8Array, sink: RenderPlaySink): Float32Array[][];
  /** The "still playing?" probe for length auto-detect, or null (the render then runs for the fixed length). */
  playingProbe?(host: RenderHost, id: number, rom: Uint8Array, warn: (msg: string) => void): (() => boolean) | null;
}
// TrackerIntegration.render?: TrackerRender
```

`RenderHost` and `RenderPlaySink` are tracker's neutral types for what a hook needs of the shell (`readRam(id)`, `readCpu(id,
addr)`, `writeRam(id, offset, bytes)`, `pressButton(id, button, down)`, `renderChunk(ms)`, `renderAudio(ms)`); the render's
`RenderContext` satisfies them structurally, so a hook is testable without a host. `selection by name` stays in `render.ts`
(it matches `--song` against `listSongs`): the LSDj, risa and smsggdj lookups are the same three loops today with different
list functions. The moves: LSDj's `resolveLsdjSongSeed` (`render.ts:154`) and the NR52 probe (`:762`) into lsdj; risa's
`resolveRisaSongSeed` (`:195`), `pressRisaPlay` (`:96`), the probe (`:767`) and its unsupported-version warning into risa;
smsggdj's `loadSmsggdjSongLive` (`:311`) and its constants (`:285` to `:290`), `smsggdjIsPlaying` (`:248`) and the Pause
press into smsggdj. `smsggdjIntegration.liveLoad` (the pure write list) is already smsggdj's and `loadSong` calls it
directly instead of the shell calling it through `trackerRegistry`.

### 4.2 Tracker `playback`

```ts
export interface TrackerPlayback {
  syncMode(sram: Uint8Array | null): string | null;          // savSyncMode: the cart's own SYNC setting, polled
  songSignature(sram: Uint8Array | null): number;            // workingSongSignature: change detection, 0 = unreadable
  rowTicks(sram: Uint8Array | null): RowTicksTable | null;   // songRowTicksFromSav: the predictor's table
  anchor: {                                                  // sampleControllerAnchor / controllerReader
    reader(header: Uint8Array): PlaybackReader | null;       // LsdjReader.fromHeader, null when unsupported
    playing(state: unknown): boolean;                        // anyChannelPlaying
    rows(state: unknown): (number | null)[];                 // anchorRowsFromState
  };
}
// TrackerIntegration.playback?: TrackerPlayback
// PlaybackReader { read(wram: Uint8Array): unknown }       (tracker owns it, and RowTicksTable / ControllerAnchor)
```

Tracker owns the neutral types (`ControllerAnchor { rows: (number | null)[]; seq: number }` and the row-tick table;
`playbackModel.ts` is already tracker's and says the seam is meant for risa too); `lsdj/ts/playback/{predict,anchor}.ts`
import them from tracker instead of declaring them. The sync-mode to drive and listen mapping `graph/controller.ts` holds
(`serialOutCaptureFor` compares `LsdjSyncMode.MidiOut` and `MasterSync`, lines 125 to 138) becomes a lsdj export the
`lsdj-sync` role's protocol data carries (`playback.captureFor(mode): "drive" | "listen" | "off"`), so `controller.ts`
imports no cart. `projectStore` asks `resolveTracker(roles)?.playback`: `controllerReader` (518) replaces the `"lsdj-sync"`
literal by the integration lookup, `refreshControllerSong` (476) and `controllerModel` (192) call `playback.syncMode`,
`songSignature` and `rowTicks`. The poll timer, `controllerSongSig`, `controllerSync`, `controllerAnchor` and the publish
stay in the store. **A system with no `playback` member reads "no sync, no signature, no table, no reader"**, which section 6
checks against what LSDj's functions return for a non-LSDj battery today (it matches for sync mode and for a 128 KiB image,
and differs for two narrower cases that no shipped combination reaches).

### 4.3 `sramSignature`

`TrackerIntegration.sramSignature?(bytes: Uint8Array): number | null` is LSDj's `lsdjSramSignature` moved from
`sramAutoSave.ts:39` into lsdj (with its `decodeSong` / `encodeSong` import, `sramAutoSave.ts:15`). The shell's
`sramSignature(bytes)` (`:56`) becomes: for each integration in product order, the first non-null `sramSignature(bytes)`,
else `hashBytes(bytes)`. The rule is byte-based as today (it never looked at a role, only at the 128 KiB image and the `jk`
magic), so the registry asks each integration without knowing the system. The same signature must be taken for the live
bytes and the on-disk bytes (`decideAutoSave`, `dirtySramTargets`), which it is because both go through the one function.

### 4.4 `songImport`

`TrackerIntegration.songImport?: { extensions: readonly string[]; import(host, sys, paths): boolean }` replaces
`lsdjSongImport.ts` (57 lines: `.lsdsng` and `.lsdprj`, the sav edit, the kit overrides, the one reload) which moves into lsdj
with `isSongPath` becoming `extensions.some(...)`. `fileDrop.ts:20` stops importing it: `DropContext.targetIsLsdj` (`:48`,
set at `ui/App.tsx:248` by `hasRole(target?.roles, "lsdj-sync")`) becomes `targetSongExtensions: readonly string[]`, the
target's tracker's `songImport.extensions` (empty for none), and `resolveDropAction` (`:66`) tests the dropped files against
it. The refusal reason `"song files need an LSDj instance"` becomes `"song files need a tracker that imports them"`
(section 6). The two other call sites, `ui/App.tsx:47, 271` and `ui/screens/menu/menuDefs.ts:42, 1650`, call
`integration.songImport.import(...)`; both are F4b files, edited here for the signature only.

### 4.5 Role hooks

`core/ts/systemRoles.ts`'s `RoleType` gains three optional members (alongside `onConstruct`):

```ts
parameters?(config: Record<string, unknown>): readonly RomParameter[];
deriveConfig?: { headerBytes(short: Uint8Array): number; derive(header: Uint8Array, config: Record<string, unknown>): Record<string, unknown> };
hostLatencyMs?(config: Record<string, unknown>): number;
```

- **`parameters`.** `MGB_PARAMETERS` (`parameterMap.ts:316`) goes to mgb, the BlipToaster tables (`BLIPTOASTER_SPECS`, 256, six
  chips) to bliptoaster, each returning its expanded lanes (the unknown-chip fallback to the 2A03 core moves with the table).
  The CC-spec types and `expandRomSpec` (289) must move too, to a new `core/ts/romParameters.ts` (`RomParameter`, `RomSpec`,
  `expandRomSpec`), because a package may not import the shell and each cart builds its tables with them. `parameterMap.ts`
  keeps the slot pool, `preRoutingChannel` and `projectParameterMap`, which asks the registry (`stores.registry`, passed in by
  `pluginControlPlane`) for `roleType(kind)?.parameters?.(config)` instead of `parametersForRole`.
- **`deriveConfig`.** BlipToaster's chip scan (`withDerivedRoleFacts`, `systemsStore.ts:1506`) becomes the `bliptoaster`
  role's hook: `headerBytes(short)` is `needsChipLabelScan(short) ? BLIPTOASTER_CHIP_SCAN_LEN : short.length` (a function of the
  short header, because today the deep read of 0x4010 happens only for a ROM predating the chip tag, so "the deep read happens
  only when a role asks"), and `derive` is the `blipToasterChip` write. The store reads the short header, asks every
  role's `deriveConfig.headerBytes(short)`, reads the largest once, and applies each `derive`. The `platform "nes"` it passes
  today (`:1513`) is the role's knowledge now. **It runs on both paths (decision 11):** on a fresh build right after the
  providers (`defaultRoles`, `systemsStore.ts:1354`), and on a loaded project where `withMissingFeatureRoles` (1489) runs it
  now. The providers therefore see the short 0x150 header (Sega: the console's `providerHeaderBytes`), which is enough for
  BlipToaster's provider to recognise the cart (`isBlipToasterRomHeader` reads the SIG marker inside it); the provider
  writes no `chip`, and `readRoleHeader`'s NES branch (83 to 90) goes. An untagged S5B ROM gets `s5b` in the same construct
  that builds it, as today, so nothing visible changes, and `chip` has one writer.
- **`hostLatencyMs`.** `lsdj-sync` returns 33 for `MidiSync` and `MidiSyncArduinoboy` and 0 otherwise; the 33 and the
  `LsdjSyncMode` import leave `pluginControlPlane.ts:19, 99 to 104`. `__rp_syncLatencyMs` sums over the systems'
  PLACED roles (`protocolPipeline`, so an `lsdj-sync` the pipeline leaves out on an mGB cart still contributes nothing) as the
  maximum, asking `registry.roleType(kind)?.hostLatencyMs?.(config)`.

### 4.6 Link groups and `savFromJson`

- **Link groups.** `inheritLinkGroup` (`systemsStore.ts:1053`) finds each system's core-config role by the existing rule that
  a core's config role is named after the core (`e.roles.find(r => r.kind === e.core)`, as `graphSystems` already does at 264),
  and the "BOTH are SameBoy" guard is the descriptor's `capabilities.linkCable` of the system's platform; the config field it
  writes is `linkCable.field`. No `"sameboy"` literal remains in the store.
- **`savFromJson` leaves the contract.** It leaves `ConstructCaps` (`core/ts/systemRoles.ts:75`), `Backend` (`backend.ts:470`),
  `HostBackend`'s pick (`:486`) and `realBackend.ts:94`; lsdj's construct hook (`lsdj/ts/app.ts:26`) calls its own
  `savFromJson` (`@retroplug/lsdj` already exports it). Three tests count the backend's `savFromJson` log entry and are
  rewritten to assert the seeded `sramBytes` instead (`test/systems/lsdj-seed.test.ts:33, 46`,
  `test/systems/store-mutate.test.ts:161, 169`, `test-native/app-lsdj-seed.test.ts`), which is what they exist to prove.

### 4.7 The four doors, and where the tests go

- `src/kit/compile.ts` is deleted; `src/kit/index.ts` goes with it (its other re-exports are `NO_SLOT_REASON` and `KitSpecError`,
  which the three cli sessions and the tests import from `@retroplug/dsp` directly). `compileKit`, `compileDmc` and the two format
  constants are imported from `@retroplug/lsdj/kitFormat` and `@retroplug/risa/kitFormat` by the tests that use them;
  `kitFormats.ts` imports `KitFormat` from `@retroplug/dsp/kitContract`.
- `src/tracker/index.ts` is deleted; `liveSav.ts` stays and its importers (`projectStore.ts:22`, the menu, three tests) import
  `./tracker/liveSav`; the catalogs are imported from their packages by the tests that use them.
- `trackerRegistry.ts` keeps `TRACKER_INTEGRATIONS` and the three resolvers; the four re-exports go (`render.ts` stops using
  `smsggdjIntegration`, and the seven tests import it from `@retroplug/smsggdj/smsggdjIntegration`).
- `audioDriver.ts` keeps the `AudioDriver` facade and its own types; its re-exports (53 to 61) go. A test that wanted the kit
  spec types imports them from the owning package (`@retroplug/dsp/kitContract`, `@retroplug/lsdj/kitFormat`,
  `@retroplug/risa/kitFormat`).
- **Tests follow their code.** A test of code that moved goes to the owner's `ts/test` (its slug becomes `<package>/<path>`):
  the `parameterMap` cases that pin the mGB and BlipToaster tables go to mgb and bliptoaster (the projection cases stay), the
  `lsdjSramSignature` cases to lsdj, the song-import cases to lsdj, the render hooks' cases to their carts. Shell tests that
  drive carts end to end (`test-native/*`, the menu tests that call `menuDefs`) stay. The skip baseline records nothing for
  the ts tier, so a move changes no entry.

## 5. Render, and the embed

### 5.1 Render

`render.ts` keeps its pipeline (settle, lead-in, sink, end detection, the part files and the cancel points) and loses every
platform switch. It reads the ROM's header through `ctx.backend.readFilePrefix`, classifies it with the registry
(`classifyRom`'s prefixes), takes the roles `registry.defaultRoles(core, platform, header, "")` gives (the same roles the
built system will get, because the store builds them the same way), and `resolveTracker(roles)` the integration. The
`RenderContext` gains the registry (the CLI session and the worker both hold one: `bootSession.ts:20`, `render/worker.ts:73`).

- **Platform.** `platformOf(rom)` (by extension today) becomes the classification; the extension is kept as the descriptor's
  `extensions` for the fallback when the file cannot be read (section 6, edge E1).
- **Songs.** `--song` / `--song-index` use `integration.render.seedSong` (LSDj, risa: the SRAM the system boots from) or
  `loadSong` (smsggdj: after the boot settle, as now); `--list-songs` in `cli/sessions/render.ts:23` calls `render.listSongs`.
  A ROM whose integration has neither refuses the flag, naming the carts that offer it (below).
- **Play gesture.** The integration's `pressPlay` (risa's SELECT+START with its two discarded prep chunks, smsggdj's Pause
  with its "already playing?" check) else the descriptor's `render.defaultPlay` (Game Boy: Start, which LSDj and mGB rely on),
  else none. Generic NES and SMS ROMs keep none, because they match no integration and their descriptor declares none.
- **End detection.** The integration's `playingProbe` (LSDj's NR52 poll, risa's sequencer flag; smsggdj and BlipToaster have
  none, so a fixed length, and the smsggdj "loops forever" warning stays in its hook). LSDj's probe is chosen today when the
  SAV is LSDj's and is chosen under the new rule when the ROM is: edge E2.
- **Split.** `--split pins|channels` is validated against the descriptor's `splits`; `rolesForRender`'s injected `"mesen"`
  patch (`render.ts:392`) becomes the split's `configPatch` applied to the system's core-config role (`kind === core`), and
  the file names come from `ConsoleSplit.names` and the mono/stereo lane choice from `render.mixLanes`. The `GB_CHANNELS`,
  `NES_PINS` and `NES_CHANNELS` constants (62 to 64) and `NES_START`, `GB_START`, `SMS_START` (44 to 47) leave the shell.
- **Error text** naming carts is built from the registered integrations in product order: where `render.ts` writes `a Game
  Boy (LSDj) / NES (risa) / Master System + Game Gear (smsggdj) feature` it now writes `a feature of <label (platform label)>,
  ...` for each integration with a `render.seedSong` or `loadSong`, in product order (`lsdj`, `smsggdj`, `risa`). The words
  change deliberately; section 6 lists the tests that read them.

### 5.2 The embed

mgb's TS declares the embedded synth, and the generator checks it against the manifest:

```ts
// core: PackageContributions.embeds?: readonly { id: string; label: string; platform: Platform }[]
// packages/mgb/ts/app.ts
export default definePackage({ roles: [MGB_ROLE], romProviders: [MGB_ROM_PROVIDER],
                               embeds: [{ id: "mgb", label: "mGB (GB MIDI Synth)", platform: "gb" }] });
```

`scripts/lib/packages.mjs` gains the check (it reads no manifest `embed` today; only `cmake/packages.cmake:31 to 39` does): every
manifest `embed` id has exactly one TS declaration in the same package, every declaration has a manifest entry, a platform
names a registered console, and an id is not declared twice across packages; an unpaired side refuses the generation. The
generated `EMBEDS` list feeds `SystemsStore.loadEmbedded(id)`, which takes the platform from the declaration instead of
`systemsStore.ts:1161, 1303`'s `platform = "gb"`. `projectConfig`'s `embeddedRom` is unchanged on disk (`"mgb"`). The start
menu's `Load mGB (GB MIDI Synth)` row (`menuDefs.ts:2560`, and the infoRow `"mGB"` at 2470) reads its label from the registry;
F4b moves the row into the mgb package.

### 5.3 The cart-named API

`SystemsStore.loadMgb()` (371) becomes `loadEmbedded(id: string): number | null` (refuses an id no package declares, returning
null as `loadInPlace` does for a failed load). `AudioDriver.compileKit(spec)` / `compileDmc(spec)` (`audioDriver.ts:295, 296`)
become `compileKitFormat<S>(id: string, spec: S): KitCompileResult`, dispatching through `kitFormat(id)` as the two bodies
already do; the specs are typed by the owner package at the call (`compileKitFormat<KitCompileSpec>("lsdj", spec)`), so the
driver names neither cart. `cli/sdk-types.d.ts:391` loses `loadMgb` and gains `loadEmbedded`; the BlipToaster consumer calls
none of the three (a grep of `/workspaces/bliptoaster`, not edited). Every caller in the repo is updated, 33 files for `loadMgb`
(section 1), including the Node addon's `emu-ops.mjs` and README, and the specs and docs that name it
(`spec/03-ts-layer.md:133, 216`, `spec/09-cli-debugging.md:65`, `spec/07-remaining-work.md:829`).

## 6. Behaviour edges

Each was checked against the code; none moves a baseline.

- **E1. Render classifies by content, not extension.** `platformOf` reads the extension only (`render.ts:66`); the store
  classifies by content, and a render with a mislabelled ROM (a `.nes` holding a Game Boy ROM, a `.bin` GB ROM) passes
  the first and builds with the second. The new rule has one answer (the content's), so a mislabelled file renders with
  the right options instead of the wrong ones, and a `.bin` Game Boy ROM, which today fails `--split channels` with `got other`,
  now accepts it. A file that cannot be read classifies by extension (the descriptor's `extensions`), so `render-cli.test.ts:114 to 119`
  (a missing `x-no-such.gb`, the message must name the ROM) still holds: the failure comes from the load, as it does now.
- **E2. LSDj end detection is keyed off the ROM, not the SAV.** Today it needs `readFile(sav)` to be a 128 KiB LSDj image
  (`render.ts:763`, `isLsdjSav`); the new rule needs the ROM's integration to be LSDj's (its `lsdj-sync` marker from the title).
  They differ for an LSDj ROM with no sibling `.sav` (today a fixed-length render, in the new rule an auto-detected one; the
  store seeds a valid empty sav at construct, `lsdj/ts/app.ts:26`, so the probe works) and for a non-LSDj GB ROM next to an LSDj
  sav (today a probe on an mGB ROM, in the new rule none). The 13 render-baseline cases and the `render-cli` / `render-chunks`
  / `audio-render` cases all use an LSDj ROM with its matching sav or fix a duration (`CASES`, `render-cases.ts:69` on: the five
  LSDj cases are the real LSDj ROMs with their saves, the SGB jingle is `start: false` with a duration, the smsggdj and risa
  cases fix a duration or pair risa with its own sav), so the answer for the run's affected cases is **none**; the prototype
  confirms it by running them with `RP_FAIL_ON_SKIP=1` against the fixtures. The output file name defaulting to the working song
  (`currentSongName`, 430) follows the same rule through `integration.songs.workingName(raw)`, which each catalog guards itself.
- **E3. A system with no `playback` reads "no sync, no signature".** For a non-LSDj battery: `savSyncMode` returns null
  (`isLsdjSav` fails, `fromSav.ts:50`), as an absent member does; `songRowTicksFromSav` throws inside `decodeSav` for a 128 KiB
  image with no `jk` magic (`codec/sav.ts:77`) and returns null, so `controllerModel`'s `songRowTicks: table ?? []` is the
  same `[]`; `controllerReader` is gated on `lsdj-sync` so it is null. **Two cases differ**: `songRowTicksFromSav` on a battery
  of 32 KiB up to 128 KiB (a 64 KiB risa battery is one) is not null today (`decodeSav` reads it as an early-LSDj image and
  `songRowTicks` runs over bytes that are not a song), where the absent member gives `[]`; and `workingSongSignature` hashes any
  image of at least `kSongByteCount` bytes (`fromSav.ts:62`) and so is non-zero and moves for a non-LSDj battery, where the
  absent member gives 0 and never fires `publishGraph` + `notifySystemsChange` (the re-publish is deduped by the graph, the
  notification is not). Both are the controller polling a system that is not an LSDj cart, which MI.MAP does not drive; the
  change is a narrowing to what was intended. **One test file reaches the second case:** `test/controller/songRefresh.test.ts`
  builds its system from the generic `gbRom()` fixture (no LSDj title, so no `lsdj-sync` role) and feeds it a blank bank;
  six of its cases (the signature and publish cases from line 99, and the anchor cases) rely on the signature moving for a
  system that is not an LSDj cart. Under the new rule they seed `lsdjRom()` (`test/systems/fixtures.ts:38`) instead, which is
  what they mean; the non-LSDj sync case at 178 keeps `gbRom()` and still reads null. Stage 3 changes the fixture in that file
  and nothing else in it, and its commit names this as a test-fixture change, not a behaviour change of a shipped combination.
- **E4. Error text.** The words that name carts change (section 5.1): `render: --song/--song-index is a feature of ...`. Tests
  that read the old words: none contain the full sentence; `test-native/sms-render.test.ts:171` checks only the substring
  `--song/--song-index`, which the new text keeps, and `cli/sessions/render.ts:48`'s `--list-songs` sentence (no test)
  changes the same way. `fileDrop`'s `"song files need an LSDj instance"` is a reason string (`ignore`), and
  `test/file-drop/route.test.ts:134 to 145` builds a `DropContext` with `targetIsLsdj`, so the field's rename changes that test's context and its song-file cases (the reason string itself is read by no test I found).
- **E5. Detection order.** The brief's statement that the four content signatures are disjoint is false for crafted files:
  `test/paths/rom-format.test.ts:76` pins that a NES magic beats a GB logo and a GBA logo beats a GB logo. In product order the
  Game Boy descriptor comes first (`sameboy` is before `mesen`), so product order alone would flip both. The descriptor carries
  `rank` (section 3) so the order is data and the test stays. The user chose it (decision 10).
- **E6. `Platform` as `string` loses compile-time exhaustiveness.** A `switch` on a platform no longer fails to compile
  for a missing arm. The remaining platform switches are all being removed here; the grep test (section 7) is what keeps one
  from coming back.

## 7. Proof

### 7.1 The shell-source boundary rule

`check:boundaries` ([check-boundaries.mjs](../packages/retroplug/scripts/check-boundaries.mjs)) judges packages only today (header lines
13 to 15: `retroplug` and `native` are the shells and "are not judged"). F4a adds rule 9: **every `@retroplug/*` specifier in
`packages/retroplug/src` must match an explicit allowlist, each entry with its reason**; a specifier naming a cart package
(lsdj, risa, smsggdj, bliptoaster, mgb, lsdj-launchpad, n8) or a console package (sameboy, mesen) outside the list is refused. The
list is a constant in the script, an entry no import uses fails the check (as `EXCEPTIONS` does), `src/generated/` stays
unjudged (the one place that names every package, as the header says), tests under `packages/retroplug/test*` are outside `src` and
unjudged, and `ui/` and `cli/` join in F4b. The allowlist, derived from what stays legitimately after the moves:

| Specifier | Why it stays |
| --- | --- |
| `@retroplug/core`, `@retroplug/core/*` (systemRoles, constructSpec, consoles, platform, definePackage, midiRouting, configFields, graphDocument, offlineJob, protocol, descriptor, register, defineNode, canonicalJson) | The contract every package implements; the shell is its host |
| `@retroplug/tracker/{songCatalog, assetCatalog, trackerIntegration, playbackModel, workingSongBlob, packageContributions}` | The tracker contract's types and the neutral working-song image the store carries |
| `@retroplug/dsp/{kitContract, kitJobs, kitEffect, packageContributions}`, `@retroplug/dsp` | The kit compile driver's contract and its shared half (`NO_SLOT_REASON`, `KitSpecError`); dsp is a library, not a cart |
| `@retroplug/launchpad/{registry, enums}` | The control surface's registry (`controller/registry.ts`) and the controller target the project settings store (`projectConfig.ts:25`) |

What leaves the list by the moves (each is a line section 1 counted): every import of `lsdj`, `risa`, `smsggdj`, `bliptoaster`
(except through the contract above), `@retroplug/sameboy/enums` and `@retroplug/mesen/enums` in `buildGraph.ts` and
`projectConfig.ts`, `@retroplug/lsdj/enums` in `controller.ts`, `pluginControlPlane.ts` and `projectConfig.ts`. **The v2 to v3 migration (decision 13)** read `MODEL_VALUES`,
`HIGHPASS_VALUES`, `REGION_VALUES`, `CHANNEL_EXPORT_VALUES` and `LSDJ_MODE_VALUES` (`projectConfig.ts:21 to 27`) for
`V3_ROLE_ENUMS` (186 to 190); the five index-ordered lists are frozen as literals beside the step, the imports go, and
`test/project/migrate-v3-frozen.test.ts` asserts every frozen value is still a valid value of the owning package's current
schema, so a rename in a package fails there instead of migrating a v2 file to a default.

**RED first.** The rule is added before any move and run on today's tree, where it lists every deep import (the 47 lines
of section 1, less the contract ones) and the five-way count by file; the stage commits shrink the list and the last stage
leaves it empty. A fixture pair in `test/boundaries` (a shell tree with one forbidden import, one with only allowed ones) holds
the rule the way the existing fixtures hold rules 1 to 8.

### 7.2 The grep test

**A test that `src/` holds no platform literal outside comments is reliable and is added in stage 5.** Today
`"gb" | "nes" | "gba" | "sms" | "gg"` as a string literal outside a comment occurs 46 times in 6 files (`render/render.ts` 26,
`platform.ts` 10, `systemsStore.ts` 6, `graph/buildGraph.ts` 2, `render/types.ts` 1, `projectConfig.ts` 1), and the extension
literals `.gb` ... `.gg` in `savPaths.ts:77`, `render.ts:68 to 73` and `platform.ts:111, 112` (and as glob patterns in
`fileSelection.ts:20, 21`, which the test's literal scan also catches as `*.gb`). After the moves none remains, and the
test reads tokens with TypeScript's scanner (`ts.createScanner`, from the same pinned `typescript` the boundary check
loads; `preProcessFile` reads imports only) rather than a regular expression over text, so a literal in a template string or a comment is told apart. What it cannot see is a computed
platform (`["g", "b"].join("")`) or a platform id spelled in a string that is not a bare literal: it guards accident, not a
determined author, as the boundary check says of itself. Allowed: `src/generated/` and the registry's own error messages
(which interpolate ids rather than spell them).

### 7.3 The oracles held identical

Nothing is re-recorded and no `RP_*_UPDATE` is set. On the fixtures-present run with `RP_FAIL_ON_SKIP=1`, and on the CI picture
(`__RESOURCES_DIR__` pointed at a missing directory) for the fixture-free subset:

- strict parity (`pnpm test:native parity`, 30 files and 250 cases at F2's acceptance), the kit baseline (39 cases, 12 edge banks), the render
  baseline (13 cases), `render-chunks`, `render-cli`, `render-guard`, the `buildGraph` goldens and role goldens
  (`golden/build-graph-roles.json`), the protocol digests, the `[packages]` golden (**it moves only if a console or embed
  registration adds a type or changes a span label, and none does: the descriptors are TypeScript data**), and the device and box
  image hashes and pack bytes (no native source changes);
- `pnpm test`, `test:native`, `test:ui` and the pure-TS menu tests (`test/menu/*`, which call `menuDefs` and so exercise the
  changed code from above), `test:plugin` (no C++ changes, run for the engine-graph and packages cases), `check:boundaries` with
  the new rule green and `check:docs`;
- BlipToaster's suite and NESVJ's against the rebuilt CLI (neither calls `loadMgb`, `compileKit` or `compileDmc`), and
  `reaper:params` / `reaper:params-clap` (the parameter map moved behind `parameters`, and this is the only proof a host acts
  on the re-read flag).

### 7.4 New tests, by stage

1. **Consoles.** `test/consoles/registry.test.ts`: classification of every ROM fixture and the synthetic headers of
   `rom-format.test.ts`, identical to today (a recorded table, taken before the change), the two-read order asserted through a
   counting backend, the polyglot priorities (E5); the extension-only fallback; a duplicate platform, a descriptor whose core has no
   system role and a real cart naming no platform each refused by the generator (`test/packages/packages-generator.test.ts`
   fixtures, RED first); `projectConfig` refusing an unknown platform and core with the known set in the message; a table-equality
   test that probes each old `SETTING_FIELDS` / `CONSTRUCT_FIELDS` / `CONFIG_PARAMS` entry with a battery of values and compares
   the descriptor's answer (recorded from the old tables before they are deleted).
2. **Render.** The existing render suites unchanged; `test/render/render-hooks.test.ts` driving each integration's hooks over
   a fake `RenderHost`; a case for E2 (an LSDj ROM with no sav, auto-detect on) and E1 (a `.bin` GB ROM accepts `--split channels`).
3. **Playback and tracker hooks.** `test/tracker/playback-absent.test.ts` pinning E3's equal and differing cases; the
   controller poll tests over `resolveTracker(...).playback`; `sramSignature` through the registry (an LSDj image, a non-LSDj
   image, a 128 KiB image with no magic); `fileDrop` with `targetSongExtensions` (`test/file-drop/route.test.ts` moves its context field).
4. **Role hooks and embed.** The generator's embed pairing (a manifest `embed` with no TS declaration, a declaration with no
   manifest entry, an unregistered platform; RED first); `parameters` equality (the lane lists of mgb and the six BlipToaster chips
   equal the old tables' expansion, recorded before the move); `deriveConfig` (the chip scan reads 0x4010 only when asked);
   `hostLatencyMs` through the registry; `loadEmbedded("nope")` returning null.
5. **Boundary and grep.** The new rule's fixtures and the grep test, both RED on the pre-stage tree.

## 8. The stages

Five stages on a prototype branch, DeepSeek Flash implementing and Opus reviewing each before the next, as F3 and F2 ran.
Each stage leaves the tree green on every oracle of 7.3 that it can reach (the parity and render baselines need no
re-record at any stage, so a stage that changes them is wrong).

1. **Console descriptors.** `core/ts/consoles.ts` and the `consoles` / `realCarts` contribution keys; the `sameboy`, `mesen` and
   `n8` descriptors; the generated lists and the generator's refusals; `src/consoles.ts` and `src/platform.ts` over it; open
   `Platform` / `Core`; `projectConfig` validation; `buildGraph`'s tables, capabilities and cart node; `savPaths` and
   `fileSelection` extensions. *Exit:* the registry test, the table-equality test and the generator fixtures green; parity, the
   `buildGraph` and role goldens and `[packages]` unchanged; `tsc` clean on the packages that changed.
2. **Render.** The `render` member on lsdj, risa and smsggdj; `render.ts` and the CLI render session off every platform switch;
   split modes from the descriptor; the registry on `RenderContext`; the new error words. *Exit:* the 13 render-baseline cases
   (with fixtures), `render-cli`, `render-chunks`, `sms-render`, the risa render suites identical; E1 and E2 tests green;
   `render.ts` imports no cart.
3. **Playback and the tracker hooks.** `playback` and the controller poll; `graph/controller.ts` cart-free; `sramSignature`;
   `songImport` and `fileDrop`. *Exit:* the controller, auto-save and file-drop tests green; E3's test green; the launchpad and
   lsdj-launchpad suites and the protocol digests unchanged.
4. **Role hooks and the embed.** `parameters`, `deriveConfig` and `hostLatencyMs`; link groups; `savFromJson` out of
   `ConstructCaps` and the backend; the TS embed and the generator check; `loadEmbedded`; `compileKitFormat` and every caller
   (the 33 `loadMgb` files, the Node addon, the SDK `.d.ts`). *Exit:* the parameter-map equality test, the three rewritten
   `savFromJson` tests, `reaper:params` and the kit baseline identical; no `loadMgb`, `compileKit` or `compileDmc` left in the repo.
5. **Doors, tests, the rule, the docs.** The four doors deleted and their importers repointed; tests moved to their packages;
   rule 9 and its fixtures; the grep test; the docs pass (`AGENTS.md`, `spec/00`, `02`, `06`, `07` and `docs/package-split.md`,
   which splits F4 into F4a and F4b and records per-product bundles as owed). *Exit:* `check:boundaries` green with the allowlist
   as section 7.1 lists it and RED on the stage 4 tree; every oracle of 7.3; the BlipToaster and NESVJ runs.

## 9. Risks and open questions

Each is found against the code; none is resolved here.

1. **Detection order (decided, decision 10).** `rom-format.test.ts:76` pins that NES beats GB and GBA beats GB for a crafted
   header with two signatures, and product order puts `sameboy` first; the descriptor's `rank` keeps today's order as data.
2. **The providers' deep header read (decided, decision 11).** `deriveConfig` runs on both paths and the providers see the
   short header; section 4.5.
3. **Who owns `driveRealCart` (decided, decision 12).** mesen's NES platform names it as `realCartOptOut`; section 3.
4. **`V3_ROLE_ENUMS` (decided, decision 13).** Frozen lists beside the migration step; section 7.1.
5. **`RenderContext` carries the registry (decided by the controller).** `render.ts` classifies roles BEFORE the system
   exists (the SRAM seed is passed to `adopt`), so it needs `registry.defaultRoles(...)`, which the store keeps private.
   `RenderContext` gains the registry, which both constructors already hold (`bootSession.ts:20`, `render/worker.ts:73`); a
   `SystemsStore.rolesForRom(path)` was the alternative, rejected because the render builds no store before the system exists.
6. **A drop target without a project.** `fileDrop`'s `targetSongExtensions` is computed in `ui/App.tsx:248` (an F4b file) from the
   target's integration; with no target it is empty and song files are ignored as today. Not a risk to the oracles; recorded because
   the edit is in a file F4a otherwise leaves.
7. **Type safety.** `compileKitFormat(id, spec)` types the spec at the call, so a wrong spec for an id is a runtime refusal where
   `compileKit` was a compile error; `Platform` and `SystemNodeType` as `string` lose exhaustiveness (E6).
8. **`parseConfig` drops, it does not report (verified).** An entry with an unknown platform fails the strict parse and is
   dropped silently, today and after (`projectConfig.ts:363 to 367`); the refinement's message names the known set for a direct
   caller only. Reporting dropped entries would be a behaviour change and is not F4a's.
9. **Other unverified items.** `menuDefs.ts` line numbers in the survey (F4b); whether any test outside the files grepped asserts `kit/compile`'s module path.
10. **Brief corrections.** Section 1 and 6 record where the brief and the code disagree: the signatures are not disjoint (E5), the
    expansion functions cannot stay in the shell (section 4.5), the generator reads no manifest `embed` today (5.2), and `deriveConfig`
    cannot be a fixed `headerBytes` number (4.5).

## 10. What it leaves

For `spec/07-remaining-work.md`'s F4a bullet:

- **F4b**: the UI onto extensions (menu contributions replacing `TRACKER_UI`, console settings rows, the N8 and Launchpad menus,
  TSX in packages, the HD player and the overlays as views) and the cart CLI tools as a second surface on the same host contract;
  `ui/` and `cli/` join the boundary rule.
- **Per-product bundles**, owed until a product's package list differs.
- **`MemoryType`** stays a closed wire enum (F2's item, unchanged).
- The Windows, macOS and arm64 builds of everything F2 and F3 left
  owed (F4a touches only TypeScript, so it adds none of its own).

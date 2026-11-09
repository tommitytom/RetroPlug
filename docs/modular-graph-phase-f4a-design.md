# Phase F4a: the non-UI registrations

**Date:** 2026-11-09 · **Branch:** `feature/modularize` · **Status:** designed 96d748e5c; prototyped (five stages, each Opus-reviewed and fixed); as built in section 11; plan next. (Sections 1 to 4 were approved by the user 2026-11-08 and 09 and the four questions the draft raised answered 2026-11-09; sections 3 to 10 are corrected in place to what the prototype built, and section 11 records every departure.)

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
| 7 | Cart-named API | `loadEmbedded(id)` and `compileKitFormat(id, spec)` replace `loadMgb` and `AudioDriver.compileKit` / `compileDmc`; every caller in the repo updated; the SDK `.d.ts` loses `loadMgb`; mgb's TS declares its embed `{ id, label, platform }`, cross-checked against the manifest `embed` (user, option A; as built the pairing is refused by the generated module, section 5.2) | Keep the cart names as one-line aliases (B); packages contribute named SDK methods (C) |
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

**As built, the contribution is a manifest member, not a `definePackage` key.** The first prototype stage put `consoles`
and `realCarts` on `PackageContributions` and generated them into `src/generated/packages.ts`, which imports every app
module, so the planner's bundle (`graph/buildGraph.ts`, whose tables the descriptors now are) grew from 12 modules to 210
(780 KB: every app, every role schema, zod) and F1's invariant that the planner carries no node's code, no role's schema
and no app broke. The stage's review found it and the controller decided the fix, the pattern F1 used for the protocol
tables: a manifest member `retroplug.consoles` names a module that default-exports a `ConsolesContribution`
(`{ consoles?, realCarts? }`) and imports only the package's enum modules and `@retroplug/core/consoles`; the generator
writes an import-light `src/generated/consoles.ts` from it; and `test/script/bundle-protocol.test.ts` is held byte for byte
at its `96d748e5c` content (the planner bundle is 16 modules, 29,230 bytes, with no app, no generated `packages.ts` and no
zod). `PackageContributions` therefore carries no `consoles` or `realCarts`.

```ts
// packages/core/ts/consoles.ts (new; the contract only, no table)
// Platform and Core stay in core/ts/platform.ts as documented open `string` aliases (decision 3) and consoles.ts
// re-exports them, so a package may name them from either specifier.
export type Platform = string;   // the registered ids, e.g. "gb" "nes" "gba" "sms" "gg"
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
  /** Tiebreak among detectors that both match one prefix: lower first (decision 10). Two of one rank run in their
   *  descriptors' id order, so the whole order is total. */
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
  /** AS BUILT, not in the approved snippet: the core config's export-mode field and its labels in the native enum's
   *  order, whose index is the `mode` `streams` is asked for (index 0, the mix, is what a config naming no readable label
   *  gives). A split's `tapMode` is the index of the label its `configPatch` names. See section 11. */
  readonly exportModes?: { readonly field: string; readonly values: readonly string[] };
  readonly capabilities?: {
    readonly linkCable?: { readonly field: string };                // "linkGroupId": the config field naming a cable group
    readonly serialOut?: true;                                      // the core can capture its serial out for a decoder node
    readonly channelTap?: true;                                     // the engine's split tap can arm on it (a NES)
  };
  readonly splits?: { readonly channels?: ConsoleSplit; readonly pins?: ConsoleSplit };
  readonly render?: { readonly mixLanes: "stereo" | "left"; readonly defaultPlay?: { readonly button: number } };
}
```

and the contribution (as built, a `retroplug.consoles` manifest member, see above) with the n8 pack's list:

```ts
export interface RealCart { readonly platform: Platform; readonly cartNode: string }   // { platform: "nes", cartNode: "rp.n8.nes" }
export interface ConsolesContribution {   // a consoles module's default export
  readonly consoles?: readonly ConsoleDescriptor[];
  readonly realCarts?: readonly RealCart[];
}
```

`RealCart` documents one cart per platform and one per node, which the generated list refuses otherwise.

**What each console contributes** (the data moves out of the shell verbatim; the sources are the lines section 1 lists).

- `sameboy` (`packages/sameboy/ts/consoles.ts`, default-exporting `{ consoles }`): `sameboy.gb`, core `sameboy`, node `rp.sameboy.gb`; platform `gb` (`.gb`, `.gbc`);
  detect rank 2 on the Game Boy logo at 0x104 over a 0x134 prefix; `hasBattery` is the cart-type set at `$147`;
  `settingFields` from `SETTING_FIELDS["rp.sameboy.gb"]`, no construct fields, no params; four stereo streams
  (`ch1` to `ch4`); `capabilities { linkCable: { field: "linkGroupId" }, serialOut: true }`; a `channels` split with
  names `pulse1 pulse2 wave noise` and no patch or tap; `render { mixLanes: "stereo", defaultPlay: { button: 7 } }` (the Start
  button, `render.ts:44`).
- `mesen` (`packages/mesen/ts/consoles.ts`, default-exporting `{ consoles }`, which lists the GBA, then the NES, then the
  SMS/GG family, so that a platform's place in the registry, which `romExtensions()` and the sibling-ROM probe order
  follow, is today's `gb, gbc, gba, nes, sms, gg`; product order alone would give `nes` before `gba`): `mesen.nes` (platform `nes`, `.nes`, detect rank 0 on `NES\x1a`, `hasBattery`
  iNES flags6 bit 1, the NES setting fields, construct fields and the `expansionVolume` param, the three NES stream layouts,
  `capabilities { channelTap: true }`, splits `channels` (names `square1 square2 triangle noise dmc`, patch
  `{ channelExportMode: "individualMono" }`, tap mode 3) and `pins` (names `pulse tnd expansion`, patch
  `{ channelExportMode: "stereoModPins" }`, tap mode 1), `render { mixLanes: "left" }`); `mesen.gba` (`gba`, `.gba`, rank 1
  on the logo at 0x04, no fields, no battery rule); `mesen.sms` (`sms` `.sms` and `gg` `.gg` with `nodeSettings
  { gameGear: true }` and `extensionOnly`, rank 3 on `TMR SEGA` over a 0x8200 prefix with the region-nibble rule,
  `providerHeaderBytes: 0x8200`, construct field `enableFm`). `mesen.nes` also declares `exportModes { field:
  "channelExportMode", values: CHANNEL_EXPORT_VALUES }`: the parity baseline pins that a NES built with
  `channelExportMode: "pinsPlusRef"` renders its four pin streams, and that mode has no split, so its ordinal cannot come
  from a split's `tapMode` (without the member `exportModeOf` answered mix for it and `parity/render-shapes`'
  `perChannel-nes-pinsPlusRef` failed).
- `n8` (`packages/n8/ts/consoles.ts`, default-exporting `{ realCarts }`; its `app.ts` mentions none):
  `realCarts: [{ platform: "nes", cartNode: "rp.n8.nes" }]`. `drivenCartSystem` (buildGraph 149)
  becomes "the first system whose platform has a registered cart node and whose core config's `realCartOptOut` field is not
  `false`"; `cartNodeId` and the node type come from the registration. The opt-out field stays mesen's (decision 12): its
  NES platform declares `realCartOptOut: "driveRealCart"`, the field its role schema already holds (`mesen/ts/app.ts:92`) and
  saved projects already carry, and `drivesRealCartOf` becomes the descriptor lookup the store and the menu share. It keeps
its name and its one-argument form and gains an optional `platform` that `drivenCartSystem` passes; for a platform with no
registered cart it answers true (nothing to opt out of: the GBA's mesen config keeps the NES's `driveRealCart` field).

**The registry** is the shell's `src/consoles.ts` (beside `romProviders.ts`, `trackerRegistry.ts` and `kitFormats.ts`), over
a generated `CONSOLES` list and `REAL_CARTS` list in `src/generated/consoles.ts` (not `packages.ts`, see above), in product
order. `src/platform.ts` stays as the module the callers know, with the same names over lookups: `defaultCoreFor`,
`systemNodeTypeFor`, `detectPlatform`, `romHasBattery`, `ROM_SNIFF_LEN` and `SEGA_SNIFF_LEN` become derived values and
functions (the first and last distinct `prefixBytes`). `SystemNodeType` becomes `string`. The registry carries lookups the
design did not name, which the shell needs to stop naming a console: `consoleFor(nodeType)`, `consoleOfPlatform`,
`platformEntry` (extensions, `nodeSettings`, `realCartOptOut`), `platformCapabilities`, `registeredPlatforms` and
`registeredCores` (what `projectConfig` validates against), `registeredRealCarts` and `realCartOf`, `romExtensions` (what
`savPaths` and `fileSelection` read), `platformForExtension` (the render's fallback for an unreadable file),
`providerHeaderBytes` and `sniffLengths`. `systemNodeTypeFor` throws for an unregistered platform (every shipped path takes
a platform from the registry or drops an unknown one at load). The tables in `graph/buildGraph.ts` (`SETTING_FIELDS`, `CONFIG_PARAMS`,
`CONSTRUCT_FIELDS`, `streamsOf`) are read from the descriptor by node type; `exportModeOf(type, config)` and `linkGroupOf(type, config)` read
the system's descriptor by node type (`channelExportMode` stays a mesen config field, named by `exportModes`, and
`linkGroupId` by `linkCable`).
The capability tests become data: `s.type !== "rp.sameboy.gb"` at 379 and 502 is `capabilities.linkCable`, 485 is
`capabilities.serialOut`, `tapArmOf` (399) and `nesMode` are `capabilities.channelTap` and `splits[...].tapMode`, and the
`isPins(routing) && type !== "rp.mesen.nes"` test at 567 is "the console has a `pins` split".

**Classification.** `classifyRom` (`systemsStore.ts:128`) stays the one place ROM bytes enter TypeScript, reading the same
two prefixes in the same order as today. The registry sorts the distinct `prefixBytes` ascending, reads each once through
`readFilePrefix`, runs every detector with `prefixBytes <= that length` in `rank` order (the order is computed once at load, with the
descriptor id as the final tiebreak, and `ext` goes on the last tier only) and returns the first answer, which
`detectorAnswer` refuses if the descriptor does not list that platform (a guard the generated module cannot hold, since it
runs no detector); if none answers, the last tier's extension-only fallback claims the file for the one
`extensionOnly` platform the extension names, else `"unknown"`. So the NES, GBA and GB detectors all declare 0x134 (the
Nintendo tier: the file's first read is shared, as today, though the NES needs 4 bytes and the GBA 36) and Sega declares
0x8200: two reads, 0x134 then 0x8200, exactly as `classifyRom` does. The detector bodies move unchanged (the iNES magic,
the GBA logo, the GB logo, the three Sega offsets with the `+0x200` copier shift and the region nibble rule). The tests
that pin it stay (`test/paths/rom-format.test.ts`, 12 cases, `test/systems/battery.test.ts`).

**Validation and the generator.** `projectConfig.ts` replaces the two `z.enum`s with exported `platformSchema` and
`coreSchema`, each a `z.string().superRefine(...)` against the registered platforms and cores (a `superRefine` because the
message names the set read when the refinement runs), with the message `unknown platform "x" (known: gb, gba, nes, sms,
gg)` (the registration order, mesen listing its GBA first, above); an entry failing
the strict parse is dropped by `parseConfig` silently, as an entry with an unknown platform is today (`projectConfig.ts:363`
keeps each `safeParse` success and reports nothing), so the message reaches only a caller that parses an entry directly. The
refinement reads the registry when it runs, not when the module loads. The v1 to v2 migration step looks the core up in the registry
and writes nothing for a non-platform string, as `DEFAULT_CORE[...]` returning `undefined` does today. The generated modules
refuse, in `consoles.ts` (data only): a platform id registered twice, a descriptor whose `nodeType` two descriptors share, a
`RealCart` naming a platform no descriptor registers, and two carts naming one platform or one cart node; and in
`packages.ts` (the one check that needs the app): a console descriptor whose `core` has no registered `system`-category role
(the role is named after its core: `systemRoleFor`, `core/ts/systemRoles.ts`). The refusals live in the generated modules,
not in the generator script, because a role's category is TypeScript data the generator never evaluates; the fixtures load
the generated module, as the existing duplicate checks do. `render/types.ts`'s own `Platform` union is deleted.

**Files that change:** `packages/core/ts/{platform,systemRoles}.ts` and the new `consoles.ts` (`definePackage.ts` changes
only in stage 4, for the embed); new `packages/sameboy/ts/consoles.ts`, `packages/mesen/ts/consoles.ts` and
`packages/n8/ts/consoles.ts`, each named by a `retroplug.consoles` member and a `./consoles` export in its manifest;
`packages/native/cmake/bundles.cmake` (the byproduct list gained `consoles.ts`);
`src/platform.ts`, the new `src/consoles.ts`, `src/projectConfig.ts`, `src/systemsStore.ts`, `src/graph/{buildGraph,
systemResources,directGraph}.ts`, `src/savPaths.ts:77`, `src/fileSelection.ts:20`, `src/render/types.ts`;
`scripts/lib/packages.mjs` and the regenerated `src/generated/{consoles,packages}.ts`.

## 4. The cart hooks the stores reach through

### 4.1 Tracker `render`

`packages/tracker/ts/trackerIntegration.ts` gains an optional `render?: TrackerRender`, implemented by lsdj, risa and
smsggdj (BlipToaster has no songs and none of this). As built the contract is its own module, `trackerRender.ts`
(published at `./trackerRender`), and each member's contract is a module of tracker's, the cart's implementation beside the
cart (`lsdj/ts/lsdjRender.ts`, `risa/ts/risaRender.ts`, and `SMSGGDJ_RENDER` in `smsggdj/ts/smsggdjIntegration.ts`, whose
`liveLoad` became the named `smsggdjLiveLoad`). The interface below is the design's; the as-built members it did not have
follow it.

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

**As built, `TrackerRender` has these members the snippet does not:**

- `playingProbe(host, id, rom, facts, warn)` takes the request's `RenderFacts` (`rom`, `sav`, `state`, `song`,
  `durationMs`, `maxDurationMs`) before `warn`, so a probe that answers null can quote the length the render falls back to
  (risa's `; rendering a fixed Nms`) and smsggdj's loops-forever warning fires where the old code fired it.
- `warnings?(facts, warn)`: what a render of this cart cannot do, raised once after the boot settle and before the song
  loads (smsggdj's blank-boot warning needs the battery and the "song selected / savestate" facts). The old texts and
  conditions are kept exactly; the render holds no cart-specific words.
- `songNameLength?: number`: LSDj's 8. The shared selection truncates a `--song` by it (an LSDj `--song MYSONGNAME` matches
  the project `MYSONGNA`); a cart without it compares the whole name.
- `songList?: { listing?(songs), empty }`: how a cart spells its songs where the neutral form will not do (LSDj's `0: NAME`
  and `(no named projects \u2014 only the working song)`, smsggdj's `0:NAME` and `(none)`), so the not-found message and
  `--list-songs` keep each cart's own words. Absent: `index: name` and `(no saved songs)`.
- `seedRefusal?(index)`: why a `seedSong` that answered null cannot promote `index`, in the cart's words (risa's `render:
  could not load risa song N (needs a current-layout catalog)`); the render throws it before the boot. Without it the
  render's sentence is `render: could not seed song N (Label) from <battery>`. Added because the first stage 2 dropped a
  null seed and rendered the wrong song silently, which the old code threw on.

`RenderHost` (`renderAudio`, `readCpu`, `readRam`, `writeRam`, `pressButton`; no `readFile`, since a hook is passed the
`rom` and `sav` bytes the host resolved) and `RenderPlaySink` (`renderChunk(ms)`, on the sink alone because a play gesture
must capture through the sink, which shapes the chunk as the request asked) are tracker's neutral types for what a hook
needs of the shell; the render's `RenderContext` satisfies them structurally, so a hook is testable without a host.
`PLAY_HOLD_MS` (100, tracker's one value, replacing the render's `DETECT_CHUNK_MS` and the gestures' own) is the hold a
press needs and the default pull length. `selection by name` stays in `render.ts`
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

Tracker owns the neutral types (`ControllerAnchor { rows: (number | null)[]; seq: number }` and the row-tick table,
`[channel][row]`; `playbackModel.ts` is already tracker's and says the seam is meant for risa too); `lsdj/ts/playback/
{predict,anchor}.ts` import them from tracker and re-export them. As built the contract is `trackerPlayback.ts` (with
`PlaybackAnchor` for the `anchor` member and `PlaybackReader`), beside `trackerSongImport.ts` and `trackerSramSignature.ts`,
and LSDj's implementation is `lsdj/ts/lsdjPlayback.ts` (`LSDJ_PLAYBACK`).

**The capture mapping is manifest data, not a playback function.** The sync-mode to drive and listen mapping
`graph/controller.ts` held (`serialOutCaptureFor` compared `LsdjSyncMode.MidiOut` and `MasterSync`) is `rp.lsdj.sync`'s
manifest entry in `packages/lsdj/package.json`, `"capture": { "midiOut": "drive", "masterSync": "listen" }`, which
`scripts/lib/packages.mjs` emits into `PROTOCOL_NODES` (`ProtocolNodeSpec.capture`, typed `SerialOutCaptureMode`) and
`controller.ts` maps the (possibly forced) mode through (own-key test by `Object.prototype.hasOwnProperty.call`, since the
shell's lib target refuses `Object.hasOwn`). The design's `playback.captureFor(mode)` would have had the planner resolve a
cart's integration, which pulls `generated/packages.ts` and every app module into the planner's bundle, and F1's rule (held
by `test/script/bundle-protocol.test.ts`) is that bundle carries no package; the manifest is where the cart already speaks
to the planner. Both halves of the intent hold (lsdj owns the mapping, `controller.ts` imports no cart). The generator
refuses a `capture` that is not an object, a value that is none of `off`, `listen`, `drive`, a capture on a node that is not
wired (an unwired node is dropped from `PROTOCOL_NODES` whole, so the table would be dead data), on a node with no `out`,
and on a node whose `out` no package declares or declares with no `serialOut`; each with its own sentence and fixture, and
the emitted table is spelled `{ "k": "v", ... }` like its neighbours. `projectStore` asks `resolveTracker(roles)?.playback`: `controllerReader` (518) replaces the `"lsdj-sync"`
literal by the integration lookup, `refreshControllerSong` (476) and `controllerModel` (192) call `playback.syncMode`,
`songSignature` and `rowTicks`. The poll timer, `controllerSongSig`, `controllerSync`, `controllerAnchor` and the publish
stay in the store (the reader is built once per ROM and cached by ROM path; `controllerModel` reads the battery
only when the system's tracker has playback). **A system with no `playback` member reads "no sync, no signature, no table,
no reader"**, which section 6 checks against what LSDj's functions return for a non-LSDj battery today (it matches for sync
mode and for a 128 KiB image, differs for two narrower cases, and, because the gate is the ROM's roles, reads nothing for a
generic ROM beside an LSDj battery: edge E3 as widened).

### 4.3 `sramSignature`

`TrackerIntegration.sramSignature?(bytes: Uint8Array): number | null` (the type is `trackerSramSignature.ts`'s
`SramSignature`) is LSDj's `lsdjSramSignature` moved from `sramAutoSave.ts:39` into `lsdj/ts/lsdjSramSignature.ts` (with its
`decodeSong` / `encodeSong` import, `sramAutoSave.ts:15`); risa has none, being a battery with no signature. The shell's
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
it. Recognition of a dropped song file is the product's union of every integration's `songImport.extensions`, so a song file
dropped where nothing imports it is refused with the new reason rather than as an unsupported file. The refusal reason
`"song files need an LSDj instance"` becomes `"song files need a tracker that imports them"` (section 6). **As built the
hook takes a host adapter:** `songImport.import(host: SongImportHost, sys: SongImportTarget, paths)`, where
`SongImportHost` (tracker's `trackerSongImport.ts`) is `caps` (a construct hook's `ConstructCaps`: an import that carries
ROM assets patches the ROM through the same reads), `writeFileAtomic`, `readSram`, `loadSram` (the one reboot), `setRoleConfig`
and `savPathOf(id)` (the save file the shell pairs with a system, or null when the system went away, in which case the import
refuses rather than writing to an empty path), and `SongImportTarget` is `{ id, romPath, roles }`. The shell builds the
host in `src/songImportHost.ts` from the backend and the systems store, and lsdj's is `LSDJ_SONG_IMPORT` in
`lsdj/ts/lsdjSongImport.ts`. The two other call sites, `ui/App.tsx:47, 271` and `ui/screens/menu/menuDefs.ts:42, 1650`, call
`integration.songImport.import(...)`; both are F4b files, edited here for the signature only.

### 4.5 Role hooks

`core/ts/systemRoles.ts`'s `RoleType` gains three optional members (alongside `onConstruct`):

```ts
parameters?(config: Record<string, unknown>): readonly RomParameter[];
deriveConfig?: { headerBytes(short: Uint8Array): number; derive(header: Uint8Array, config: Record<string, unknown>): Record<string, unknown> };
hostLatencyMs?(config: Record<string, unknown>): number;
```

- **`parameters`.** `MGB_PARAMETERS` (`parameterMap.ts:316`) goes to mgb, the BlipToaster tables (`BLIPTOASTER_SPECS`, 256, six
  chips) to bliptoaster, each returning its expanded lanes (the unknown-chip fallback to the 2A03 core moves with the table); as built the tables are
  `mgb/ts/parameters.ts` and `bliptoaster/ts/parameters.ts` and the lanes are held to the lists the shell's tables expanded
  to (recorded before the move: mGB 24, BlipToaster 2a03 51, vrc6 73, vrc7 88, s5b 66, n163 71, mmc5 80).
  The CC-spec types and `expandRomSpec` (289) must move too, to a new `core/ts/romParameters.ts` (`RomParameter`, `CcSpec`,
  `RomSpec`, `expandRomSpec`), because a package may not import the shell and each cart builds its tables with them. `parameterMap.ts`
  keeps the slot pool, `preRoutingChannel` and `projectParameterMap`, which asks the registry (`stores.registry`, passed in by
  `pluginControlPlane`) for `roleType(kind)?.parameters?.(config)` instead of `parametersForRole`.
- **`deriveConfig`.** BlipToaster's chip scan (`withDerivedRoleFacts`, `systemsStore.ts:1506`) becomes the `bliptoaster`
  role's hook: `headerBytes(short)` is `needsChipLabelScan(short) ? BLIPTOASTER_CHIP_SCAN_LEN : short.length` (a function of the
  short header, because today the deep read of 0x4010 happens only for a ROM predating the chip tag, so "the deep read happens
  only when a role asks"), and `derive` is the `blipToasterChip` write. The store reads the short header, asks every
  role's `deriveConfig.headerBytes(short)`, reads the largest once, and applies each `derive`. As built this is one shared module, `packages/retroplug/src/rolesForRom.ts`
  (`rolesForRom`, `derivedRoleConfigs`): it reads the provider prefix (the console's `providerHeaderBytes`, else
  `ROLE_HEADER_LEN`, which lives in core's `systemRoles.ts` beside `RoleDerive` so a role's contract can say what the short
  header is), runs the registry's default roles, then asks each deriving role, and BOTH `SystemsStore.defaultRoles` and the
  render's `resolveRenderRom` call it, which replaced `readRoleHeader`, the render's own copy of the read and a duplicated
  length constant. `headerBytes` answers at least the short length (`Math.max`). **A file the backend cannot read derives
  nothing:** `derivedRoleConfigs` returns the roles unchanged when the read is empty, before asking `headerBytes`, so a stored
  `chip` is not overwritten with the 2A03 default (the guard the first stage 4 lost, found in review; `RoleDerive`'s doc
  says `derive` is called only with a readable file's bytes). The `platform "nes"` it passes
  today (`:1513`) is the role's knowledge now. **It runs on both paths (decision 11):** on a fresh build right after the
  providers (`defaultRoles`, `systemsStore.ts:1354`), and on a loaded project where `withMissingFeatureRoles` (1489) runs it
  now. The providers therefore see the short 0x150 header (Sega: the console's `providerHeaderBytes`), which is enough for
  BlipToaster's provider to recognise the cart (`isBlipToasterRomHeader` reads the SIG marker inside it); the provider
  writes no `chip`, and `readRoleHeader`'s NES branch (83 to 90) goes. An untagged S5B ROM gets `s5b` in the same construct
  that builds it, as today, so nothing visible changes, and `chip` has one writer.
- **`hostLatencyMs`.** `lsdj-sync` returns 33 for `MidiSync` and `MidiSyncArduinoboy` and 0 otherwise; the 33 and the
  `LsdjSyncMode` import leave `pluginControlPlane.ts:19, 99 to 104`. `__rp_syncLatencyMs` sums over the systems'
  PLACED roles (`protocolPipeline`, so an `lsdj-sync` the pipeline leaves out on an mGB cart still contributes nothing) as the
  maximum (a hook's doc says only a placed role counts), asking `registry.roleType(kind)?.hostLatencyMs?.(config)`.

### 4.6 Link groups and `savFromJson`

- **Link groups.** `inheritLinkGroup` (`systemsStore.ts:1053`) finds each system's core-config role by the existing rule that
  a core's config role is named after the core (`e.roles.find(r => r.kind === e.core)`, as `graphSystems` already does at 264),
  and the "BOTH are SameBoy" guard is the descriptor's `capabilities.linkCable` of the system's platform; the config field it
  writes is `linkCable.field`. No `"sameboy"` literal remains in the store. As built the guard compares the two systems'
  cable FIELDS (both must declare a `linkCable` and name the same field), since a parent's field name written into a child
  whose core reads another is a number neither core reads; no pair in the tree differs, `sameboy.gb`'s `linkGroupId` being
  the one cable. The group value is read as a number (a stored non-number counts as ungrouped).
- **`savFromJson` leaves the contract.** It leaves `ConstructCaps` (`core/ts/systemRoles.ts:75`), `Backend` (`backend.ts:470`),
  `HostBackend`'s pick (`:486`) and `realBackend.ts:94`; lsdj's construct hook (`lsdj/ts/app.ts:26`) calls its own
  `savFromJson` (`@retroplug/lsdj` already exports it; the hook calls `savFromJson("{}")`), and `mockBackend` loses it too, so
  the shell has no sav encoder. Three tests count the backend's `savFromJson` log entry and are
  rewritten to assert the seeded `sramBytes` instead (`test/systems/lsdj-seed.test.ts:33, 46`,
  `test/systems/store-mutate.test.ts:161, 169`, `test-native/app-lsdj-seed.test.ts`), which is what they exist to prove.

### 4.7 The four doors, and where the tests go

- `src/kit/compile.ts` is deleted; `src/kit/index.ts` goes with it (its other re-exports are `NO_SLOT_REASON` and `KitSpecError`,
  which the three cli sessions and the tests import from `@retroplug/dsp` directly). the two compilers and the two format
  constants are imported from `@retroplug/lsdj/kitFormat` and `@retroplug/risa/kitFormat` by the tests that use them. As
  built the compilers are renamed `compileLsdjKit` and `compileRisaKit` (the old `compileKit` / `compileDmc` names could not
  survive the stage's empty grep, section 8 stage 4's exit);
  `kitFormats.ts` imports `KitFormat` from `@retroplug/dsp/kitContract`.
- `src/tracker/index.ts` is deleted; `liveSav.ts` stays and its importers (`projectStore.ts:22`, the menu, three tests) import
  `./tracker/liveSav`; the catalogs are imported from their packages by the tests that use them.
- `trackerRegistry.ts` keeps `TRACKER_INTEGRATIONS` and the three resolvers; the four re-exports go (`render.ts` stops using
  `smsggdjIntegration`, and the seven tests import it from `@retroplug/smsggdj/smsggdjIntegration`).
- `audioDriver.ts` keeps the `AudioDriver` facade and its own types; its re-exports (53 to 61) go. A test that wanted the kit
  spec types imports them from the owning package (`@retroplug/dsp/kitContract`, `@retroplug/dsp/kitEffect`,
  `@retroplug/lsdj/kitFormat`, `@retroplug/risa/kitFormat`).
- **Tests follow their code.** A test of code that moved goes to the owner's `ts/test` (its slug becomes `<package>/<path>`):
  the `parameterMap` cases that pin the mGB and BlipToaster tables go to mgb and bliptoaster (the projection cases stay), the
  `lsdjSramSignature` cases to lsdj, the song-import cases to lsdj (with one end-to-end case kept in the shell, below), the
  render hooks' cases to their carts. As built the moves are `test/bliptoaster/romDetect` to `bliptoaster/romDetect` and
  `bliptoaster/rolesForRom`, `test/render/render-hooks` to `lsdj/`, `risa/` and `smsggdj/renderHooks`,
  `lsdj-song-import` / `lsdj-lsdprj-safety` to `lsdj/songImport` (over a fake `SongImportHost`) and `lsdj/lsdprjImport`,
  `lsdj-song-safety` to `lsdj/songSafety`, `songRefresh`'s signature cases to `lsdj/workingSongSignature`, and
  `role-parameters`' tables to `mgb/parameters` and `bliptoaster/parameters`. A moved test may import only what its package
  may, which is why the importer case runs over a fake host; the shell keeps `systems/lsdj-song-import`, one case driving the
  real store and `MockBackend` through `src/songImportHost.ts` into the importer (the only test of the adapter). Shell tests that
  drive carts end to end (`test-native/*`, the menu tests that call `menuDefs`) stay. The skip baseline records nothing for
  the ts tier, so a move changes no entry.

## 5. Render, and the embed

### 5.1 Render

`render.ts` keeps its pipeline (settle, lead-in, sink, end detection, the part files and the cancel points) and loses every
platform switch. It reads the ROM's header through `ctx.backend.readFilePrefix`, classifies it with the registry
(`classifyRom`'s prefixes), takes the roles `registry.defaultRoles(core, platform, header, "")` gives (the same roles the
built system will get, because the store builds them the same way), and `resolveTracker(roles)` the integration. As built
the read is `rolesForRom` (section 4.5), shared with the store, and the result is the exported `resolveRenderRom` (`platform`,
`core`, `roles`, `integration`), which `--list-songs` reads too (`renderSongs`) so the CLI boots nothing to list. A ROM
nothing classifies resolves to `unknown` with no roles and no integration, and the render goes on with no gesture, no probe
and a stereo mix until the store's own classify refuses the build (`render: could not load ROM: <path>`). The
`RenderContext` gains the registry (the CLI session and the worker both hold one: `bootSession.ts:20`, `render/worker.ts:73`).

- **Platform.** `platformOf(rom)` (by extension today) becomes the classification; the extension is kept as the descriptor's
  `extensions` for the fallback when the file cannot be read (section 6, edge E1).
- **Songs.** `--song` / `--song-index` use `integration.render.seedSong` (LSDj, risa: the SRAM the system boots from) or
  `loadSong` (smsggdj: after the boot settle, as now); `--list-songs` in `cli/sessions/render.ts:23` calls `render.listSongs`
  (through `renderSongs`). A ROM whose integration has neither refuses the flag, naming the carts that offer it (below). A
  `seedSong` that answers null refuses before the boot in the cart's words (`seedRefusal`), and a `loadSong` that cannot
  re-read the ROM or battery throws the old smsggdj sentence. The shared selection logs `song "NAME" (slot N)` followed by
  `→ working song` when `seedSong` supplied the seed and `→ work RAM` when `loadSong` will run, as before.
- **Play gesture.** The integration's `pressPlay` (risa's SELECT+START with its two discarded prep chunks, smsggdj's Pause
  with its "already playing?" check) else the descriptor's `render.defaultPlay` (Game Boy: Start, which LSDj and mGB rely on),
  else none. Generic NES and SMS ROMs keep none, because they match no integration and their descriptor declares none.
- **End detection.** The integration's `playingProbe` (LSDj's NR52 poll, risa's sequencer flag; smsggdj and BlipToaster have
  none, so a fixed length, and the smsggdj "loops forever" warning stays in its hook). LSDj's probe is chosen today when the
  SAV is LSDj's and is chosen under the new rule when the ROM is: edge E2.
- **Split.** `--split pins|channels` is validated against the descriptor's `splits`; `rolesForRender`'s injected `"mesen"`
  patch (`render.ts:392`) becomes the split's `configPatch` applied to the system's core-config role (`kind === core`), and
  the file names come from `ConsoleSplit.names` and the mono/stereo lane choice from `render.mixLanes` (the summary lines
  `GB 4-channel render (...)` and `NES channels (5 mono streams ...)` are rebuilt from the lane shape and the upper-cased
  platform id). The two refusals are byte-identical to the old ones: `--split pins is NES-only (got gb)` and `--split
  channels needs a Game Boy or NES ROM (got gba)`, built from the labels of the consoles that declare the split, with `got
  other` for a ROM nothing classifies (a first version said `got unknown`; the reviewer held byte identity). The `GB_CHANNELS`,
  `NES_PINS` and `NES_CHANNELS` constants (62 to 64) and `NES_START`, `GB_START`, `SMS_START` (44 to 47) leave the shell.
- **Error text** naming carts is built from the registered integrations in product order: where `render.ts` wrote `a Game
  Boy (LSDj) / NES (risa) / Master System + Game Gear (smsggdj) feature (got <platform>)` it now writes `render:
  --song/--song-index is a feature of LSDj, SMSGGDJ, Risa (this ROM runs no tracker: its console is Game Boy)` (the
  integrations' own labels in product order; the association of a cart with a platform is a ROM provider's decision at
  build time, so the shell cannot derive a platform label for a cart without booting one, and the design's `label
  (platform label)` form was not built), with the same words for `--list-songs`. The words change deliberately; section 6
  lists the tests that read them. Every other moved message, log line and gate is the old text byte for byte, a U+2014 in
  an output string being spelled as the escape `\u2014` so the output is unchanged and the source holds none.

### 5.2 The embed

mgb's TS declares the embedded synth, and it is checked against the manifest:

```ts
// core: PackageContributions.embeds?: readonly Embed[]   (Embed = { id: string; label: string; platform: Platform })
// packages/mgb/ts/app.ts
export default definePackage({ roles: [MGB_ROLE], romProviders: [MGB_ROM_PROVIDER],
                               embeds: [{ id: "mgb", label: "mGB (GB MIDI Synth)", platform: "gb" }] });
```

`scripts/lib/packages.mjs` gains the reads (it read no manifest `embed` before; only `cmake/packages.cmake:31 to 39` did):
a malformed manifest entry and a manifest id listed twice are refused by the generator, and it emits `EMBEDS` (the app
modules' declarations) and `MANIFEST_EMBEDS` (the manifests' ids). **The pairing is refused by the generated
`packages.ts`, not by the generator script**, because the generator never evaluates a package's TypeScript and so cannot see
an `embeds` declaration (the same shape as the controller apps' two halves): a manifest entry no app module declares, a
declaration no manifest bakes in, the two halves in different packages, an id declared by two packages or twice by one, and
a platform no console registers each refuse, naming the package(s), with fixtures for every one (RED first). The shell's
`src/embeds.ts` (`embeddedRoms`, `embeddedRomOf`) reads the list, and `SystemsStore.loadEmbedded(id)` takes the platform
from the declaration by id (`construct` looks it up; `adopt` drops a stored entry whose id this product does not declare)
instead of `systemsStore.ts:1161, 1303`'s `platform = "gb"`. `projectConfig`'s `embeddedRom` is unchanged on disk
(`"mgb"`). The start menu's rows are generated from `embeddedRoms()` (`Load ${label}`, action id `start-${id}`), identical
today (`Load mGB (GB MIDI Synth)`, `start-mgb`); the infoRow `"mGB"` at `menuDefs.ts:2470` still spells the short name and
F4b moves the row into the mgb package, where the short name belongs.

### 5.3 The cart-named API

`SystemsStore.loadMgb()` (371) becomes `loadEmbedded(id: string): number | null` (refuses an id no package declares, returning
null as `loadInPlace` does for a failed load). `AudioDriver.compileKit(spec)` / `compileDmc(spec)` (`audioDriver.ts:295, 296`)
become `compileKitFormat<S>(id: string, spec: S): Promise<KitCompileResult>` (`async`, so an id no package registers
rejects rather than throwing synchronously; no product caller passes one), dispatching through `kitFormat(id)` as the two bodies
already do; the specs are typed by the owner package at the call (`compileKitFormat<KitCompileSpec>("lsdj", spec)`), so the
driver names neither cart. `cli/sdk-types.d.ts:391` loses `loadMgb` and gains `loadEmbedded`; the BlipToaster consumer calls
none of the three (a grep of `/workspaces/bliptoaster`, not edited). Every caller in the repo is updated, 33 files for `loadMgb`
(section 1), including the Node addon's `emu-ops.mjs` and README, and the specs and docs that name it
(`spec/03-ts-layer.md:133, 216`, `spec/09-cli-debugging.md:65`, `spec/07-remaining-work.md:829`).

## 6. Behaviour edges

Each was checked against the code; none moves a baseline. Edges E1 to E4 are corrected below to what was built (E3 and E4 widened, and E7 is new).

- **E1. Render classifies by content, not extension.** `platformOf` reads the extension only (`render.ts:66`); the store
  classifies by content, and a render with a mislabelled ROM (a `.nes` holding a Game Boy ROM, a `.bin` GB ROM) passes
  the first and builds with the second. The new rule has one answer (the content's), so a mislabelled file renders with
  the right options instead of the wrong ones, and a `.bin` Game Boy ROM, which today fails `--split channels` with `got other`,
  now accepts it (the split refusals keep their old words, `got other` for a ROM nothing classifies, a first version having
  said `got unknown`). A file that cannot be read classifies by extension (the descriptor's `extensions`), so `render-cli.test.ts:114 to 119`
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
  (`currentSongName`, 430) follows the same rule through `integration.songs.workingName(raw)`, which each catalog guards itself
  (LSDj's on the `jk` magic, so a non-LSDj battery names no song). The prototype's cases pin it:
  `test/render/render-classify.test.ts` holds an LSDj ROM with no battery auto-detecting to an HFF stop, a non-LSDj Game Boy
  ROM beside an LSDj battery running the full cap and named after its own ROM, and a real LSDj cart beside its own battery
  keeping the working-song name; the first stage's E2 case passed on the old code and was rebuilt with the full cap asserted.
- **E3. A system with no `playback` reads "no sync, no signature".** For a non-LSDj battery: `savSyncMode` returns null
  (`isLsdjSav` fails, `fromSav.ts:50`), as an absent member does; `songRowTicksFromSav` throws inside `decodeSav` for a 128 KiB
  image with no `jk` magic (`codec/sav.ts:77`) and returns null, so `controllerModel`'s `songRowTicks: table ?? []` is the
  same `[]`; `controllerReader` is gated on `lsdj-sync` so it is null. **Two cases differ**: `songRowTicksFromSav` on a battery
  of 32 KiB up to 128 KiB (a 64 KiB risa battery is one) is not null today (`decodeSav` reads it as an early-LSDj image and
  `songRowTicks` runs over bytes that are not a song), where the absent member gives `[]`; and `workingSongSignature` hashes any
  image of at least `kSongByteCount` bytes (`fromSav.ts:62`) and so is non-zero and moves for a non-LSDj battery, where the
  absent member gives 0 and never fires `publishGraph` + `notifySystemsChange` (the re-publish is deduped by the graph, the
  notification is not). Both are the controller polling a system that is not an LSDj cart, which MI.MAP does not drive; the
  change is a narrowing to what was intended. **As built the narrowing is wider than the two cases:** the gate is the ROM's
  roles, not its battery, so a generic Game Boy ROM beside an LSDj battery now reads none of the four (it read all four
  before), recorded in `trackerPlayback.ts`'s header. A store-level case pins it
  (`test/tracker/playback-absent.test.ts`: a `gbRom()` with a 64 KiB risa battery and the controller enabled does not
  refresh, publishes nothing, notifies nothing across an edit, reads no sync and publishes an empty timing table; RED on the
  design commit's `projectStore.ts`), beside the four cases that pin the equal and differing function answers.
  **Two test files reach it:** `test/controller/songRefresh.test.ts`
  builds its system from the generic `gbRom()` fixture (no LSDj title, so no `lsdj-sync` role) and feeds it a blank bank;
  six of its cases (the signature and publish cases from line 99, and the anchor cases) rely on the signature moving for a
  system that is not an LSDj cart. Under the new rule they seed `lsdjRom()` (`test/systems/fixtures.ts:38`) instead, which is
  what they mean; the non-LSDj sync case at 178 keeps `gbRom()` and still reads null. Stage 3 changes the fixture in that file
  and nothing else in it, and its commit names this as a test-fixture change, not a behaviour change of a shipped combination.
  The other is the widening's: `test/menu/launchpad.test.ts`'s `withCart` fixture paired a generic Game Boy ROM with an LSDj
  battery, so its cart-sync row read `SYNC unread`; it seeds `lsdjRom()` too. Both were failures of the new rule first.
- **E4. Error text.** The words that name carts change (section 5.1). As built the sentences are `render:
  --song/--song-index is a feature of LSDj, SMSGGDJ, Risa (this ROM runs no tracker: its console is Game Boy)` and the same
  with `--list-songs` (the console word being the ROM's platform label), and `--list-songs`'s empty notice is the cart's
  (`songList.empty`) or `(no saved songs)`. Tests that read the old words: none contained the full sentence;
  `test-native/sms-render.test.ts:171` checks only the substring `--song/--song-index`, which the new text keeps (its
  `validateRenderOpts` calls gained the integration argument, and its GBA case resolves its hooks through
  `resolveRenderRom`), and `test/render/render-classify.test.ts` now pins the `--song` and `--list-songs` sentences. The
  two split refusals and every other moved message are unchanged (section 5.1). `fileDrop`'s `"song files need an LSDj
  instance"` becomes `"song files need a tracker that imports them"`, which `test/file-drop/route.test.ts` reads (its
  song-file cases build a `DropContext` with `targetSongExtensions` in place of `targetIsLsdj`, and the non-LSDj case expects
  the new reason).
- **E5. Detection order.** The brief's statement that the four content signatures are disjoint is false for crafted files:
  `test/paths/rom-format.test.ts:76` pins that a NES magic beats a GB logo and a GBA logo beats a GB logo. In product order the
  Game Boy descriptor comes first (`sameboy` is before `mesen`), so product order alone would flip both. The descriptor carries
  `rank` (section 3) so the order is data and the test stays. The user chose it (decision 10).
- **E6. `Platform` as `string` loses compile-time exhaustiveness.** A `switch` on a platform no longer fails to compile
  for a missing arm. The remaining platform switches are all being removed here; the literal scanner (section 7.2) is what
  keeps one from coming back.
- **E7. A generic ROM beside a tracker battery refuses `--song` and `--list-songs` (new, accepted).** The render classifies
  the ROM through its roles and a tracker's hooks are reached through `resolveTracker(roles)`, so a generic Game Boy, NES or
  SMS ROM beside an LSDj or risa battery has no integration and the song flags are refused, where the old platform switch
  accepted them by the ROM's console alone. It is the same consequence of decisions 4 and 5 as E2 and the widened E3; the
  sentence names what is missing (`this ROM runs no tracker`) and `test/render/render-classify.test.ts` pins both flags on a
  `gbRom()` beside an LSDj battery. A malformed full-size LSDj battery is still refused in the codec's words
  (`missing 'jk' SRAM init magic`), because the cart's `listSongs` decodes it as every render path always did.

## 7. Proof

### 7.1 The shell-source boundary rule

`check:boundaries` ([check-boundaries.mjs](../packages/retroplug/scripts/check-boundaries.mjs)) judges packages only today (header lines
13 to 15: `retroplug` and `native` are the shells and "are not judged"). F4a adds rule 9: **every `@retroplug/*` specifier in
`packages/retroplug/src` must match an explicit allowlist, each entry with its reason**; a specifier naming a cart package
(lsdj, risa, smsggdj, bliptoaster, mgb, lsdj-launchpad, n8) or a console package (sameboy, mesen) outside the list is refused.
**As built the rule also refuses a relative import that lands outside `packages/retroplug/src`**: rules 1 to 8 do not judge
the shell's `src`, so a relative reach into `packages/<name>/ts` (or into `ui/`, `cli/` or a test tree) is the one way left to
reach a cart without a specifier, and the review found it; the half is lexical (a `tsconfig` `paths` entry or a symlink is not
seen), judges every relative import out of `src` rather than only ones into a package, and takes an in-`src` relative import
(pinned by the allowed fixture). The
list is a constant in the script (`SHELL_IMPORTS`), an entry no import uses fails the check (as `EXCEPTIONS` does),
`src/generated/` stays unjudged (the one place that names every package, as the header says), tests under
`packages/retroplug/test*` are outside `src` and unjudged, and `ui/` and `cli/` join in F4b. **The allowlist as built has 24
entries, derived from the tree** (not the design's table: the tree no longer imports bare `@retroplug/core`,
`core/defineNode`, `launchpad/apps` or `launchpad/nodes`, and imports `dsp/kitContract` where the design listed `kitJobs`,
`kitEffect` and bare `dsp`), each with its reason in the script:

| Specifier | Why it stays |
| --- | --- |
| `@retroplug/core/{canonicalJson, configFields, consoles, constructSpec, definePackage, descriptor, graphDocument, midiRouting, offlineJob, platform, romParameters, systemRoles}` (12) | The contract every package implements; the shell is its host (the document encoder, the schema helpers, the console descriptor contract, the construct spec, the contribution contract, the node descriptor, the graph document, the MIDI routing model, the offline job types, the open platform and core ids, the ROM parameter lane, the role contract) |
| `@retroplug/tracker/{assetCatalog, packageContributions, songCatalog, trackerIntegration, trackerPlayback, trackerRender, trackerSongImport, workingSongBlob}` (8) | The tracker contract: the catalogs, the integration shape and the hooks the shell's pollers, renderer and drop path ask, and the neutral working-song image the store carries |
| `@retroplug/dsp/{kitContract, packageContributions}` (2) | The kit compile driver's contract and the contribution shape an app declares its kit formats through; dsp is a library, not a cart |
| `@retroplug/launchpad/{registry, enums}` (2) | The control surface's registry (`controller/registry.ts`) and the controller target the project settings store persists (`projectConfig.ts`) |


What leaves the list by the moves (each is a line section 1 counted): every import of `lsdj`, `risa`, `smsggdj`, `bliptoaster`
(except through the contract above), `@retroplug/sameboy/enums` and `@retroplug/mesen/enums` in `buildGraph.ts` and
`projectConfig.ts`, `@retroplug/lsdj/enums` in `controller.ts`, `pluginControlPlane.ts` and `projectConfig.ts`. **The v2 to v3 migration (decision 13)** read `MODEL_VALUES`,
`HIGHPASS_VALUES`, `REGION_VALUES`, `CHANNEL_EXPORT_VALUES` and `LSDJ_MODE_VALUES` (`projectConfig.ts:21 to 27`) for
`V3_ROLE_ENUMS` (186 to 190); the five index-ordered lists are frozen as literals beside the step (now an exported
`V3_ROLE_ENUMS`, so a test can read the frozen lists and not a copy), the sameboy, mesen and lsdj imports go, and the shell
imports no sameboy, mesen or lsdj module at all. `test/project/migrate-v3-frozen.test.ts` (10 cases) iterates the FROZEN
lists, not the packages' current ones: every frozen value must still be one the owning package accepts, so a rename in a
package fails there (`expected [], got ["dmgB"]` with `dmgB` renamed), and every ordinal through `parseConfig` must map to
the value the v2 writer had at that index, so an append or a reorder in a package passes (decision 13's point). The first
version of the test iterated the current lists and could not fail on a reorder; the whole-prototype review found it.

**RED first.** The rule was added before any move and run on the stage 4 tree (a full `git archive` of it, with no fixture
noise): **21 shell-source refusals over six files** (`audioDriver.ts` 3, `kit/compile.ts` 3, `kit/index.ts` 1,
`projectConfig.ts` 3, `tracker/index.ts` 7, `trackerRegistry.ts` 4), exit 1, which is what the stage 5 moves emptied. (The
design's "47 import lines of section 1" is the count at `b88e81756` before stages 1 to 4 moved most of them.) The relative
half's RED is its own fixture: the old checker over `shell-source-relative` refuses nothing and the new one refuses both its
imports. Three fixtures in `test/boundaries` hold the rule the way the existing fixtures hold rules 1 to 8:
`shell-source-forbidden` (a cart import refused), `shell-source-allowed` (a contract import and an in-`src` relative import
accepted beside a generated cart import) and `shell-source-relative` (two refusals, `../../alpha/ts/app` and
`../../ui/App`).

### 7.2 The literal scanner (the "grep test")

**A test that `src/` holds no platform or ROM-extension literal outside comments is reliable and is stage 5's; as built it
walks the syntax tree, with its sets read from the registry.** `test/source-literals/no-cart-literals.test.ts` runs
`run-scan.mjs`, which parses every file of `packages/retroplug/src` (not `src/generated/`) with `ts.createSourceFile` from
the same pinned `typescript-5` the boundary check loads (a `.tsx` file as TSX) and collects every `StringLiteral`, every
`NoSubstitutionTemplateLiteral` and every template's head and span text. Comments are trivia, and neither a regular
expression literal nor a template's `${...}` can move the walk's place. **The first version drove `ts.createScanner` and
lost its place after a template or a regular expression, finding 46 of the 86 literals the base held** (the Critical finding
of the whole-prototype review); the design's own sentence, "reads tokens with TypeScript's scanner", was the mistake, and a
bare scanner does not track template nesting or regex-versus-division. The sets are not in the script: the test imports
`CONSOLES` from `src/generated/consoles.ts` and passes every descriptor's platform ids (`gb, gba, gg, nes, sms`) and
extensions (`.gb, .gba, .gbc, .gg, .nes, .sms`) as `--platforms` and `--extensions`; the script keeps no copy and exits 2
without them. A hit is a platform id, a descriptor extension, or `*` plus an extension (the `*.gb` glob). A console a
product registers later is therefore guarded with no edit to the test, and `.gbc` is guarded (a hard-coded list omitted it).
What it cannot see is a computed platform (`["g", "b"].join("")`) or a platform id spelled in a string that is not a bare
literal: it guards accident, not a determined author, as the boundary check says of itself. **RED on `96d748e5c`'s `src`:
90 literals in 8 files** (`fileSelection.ts` 12, `buildGraph.ts` 2, `platform.ts` 12, `projectConfig.ts` 5, `render/render.ts`
41, `render/types.ts` 5, `savPaths.ts` 6, `systemsStore.ts` 7); the design's 46 in 6 files was a different predicate (platform
ids only, outside comments) that neither walk reproduces, and the 86 of the reviewer's independent scan differ from 90 by
exactly the four `.gbc` literals its hard-coded set omitted. A fixture holds six literals (a bare one, a `.nes`, a `*.gba`
glob, a template span, a literal after a substitution template and one after a regular expression) and a comment and a
generated module it must skip; the old scanner found 3 of the 6. The real tree prints `{"literals":[]}`.

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
- BlipToaster's suite and NESVJ's against the rebuilt CLI (neither calls `loadMgb`, `compileKit` or `compileDmc`; 57/57 and
  19/19 at the tip), and
  `reaper:params` / `reaper:params-clap` (the parameter map moved behind `parameters`, and this is the only proof a host acts
  on the re-read flag).

### 7.4 New tests, by stage

The list below is what was added; the design's plan is kept where the build followed it, and each item says where it did
not. Every new test that pins a behaviour change was shown RED first (on the pre-change code or a temporarily restored old
file), and the reports hold the logs.

1. **Consoles.** `test/consoles/registry.test.ts` (with `cases.ts` and `recorded.ts`): the recorded tables are the OLD code's,
   produced by bundling `96d748e5c`'s `platform.ts` and `buildGraph.ts` over the case list before the first source edit
   (97 classification rows, 31 battery rows, 858 knob rows over four node types, 16 stream rows); the registry is held to them
   through its own seams (`classifyRom` with a counting `readFilePrefix` asserting the answer and the reads, one 0x134 read
   when the cheap tier answers and `[0x134, 0x8200]` otherwise; `systemKnobs` and `droppedKnobs`; `buildGraph`'s per-channel
   shape with `exportModeOf`), the polyglot priorities (E5), the extension-only fallback, every split's `tapMode` held to the
   index of the label its `configPatch` names, and `detectorAnswer` refusing an answer the descriptor does not list; seven
   generator fixtures in `test/packages/packages-generator.test.ts` (a duplicate platform, a duplicate node type, a core with no
   system role, a real cart naming no platform, two carts on one platform, two carts on one node, and the `consoles` module
   itself), RED first; `projectConfig` refusing an unknown platform and core with the known set in the message
   (`platformSchema` / `coreSchema`); a `build-graph.test.ts` case for a platform with no registered cart; and
   `test/script/bundle-protocol.test.ts` held unchanged. One C++ oracle changed (section 11): `SystemNode.test.cpp`'s
   `[settings]` readers read the descriptors now.
2. **Render.** The existing render suites unchanged but for the sentences section 6 lists; `test/render/render-classify.test.ts`
   (10 cases: E1, E2 as full-cap, the cart's refusal for a null seed, LSDj's 8-character `--song`, the split refusals' bytes,
   the E4 sentences, E7 and a malformed battery's throw) and the hook cases over a fake `RenderHost`, written as
   `test/render/render-hooks.test.ts` and moved in stage 5 to `lsdj/renderHooks`, `risa/renderHooks` and
   `smsggdj/renderHooks`.
3. **Playback and tracker hooks.** `test/tracker/playback-absent.test.ts` (6 cases: E3's equal and differing answers and the
   store-level case that is E3 as widened); the controller poll tests over `resolveTracker(...).playback`; `sramSignature` through the
   registry in `test/sram/auto-save.test.ts` (an LSDj image, a short non-LSDj battery, a 128 KiB image with no magic) and the
   moved `lsdjSramSignature` cases as `lsdj/sramSignature`; the generator's `capture` refusals (not an object, a bad value, an
   unwired node, no `out`, an undeclared `out`, an `out` that decodes nothing, and the emitted form), RED first; `fileDrop`
   with `targetSongExtensions` (`test/file-drop/route.test.ts`).
4. **Role hooks and embed.** The generated module's embed pairing (a manifest `embed` with no TS declaration, a declaration with
   no manifest entry, the halves in different packages, an id in two packages, an id twice in one, an unregistered platform, and
   four manifest-side refusals; RED first); `parameters` equality (the lane lists of mgb and the six BlipToaster chips equal the
   old tables' expansion, recorded from `96d748e5c`'s `parameterMap.ts` before the move, as `test/midi/recordedParameters.ts`
   and then the packages' own `mgb/parameters` and `bliptoaster/parameters`); `test/systems/role-derive.test.ts` (the deep read
   only for a role that asks, and an unreadable ROM leaving a stored `chip` alone); `test/controller/sync-latency.test.ts`;
   `loadEmbedded("nope")` returning null with nothing booted (`test/systems/store-load.test.ts`).
5. **Boundary and scanner.** The three rule-9 fixtures and the scanner test (section 7.1 and 7.2), both RED on the pre-stage
   tree; `migrate-v3-frozen` (10 cases); `systems/lsdj-song-import`, the shell's end-to-end song import (RED: with `loadSong` a
   no-op in `songImportHost.ts` it fails `expected 1, got 0`); `bliptoaster/rolesForRom`; and the package cases of the moves
   (section 4.7).

## 8. The stages

Five stages on a prototype branch, DeepSeek Flash implementing and Opus reviewing each before the next, as F3 and F2 ran.
Each stage leaves the tree green on every oracle of 7.3 that it can reach (the parity and render baselines need no
re-record at any stage, so a stage that changes them is wrong).

1. **Console descriptors.** `core/ts/consoles.ts` and the `retroplug.consoles` manifest member (the design's `consoles` /
   `realCarts` contribution keys, moved there by the stage 1 review); the `sameboy`, `mesen` and
   `n8` descriptors; the generated lists (`src/generated/consoles.ts`) and the refusals; `src/consoles.ts` and `src/platform.ts` over it; open
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
   rule 9 and its fixtures; the literal scanner; the docs pass (`AGENTS.md`, `spec/00`, `02`, `06`, `07` and `docs/package-split.md`,
   which splits F4 into F4a and F4b and records per-product bundles as owed). *Exit:* `check:boundaries` green with the allowlist
   as section 7.1 lists it and RED on the stage 4 tree; every oracle of 7.3; the BlipToaster and NESVJ runs.

Section 11 records each stage's review and fix pass; the commits are in the order above.

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
   As built the read is a shell module both call, `src/rolesForRom.ts` (section 4.5).
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
  `ui/` and `cli/` join rule 9 and the literal scanner. The relative-import half of rule 9 is written against
  `packages/retroplug/src` alone and the scanner walks one root (`--root`), so F4b needs a second root (or a root list) and
  fixtures of its own; the scanner's sets can stay (they are the product's consoles either way). `menuDefs.ts` still names
  every cart's catalog (stage 5 repointed those imports at their packages and no more), so a third-party tracker still needs
  a shell edit until F4b; the instance title's `"mGB"` short name lives there too.
- **Per-product bundles**, owed until a product's package list differs. F4a keeps the precondition (no bundle reaches a cart
  except through the generated registries) but found a limit on what the plumbing can trim: **the hook code rides every
  bundle that loads the registry.** The hooks hang on the app module's integration and role objects, so the render worker
  carries LSDj playback, `.lsdprj` import and the CC tables, and the control plane and the UI carry every cart's render hooks
  (section 11 gives the growth). A per-product bundle trims by package, not by surface, so it cannot remove this; if it
  matters the fix is the pattern the controller already used twice (the console descriptors and the protocol tables):
  surface-specific contribution modules, or hooks behind a lazy import. Record it beside the per-product bundles item.
- **The shell's hard dependency on launchpad.** `launchpad/registry` and `launchpad/enums` are on rule 9's allowlist and
  `projectConfig` serializes the controller target, so a product without the Launchpad (the option the user did not take for
  the plugin) needs those behind a contribution first.
- **`MemoryType`** stays a closed wire enum (F2's item, unchanged).
- The Windows, macOS and arm64 builds of everything F2 and F3 left
  owed (F4a touches only TypeScript, so it adds none of its own; the one non-TypeScript edit is the `[settings]` oracle in
  `SystemNode.test.cpp` and `bundles.cmake`'s byproduct list, section 11).

## 11. As built (the prototype)

The prototype, on `feature/modularize` over the design commit `96d748e5c`, is five commits, one per stage, each built by
DeepSeek Flash and reviewed by Opus, with the reviews' fix passes folded into the commit they belong to by fixup and
autosquash, so the five trees stand as final (the last pass folded into all five at once, which is why a stage report's sha
is older than the branch's). Sections 3 to 10 above were corrected in place to what the build found; this section is the
record of how, and the plan is written against it.

**The stages**, by commit subject and current sha:

1. `cf1d152da feat(f4a): register the consoles as package descriptors`: `core/ts/consoles.ts`, the `retroplug.consoles`
   manifest member and `src/generated/consoles.ts`, the three consoles modules, `src/consoles.ts` and `src/platform.ts` over
   it, open `Platform` / `Core`, `projectConfig` validation, `buildGraph`'s tables off the descriptors, `savPaths` and
   `fileSelection` extensions.
2. `81e6f0f60 feat(f4a): render through the console registry and the carts' render hooks`: `trackerRender.ts`, the three
   carts' render modules, `render.ts` and the CLI session off every platform switch, `RenderContext.registry`.
3. `60762c266 feat(f4a): the tracker playback and song-import hooks, and the shell's pollers through them`: `playback`,
   `sramSignature`, `songImport` and the manifest `capture`, the controller poll, `controller.ts`, `fileDrop`, `songImportHost`.
4. `898d87866 feat(f4a): the role hooks, the embedded ROM and the cart-named API`: `parameters`, `deriveConfig`,
   `hostLatencyMs`, link groups, `savFromJson` out of the caps, the TS embed and its pairing, `loadEmbedded`,
   `compileKitFormat` and every caller.
5. `0ccc5a9cf feat(f4a): the shell's boundary, the doors gone and the tests with their code`: the four doors deleted, tests
   moved to their packages, rule 9, the literal scanner, `migrate-v3-frozen`, the docs pass.

The branch is 231 files, +8375 -2328 over the design commit (per commit now: stage 1 35 files, stage 2 26, stage 3 33, stage 4 95,
stage 5 100). Nothing was pushed.

**What each review found, and what the fix pass changed.**

1. *Stage 1 (with fixes, no Critical).* Important: the first cut generated the descriptors into `packages.ts`, so the planner's
   bundle went from 12 modules to 210 (780 KB) and F1's bundle-protocol invariant was loosened in its test to let it pass;
   real carts had no uniqueness refusals; `linkGroupOf` and `exportModeOf` scanned every descriptor rather than reading the
   system's (and `directGraph` was half converted); and the registry test checked the data, not the new lookups. Thirteen
   minors. Fix pass: the `retroplug.consoles` manifest member and `generated/consoles.ts` (controller decision, section 3),
   `bundle-protocol.test.ts` restored byte for byte (16 modules, 29,230 bytes), the two cart uniqueness refusals, both lookups
   keyed by node type, the recorded tables replayed through `systemKnobs`, `droppedKnobs`, `classifyRom` and `buildGraph`'s own
   shapes, the `detectorAnswer` guard (RED shown), `drivesRealCartOf` answering true for a platform with no cart. The last
   pass added `consoles.ts` to `bundles.cmake`'s byproducts.
2. *Stage 2 (with fixes).* Critical: a `seedSong` that answered null was dropped and a legacy risa catalog rendered the wrong
   song silently where the old code threw. Important: the first E2 case passed on the old code; `render-cli.test.ts:94`'s
   expectation had been changed outside E1 to E4; and the moved code had been rewritten (LSDj's 8-character `--song` match,
   risa's `; rendering a fixed Nms` clause, the smsggdj and LSDj listings, the split log lines, the smsggdj Pause gate, the
   loops-forever warning's place). Fix pass: `seedRefusal`, `songNameLength`, `songList`, `warnings` and the facts argument,
   every moved text and gate restored verbatim, `render-cli.test.ts` byte-identical to its pre-stage text, E2 rebuilt to pin
   the full cap. Stage 3's pass also took `drivesRealCartOf`'s fall-through and a garbled comment.
3. *Stage 3 (with fixes, no Critical).* Important: the E3 test pinned no store behaviour; E3 was wider than the design said (the
   gate is the ROM's roles, so a generic ROM beside an LSDj battery reads nothing, and `launchpad.test`'s `withCart` fixture had
   to change); and the previous fix pass was not byte-identical (the split refusals were reworded, three outputs lost an em
   dash, a malformed LSDj battery no longer threw, a generic ROM beside a tracker battery was newly refused). Controller
   decisions: a store-level E3 case; the widened E3 and the generic-ROM refusal accepted as design edges (E3, E7), because they
   follow from decisions 4 and 5; the split refusals rebuilt byte-identical from the descriptors' labels; the em dash kept in
   OUTPUT through `\u2014` escapes; the malformed-battery throw restored. The generator's capture rule (wired node, a
   `serialOut` `out`) and the reader cache keyed by ROM path came in the same pass.
4. *Stage 4 (with fixes, no Critical).* Important: `derivedRoleConfigs` lost the empty-read guard, so an unreadable ROM
   overwrote a stored `chip` with the 2A03 default (fixed, section 4.5, with a case, RED shown). Nine minors: the embed's
   platform plumbing (`construct` looks it up by id), the same-package duplicate embed and the manifest-side fixtures,
   `hostLatencyMs`'s doc, `ROLE_HEADER_LEN` in core and the dead `providerHeaderLength`, `parameters.ts` leftovers, the kit
   spec types imported through `audioDriver`, `compileKitFormat` throwing synchronously (now async), `controllerModel`
   reading the battery on every call, `savPathOf`, `inheritLinkGroup` across consoles, and two inherited em dashes. Decision:
   the `got other` text restored for byte identity. The last pass changed `inheritLinkGroup` to compare the cable fields.
5. *Stage 5 and the whole prototype (with fixes).* Critical: the scanner test drove a bare `ts.createScanner` and lost its place
   after templates and regular expressions, finding 46 of 86 literals on the base (section 7.2). Important: the shell lost its
   only end-to-end song-import test in the moves; `migrate-v3-frozen` iterated the packages' current lists, so it could not
   fail on a reorder; and the docs pass left false statements (`src/kit` as the compiler's home, spec/06's capture shape,
   AGENTS.md's generated modules, "empty allowlist"). Also rule 9's relative-import gap, comments reworded only to dodge the
   exit grep (reverted, the grep now matches import specifiers), wrapping, a duplicate import. Fix pass (folded into all five
   commits): the syntax-tree scanner over registry-derived sets, `systems/lsdj-song-import` restored, `V3_ROLE_ENUMS` exported
   and the test over the frozen lists, rule 9's relative half with its fixture, the docs fixed (`AGENTS.md`, `spec/02`, `04`,
   `06`, `07`, `docs/lsdj.md`, `docs/package-split.md`, `dsp/ts/kitContract.ts`). The whole-prototype review's verdict: every
   behaviour change sanctioned, every departure recorded, and the growth below attributed.

**Departures from the design as committed (`96d748e5c`), and why.**

- **Console descriptors come through a `retroplug.consoles` manifest member and an import-light `src/generated/consoles.ts`,
  not the app module or `packages.ts`.** The planner's bundle is `buildGraph.ts`'s, and the descriptors are its tables; through
  the app they dragged every app, every role schema and zod into it (12 to 210 modules), which breaks F1's invariant.
  `PackageContributions` carries no `consoles` or `realCarts`; n8's cart is `n8/ts/consoles.ts`. Same pattern as F1's protocol
  tables.
- **`ConsoleDescriptor.exportModes`** (not in the design's interface): `pinsPlusRef` has an export mode and no split, so the
  mode's ordinal cannot come from a split's `tapMode` (`parity/render-shapes` caught it).
- **Mesen lists its GBA before its NES**, because a platform's registry place is behaviour (`siblingRomCandidates` pins `gb, gbc,
  gba, nes, sms, gg`), so the validation message names `gb, gba, nes, sms, gg` and not the design's example order.
- **The registry's extra lookups** (section 3), `systemNodeTypeFor` throwing for an unregistered platform, `exportModeOf` and
  `linkGroupOf` keyed by node type, `drivesRealCartOf` gaining an optional platform, `detectorAnswer`, and `rank` ties broken by
  descriptor id (a total order).
- **`platformSchema` and `coreSchema` are exported `superRefine`s** (a choice: the message names the set read at run time).
- **The generator's refusals are emitted into the generated modules**, not raised by the script, where they need TypeScript
  data (a role's category, an app's `embeds`); the data-only ones sit in `consoles.ts`. Two real-cart uniqueness refusals were
  added.
- **`TrackerRender` gained `warnings`, `songNameLength`, `songList` and `seedRefusal`, and `playingProbe` takes `RenderFacts`**;
  `RenderHost` has no `readFile`; `PLAY_HOLD_MS` is tracker's one value. Each exists to keep an old text or condition exactly.
- **Each tracker member's contract is a tracker module** (`trackerRender`, `trackerPlayback`, `trackerSongImport`,
  `trackerSramSignature`), the cart's implementation beside the cart.
- **The capture mapping is manifest data** (`capture` on `rp.lsdj.sync` in `PROTOCOL_NODES`), not `playback.captureFor`, so the
  planner resolves no integration; the generator refuses six malformed shapes.
- **The `--song` and `--list-songs` refusal names the integrations' labels** (`LSDj, SMSGGDJ, Risa (this ROM runs no tracker:
  its console is Game Boy)`), not `label (platform label)`; no contract ties an integration to a platform.
- **E3 is wider than designed and E7 is new** (section 6): the gate is the ROM's roles; a generic ROM beside a tracker battery
  reads no playback and refuses the song flags. Both accepted by the controller as consequences of decisions 4 and 5.
- **`songImport.import` takes a `SongImportHost` adapter** (`src/songImportHost.ts`), and `savPathOf` answers null for a system
  that went away, in which case the import refuses; the design passed the store's pieces ad hoc.
- **One shared `src/rolesForRom.ts`** serves the store and the render; `ROLE_HEADER_LEN` lives in core; `derivedRoleConfigs`
  applies nothing to an unreadable file.
- **The embed pairing is checked by the generated `packages.ts`**, which also refuses a declaration repeated inside one package;
  the generator script reads and validates the manifest side. The start menu's mGB row is generated from `embeddedRoms()`.
- **`compileKit` and `compileDmc` became `compileLsdjKit` and `compileRisaKit`** (the old names could not survive the exit grep),
  and **`compileKitFormat` is async** (an unknown id rejects).
- **`inheritLinkGroup` compares the two systems' cable fields**, and reads the group as a number.
- **Rule 9's allowlist is 24 entries derived from the tree**, with a refusal of a relative import out of `src` the design did not
  have; `EXCEPTIONS` stays empty and rule 9's `SHELL_IMPORTS` is the one deliberate list.
- **The scanner walks the syntax tree and reads its sets from the registry**, finding `.gbc` too; RED 90 literals in 8 files on
  `96d748e5c`.
- **Decision 13's test iterates the frozen lists** and `V3_ROLE_ENUMS` is exported.
- **Two non-TypeScript edits**, against "no C++ and no CMake": `SystemNode.test.cpp`'s `[settings]` oracle read the tables out
  of `buildGraph.ts` textually and now reads the descriptors (`descriptorSource(type)`, no case added, removed or
  re-recorded); and `bundles.cmake`'s byproduct list names `consoles.ts`. No C++ source or build rule changed.
- **`Object.hasOwn` is spelled `hasOwnProperty.call`** (the shell's lib target refuses `TS2550`).
- **The exit grep for the old API excludes dated records** (`docs/audit-2026-09-20.md`, the plan and design documents), and the
  docs pass touched `spec/04`, `spec/07`'s D2 bullet, `docs/lsdj.md`, `docs/package-split.md` and `dsp/ts/kitContract.ts`
  beyond the design's list, because each stated something F4a made false.
- **One shell test kept** (`systems/lsdj-song-import`) where the design moved all song-import cases to lsdj.

Existing tests whose expectations or fixtures changed, each judged by a stage review: the three `savFromJson` tests (section
4.6), `test/controller/songRefresh.test.ts` and `test/menu/launchpad.test.ts` (E3), `test/file-drop/route.test.ts`
(`targetSongExtensions` and the reason), `test-native/sms-render.test.ts` (`validateRenderOpts` takes the hooks),
`test-native/risa-render.test.ts` (`renderSongs`), the mock render contexts (a registry), `build-graph.test.ts` (the two
lookups' signatures) and `kit-baseline.test.ts` (a comment rewrap, no data). `render-cli.test.ts` is byte-identical to the
design commit's. No golden, baseline, digest, image or pack byte moved at any stage, and no `RP_*_UPDATE` was set.

**Verification.** A baseline run at `96d748e5c` and a run at every stage and at the tip were each 43 legs (`tip-verify.sh`: the
F2 kit's 42 plus the measure), every one exit 0, and each stage's summary matched the baseline's but for the counts below.
The tip's run is `/tmp/f4apro/s5fv-*`, taken on the tip's tree.

| | baseline | tip (`0ccc5a9cf`) |
| --- | --- | --- |
| `pnpm test` | 190 files, 1999 cases, 0 skips | 207 files, 2094 cases, 0 skips |
| `test:native` (fixtures, `RP_FAIL_ON_SKIP=1`) | 172 files, 629 ok, 2 bench skips | identical |
| `test:native` (no fixtures, the CI picture) | 172 files, 629 ok, 144 skips | identical |
| parity (strict) | 30 files, 250 cases | identical |
| `test:ui` | 42 files, 46 cases | identical |
| `test:plugin` / the engine-graph binary | 13 binaries, 336 cases (325 passed, 11 skipped) | identical cases; assertion totals move run to run (1,701,888 and 1,701,892; 1,713,639 and 1,713,850) |
| `check:boundaries`, `check:docs`, `typecheck` | 0 refusals, 93 files clean, 0 | identical, with rule 9 in force |
| tsan and asan (engine-graph, graph, n8, launchpad, devmode, kit) | 172 / 80 / 104 / 53 / 51 and 172 / 296 / 104 / 96 / 3 cases | identical |
| kit baseline, render baseline, `render-chunks`, `render-cli`, the risa and sms render suites | 3 / 9, 5, 5, 6 | identical (hashes unchanged) |
| device check, box, device mode | totals, `flash 199920`, 96 cases | identical |
| images (box native `af36f223`, PIO `4b4f9abc`, check `38665e2c`) and the three `bliptoaster` pack byte hashes | recorded | identical (`diff` of the logs is empty) |
| BlipToaster's and NESVJ's suites against the rebuilt CLI | 57/57 and 19/19 files | identical |
| `reaper:params`, `reaper:params-clap` | not run at the baseline | PASS: 2725 and 644 parameters before and after the click on `Load mGB`, count unchanged |

Two stage runs were not clean first time, both recorded: stage 2's `asandevmode` exited 42 once on a loaded machine (the
timing-sensitive `ScriptPack` deadline case under ASAN; 96/96 on the re-run, no device-mode code touched), and stage 3's first
`native` and `nativeci` runs failed on `render-cli.test.ts`'s line because a fold had put the test's restore in the wrong commit
(re-folded, both green with the baseline's totals). The test delta is the stages' own cases (+17 files, +95 cases net of the
moves; the moved files' cases are conserved and one new shell file is `bliptoaster/rolesForRom`).

**Bundle sizes** (`measure.sh`, recorded and not gated; `/tmp/f4apro/base-measure.log` against `s5fv-measure.log`):

| Bundle | base | tip | growth |
| --- | --- | --- | --- |
| `cli-bundle.js` | 1,298,801 | 1,342,974 | +44,173 |
| `cp-bundle.js` | 1,141,126 | 1,176,381 | +35,255 |
| `render-worker-bundle.js` | 1,189,430 | 1,225,868 | +36,438 |
| `ui-bundle.js` | 1,943,736 | 1,981,163 | +37,427 |
| `retroplug-cli.js` (the SDK) | 1,134,781 | 1,178,451 | +43,670 |
| `dsp-kernel`, `script`, `script-test`, `ts-stripper` | | | 0 |

The binaries grow 25 to 78 KB (the embedded TypeScript), `libmesen` and its 288 objects are unchanged, and the planner bundle
stays import-light (16 modules and 29,230 bytes after stage 1's fix, 15 modules and 29,184 bytes at stage 4, no app, generated
`packages.ts` or zod module in either). The reviewer's per-stage attribution (each stage's increment, in KB;
the fix pass over stage 5 added 47 to 128 bytes):

| Bundle | s1 | s2 | s3 | s4 | s5 |
| --- | --- | --- | --- | --- | --- |
| cp | +10.5 | +9.6 | +8.4 | +5.8 | +0.8 |
| cli | +10.6 | +10.1 | +8.4 | +14.0 | +1.0 |
| render worker | +11.0 | -0.1 | +9.6 | +14.7 | +1.2 |
| ui | +10.5 | +7.1 | +4.5 | +14.4 | +0.8 |

Qualitatively: stage 1 is the descriptor data, the registry and the generated module's checks; stage 2 is the render hooks now
reaching every registry bundle (the render worker is flat because it already carried `render.ts`); stage 3 is playback, song
import and `sramSignature`, which reach the render worker too; stage 4 is the CC tables, `rolesForRom` and the embed (the control
plane gains least because it already carried `parameterMap`'s tables); stage 5 is the generated checks, with no doors. No new
package entered any bundle and the planner bundle is unchanged. The growth is the descriptors plus hook code that hangs on
objects every registry-loading bundle already imports (section 10); it has not been attributed module by module.

**What acceptance must run beyond tip-verify** (from the whole-prototype review):

- **A module-by-module bundle diff.** Run `tools/build-session.js`'s `bundleInputs` for each bundle entry (cp, ui, cli, render
  worker, CLI SDK) at `96d748e5c` and at the tip, and diff the module lists and bytes: the growth must be the new contract and
  registry modules and the hook code, and no package may newly appear.
- **The scanner's RED and rule 9's RED over full archives**: the scanner on `96d748e5c`'s `src` (90 literals in 8 files) and rule 9
  over a full archive of the pre-stage-5 tree (21 refusals over six files), re-run by the acceptance rather than read from the logs.
- **The fixtures-present run with `RP_FAIL_ON_SKIP=1`** for parity, the render baseline, `render-cli`, `render-chunks`,
  `sms-render` and the risa render suites (E2's claim that the affected cases are none rests on it), and the CI picture
  (`__RESOURCES_DIR__` and `__RISA_SRC__` pointed at a missing directory) for the fixture-free subset.
- **`reaper:params` and `reaper:params-clap` on the final tree.** The prototype ran them on the tip; acceptance repeats them on
  the tree it adopts, since the parameter map and the generated modules moved in the stages.
- **BlipToaster's and NESVJ's suites** against the final CLI (the `bt` and `vj` legs, kept).
- **A v2 project load through the frozen lists**: a store-level case or the migrate tests reading a v2 file with each of the five
  index-ordered enums, since the migration no longer imports the packages' lists.
- Beyond the review's list and still owed from F2 and F3: the Windows, macOS and arm64 builds and a first CI run, none of which
  F4a's TypeScript-only change adds to.

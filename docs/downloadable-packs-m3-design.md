# Downloadable packs, M3: placeholders for a missing pack

Part 1 of [the loading design](downloadable-packs-loading-design.md) is built in milestones; this is M3's design. Its
decision row: a project naming a missing pack opens, and the missing parts become placeholders that keep their data,
save it back unchanged, and come alive when the pack arrives (loading design section 4). M1, M2a to M2f, M5a to M5c and
the CLI milestone are done; M4 (archives, the CLI's pack commands, Settings > Packs) and M5d (the plugin's two
instances, Node, concurrency, sanitizers) come after this.

## Where things stand (2026-11-27)

What a project file can name, and what happens today when nothing provides it:

- **A system's platform or core** (`nes` with no Mesen loaded): `systemThinSchema`'s `platformSchema` / `coreSchema`
  refine against the live console registry, and `parseConfig` drops a failing system without a word
  (`src/projectConfig.ts`, `parseConfig`); `test/consoles/registry.test.ts` asserts it. The next save omits it, and the
  plugin's `getState` writes on every DAW save, so a DAW session loses it there too.
- **An embedded ROM id** nothing declares: `SystemsStore.adopt` returns null and `commit` ignores it.
- **A ROM no detector claims, or a platform with no default core**: `adopt` reclassifies the ROM bytes (`classifyRom`)
  and never reads the stored platform; `"unknown"` or no `defaultCoreFor` returns null, and the system is gone.
- **A role kind nothing registers, on a system that resolves**: kept as written and inert (`parsedForTheirNodes`,
  `withMissingFeatureRoles`); `test/systems/store-role-union.test.ts` holds it. Its stored position can move
  (`suggestedFirst`).
- **A role whose node type the Engine refused** (a pack refused natively, a clash): the Engine refuses the whole
  document, and `placeLoadedSystems` discards the system named in `lastErrors` and retries, so the system is lost on
  the next save. spec/07's M2e question ("should the control plane leave out the roles of packs its Engine refused?")
  is this.
- **Zip blobs shift.** `commit` looks up `systems/{i}/sram|state|song` by the filtered list's index while
  `buildArchive` wrote the full list's, so a dropped system 0 hands its SRAM to the system after it.
- **No graph document is saved.** The graph is derived from systems, roles and section models at every publish
  (`buildGraph`), so no file can name an unknown node type.
- **Nothing loads a pack after launch.** `loadAppPacks` is re-entrant but called once per context; `adoptContributions`
  (the project store's re-adopt of sections and services) has no production caller; the native `PackTable::load(dir)`
  and `Engine::syncPacks` exist, and no host calls a live load.
- **Native refusals never reach TS.** `__rp_packStatus` holds the TS loader's refusals only; `PackTable::refusals()`
  (API level, a missing library, discovery) has no reader, and `__rp_packList` lists loaded packs only.
- **No provenance.** The project file records no pack id; for a pack that is not installed nothing maps `nes` to Mesen.

## Decisions (the user's, 2026-11-27)

| Question | Decision |
|---|---|
| `rp.core.missing` | Deferred. No saved file can name a node type, so M3 covers what a file can name: systems (platform, core, embed) and roles. The node waits for user-built graphs; the loading design's section 4 says so |
| Naming the pack | Recorded at save: each system notes the package that provided its platform and core and each role's, built in or a pack alike. A file saved before M3 falls back to the platform or role kind's name |
| Live arrival | M3 builds the re-resolve and a minimal live load that drives it (a development pack folder loaded now); M4 puts install and Settings > Packs on that hook |
| How a placeholder is held | A placeholder is a system in the systems store (below), not a side table and not a raw layer under every system |

Rejected: a side table of kept systems in the project store, spliced back at save (the UI would need a second list,
and every slot reference, the layout's and the routing's, would be remapped twice); and every system keeping its raw
JSON under its parsed fields (it would also keep a known role's unread config keys, which is wider than M3 needs).

## 1. The placeholder system

A system that cannot be built because something a pack provides is missing becomes a placeholder: an entry in the
systems store with an id and a slot of its own, holding

- `raw`: the system's JSON object exactly as read;
- `blobs`: its zip entries (`sram`, `state`, `song`) as untouched bytes;
- `needs`: what is missing, each `{ what: "platform" | "core" | "embed" | "role", key, pack?: {id, name, version?},
  status: "missing" | "refused", reason? }`.

It builds no core and places nothing in the graph (`buildGraph` skips it). Its view carries `placeholder: { needs }`
in place of the running fields. The only edit on it is Remove, an ordinary edit that marks the project dirty and drops
its blobs.

What makes a placeholder: the strict parse failing on a platform or core the console registry lacks, an `embeddedRom`
id the embed registry lacks, or `adopt` finding no detector for the ROM or no default core for its platform. A system
that fails the parse for any other reason (a malformed field) is still dropped, as today.

## 2. Load

1. `parseConfig` keeps a system that fails only on a pack-provided name as a placeholder entry, with its original
   index, and derives `needs` from its provenance (section 4), else from the platform or kind name.
2. `commit` keeps every system's ORIGINAL index for the blob lookup, which fixes the shift. A placeholder's blobs are
   read into memory untouched, and `SystemsStore.addPlaceholder(raw, index, blobs, needs)` gives it its slot; `adopt`
   does not run for it. `adopt` itself can also hand back a placeholder (the detector or the default core is missing).
3. **A missing role on a live system.** A role is missing when its kind is not registered, its pack was refused in TS,
   or its node type was refused by the Engine. The control plane leaves a missing role out of the graph; the role
   stays in the config at its position, and the view marks it missing. `placeLoadedSystems` still discards a system
   the Engine refuses for a reason of its own, but an `unknown-type` naming a role's node is a missing role, not a
   discarded system. The Engine's refusals reach TS through the pack status (section 5).
4. The load's outcome gains `missingPacks`, the distinct packs (or, without provenance, platforms and kinds) its
   placeholders and missing roles need, with their counts.

## 3. Save

- `buildConfig` writes the systems in slot order. A placeholder writes its `raw` object unchanged.
- A live system writes as today, with two changes: its roles keep their stored order (a known kind's config is still
  parsed; nothing moves), and it carries `provenance` (section 4).
- `buildArchive` writes a placeholder's blobs byte for byte under its slot's index. Indices are the slot order, so
  removing a system renumbers every system's blobs consistently.
- The plugin's `getState` goes through the same path, so a DAW save keeps a placeholder.

## 4. Provenance

An additive member of a saved system (no migration step: a reader that does not know it ignores it):

```json
"provenance": {
  "platform": { "id": "mesen", "name": "Mesen", "version": "0.1.0" },
  "roles": { "bliptoaster": { "id": "bliptoaster", "name": "BlipToaster", "version": "0.1.0" } }
}
```

The writer takes the providing package from the registry entry's `pkg` (the console for the platform and core, the
role registry for each role) and its name and version from the pack table; `version` is present for a pack only. A
built-in package records its id and name too, so a project saved on today's desktop names Mesen when it later opens on
a build where Mesen is a pack. A placeholder's `raw` keeps the provenance it was read with.

## 5. Refusals reach TS

`__rp_packStatus` (and the status the CLI and the test hosts read) gains the native table's refusals
(`PackTable::refusals()`: API level, no library for this platform, discovery) and each Engine's `refusedPacks()`, so a
need whose pack is refused reads `status: "refused"` with its reason. That is the "needs updating" card's data, and
the missing-role rule's: the control plane leaves out the roles of packs its Engine refused.

## 6. Re-resolve

The project store subscribes to the contribution registries (`subscribeContributions`). On a commit it

1. tries each placeholder again through the load path (`adopt(raw, blobs)`), swapping a success into the same id and
   slot with its ROM, SRAM, state and roles;
2. re-places the missing roles whose kind or node type is now there;
3. runs `adoptContributions` for sections and services (wiring what has no production caller today);
4. publishes the graph once and notifies the UI.

A re-resolve leaves the project clean: nothing the user did changed it. A placeholder that still fails stays one, and
its `needs` are recomputed (missing becoming refused updates the card).

## 7. The live load

`loadPackNow(dir)` loads one development pack folder now:

1. native: `PackTable::load(dir)` into the process table, answering `{ refused: reason }` on a refusal;
2. each Engine registers it at its next `setGraph` (`syncPacks`, as today; a kinds half added live waits for the next
   launch, as M2f decided);
3. TS: `loadAppPacks()` again in each context the host has (the control plane, the UI, a CLI session), which loads only
   the new pack's halves;
4. the registry commit fires, and section 6 runs.

It is a development and test hook: reachable from the CLI SDK and the test hosts, from the plugin through the existing
control-plane bridge, and from no menu. M4's `pack install` and Settings > Packs call it.

## 8. UI

- **The placeholder tile.** `SystemGrid` renders a placeholder's slot with the tile's frame and focus border, so the
  layout does not move. Its body is text: the platform's label, the ROM's file name, and the reason ("Needs the Mesen
  pack", "Mesen 1.2 needs updating for this RetroPlug"). Its menu has Remove only.
- **A missing role** shows on the live tile's menu title row in the existing `[!]` warning style ("BlipToaster pack
  missing").
- **The notice.** A load whose outcome lists `missingPacks` shows one notice overlay (the `noticeTree` the incompatible
  and error outcomes use): the packs it needs and what each covers, and that the parts are kept and saved unchanged. In
  the plugin it shows when the editor opens.
- The shell names no pack, platform or kind: every string comes from `needs` (the boundary rule).

## 9. Tests

- **Pure tier.** `parseConfig` keeps placeholders with the right `needs`, with and without provenance; a malformed
  system is still dropped. A project with a placeholder between two live systems saves its placeholder and blobs back
  byte for byte, and a file this build wrote saves to identical bytes. The blob index fix (the system after a
  placeholder keeps its own SRAM). Role order kept. Provenance recorded for a built-in package and a pack. A fake
  registry commit swaps a placeholder in at its slot and leaves the project clean.
- **Native tier, on a host that links neither Mesen nor BlipToaster** (`retroplug-host-packs` with no pack on the
  path): a project with a BlipToaster NES system and a Game Boy system opens with the Game Boy running and the NES a
  placeholder; it saves to identical bytes; `loadPackNow` with Mesen then BlipToaster brings the NES system up with its
  ROM, SRAM and roles intact, and it renders; the API-level fixture pack gives the refused reason; a role the Engine
  refused is left out and its system still runs.
- **UI tier.** The placeholder tile's text and Remove, the `[!]` row, and the notice, run with no packs loaded.
- **Plugin.** `getState` after opening a project with a placeholder writes it back.
- **Changed, not deleted:** the "drops the entries that fail the strict parse silently" case and the discard on an
  Engine refusal become the placeholder and missing-role cases.
- **Pack modes** pass unchanged: nothing is missing there.

## 10. Not in M3

`rp.core.missing` (user-built graphs); the download offer (part 5); a known role's config keeping keys its schema does
not read; the concurrency and sanitizer legs of a live load (M5d); `pack install`, `.rppack` archives and Settings >
Packs (M4); a pack's embeds (still refused by `checkManifest`).

## 11. Docs

spec/05 (the persistence policy: placeholders, provenance, role order), spec/02 (the live load, refusals in the pack
status), spec/03 or spec/13 (the placeholder tile and the notice, wherever the grid is described), the loading
design's section 4 (`rp.core.missing` deferred, the live-load hook), spec/07 (M3 deleted, its leftovers added).

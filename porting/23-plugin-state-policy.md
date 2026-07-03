# Step 23 — Plugin state handling policy

**Status:** Proposed (design). No code yet — this exists to pin the decisions
before building. Prompted by the sav-pairing / auto-save / portable-`.rplg` work
raising "who owns the truth: the DAW chunk, or the files on disk?"

## The problem

RetroPlug persists the same logical state (ROM, cartridge battery RAM, savestate,
LSDj kits, layout) through **two independent channels** that can disagree:

- **The DAW state chunk** — `getState`/`setState` serialise the whole project to a
  self-contained zip (ROM + SRAM + savestate + kit bytes), base64-wrapped into
  DPF's string state ([PluginDSP.cpp](../packages/native/src/PluginDSP.cpp)). The
  host owns it; it travels with the `.rpp`/session.
- **Loose files on disk** — the sibling `<rom>.sav` (battery RAM, mirrored like a
  normal GB emulator) and `.rplg` project files (thin path-only or self-contained
  zip).

Nothing currently reconciles them, and the *timing* of the loose-`.sav` mirror is
incidental rather than designed. The audit surfaced three concrete symptoms that
are really one missing policy:

1. In a DAW, the loose `<rom>.sav` only updates while the **editor window is open**
   (`pumpSramAutoSave` is driven from `PluginUI::uiIdle` only) — even though the
   emulator keeps writing SRAM as audio processes. So the loose file silently goes
   stale relative to the live cartridge and the DAW chunk.
2. In **standalone** there is no chunk — the `.sav`/`.rplg` on disk *are* the
   persistence — so the same staleness is a real durability gap (esp. on quit).
3. Moving/sharing a DAW project with an absolute paired `savPath` restores SRAM
   from the embedded bytes but leaves a stale auto-save *target*, with no
   missing-file prompt (the `getState/setState` path doesn't scan).

## Current behaviour (as built)

| Aspect | Today |
| --- | --- |
| DAW chunk contents | Self-contained: ROM + SRAM + savestate + kits embedded. |
| DAW load (`setState`) | Chunk is authoritative; the loose `.sav` on disk is **never** consulted. |
| Loose `<rom>.sav` write | Only when `autoSaveSram` preference is **on** (global, default **off**) AND the editor is idle-pumping. |
| Loose `<rom>.sav` read | Only on a **thin** `.rplg` load (`resolveSavPath` → sibling or paired override). The DAW chunk / zip carries its own SRAM and ignores the sibling. |
| Standalone quit | Project has an unsaved-changes prompt; SRAM has **no** guaranteed flush. |
| Auto-save cadence | Throttled UI-idle tick; no host-save / deactivate / quit hook. |

Defaults are conservative (auto-save off, chunk-authoritative), which is safe but
means the "keep my `.sav` in sync like a real emulator" expectation silently
doesn't hold in a host.

## The decisions (each with a recommended default)

**D1 — Who wins when the DAW chunk and the on-disk `.sav` disagree on load?**
Recommend: **chunk wins in a DAW** (it's the session's truth and self-contained);
**disk wins in standalone** (there is no chunk). Optionally expose a
`sramOnLoad = { embedded-wins | newer-disk-wins | ask }` for users who edit `.sav`
files externally. Default `embedded-wins`.

**D2 — When does the loose `<rom>.sav` get (re)written?**
Recommend making this explicit instead of "whenever the editor idles":
`sramMirror = { off | on-project-save | continuous }`.
- `off` — never touch loose `.sav`; the chunk/project is the only truth.
- `on-project-save` (recommended default when mirroring at all) — flush dirty SRAM
  to the sibling on host save (`getState`) and on `deactivate()`/quit, so it stays
  fresh **without** depending on the editor being open.
- `continuous` — today's idle-tick behaviour, plus the flush hooks above.

**D3 — What does the DAW chunk embed?**
Recommend keep **self-contained** (portable across machines, survives moved ROMs)
as the default; optionally a `dawChunk = { self-contained | thin }` for users who
want small sessions and keep assets in a stable location. Default `self-contained`.

**D4 — Standalone durability.** Guarantee a **flush on quit**: the existing
project close-prompt covers the `.rplg`; add an SRAM flush (respecting D2) so
in-game progress isn't lost on close. Not optional — it's a correctness fix.

**D5 — Stale absolute `savPath` on a moved DAW project.** On `setState`, if a
paired `savPath` override names an unwritable/missing location, **fall back to the
suffix sibling** rather than silently no-op'ing or clobbering. (Scanning for
missing files on the chunk path is heavier; the fallback is the minimal safe
behaviour.) Default: silent fallback + a one-line log.

## Where the settings live

- Global user preferences already exist:
  [`UserConfig`](../packages/native/src/config/UserConfig.hpp) holds
  `autoSaveSram` (bool). D1/D2/D3 fit here as small enums, since they're
  user-workflow preferences, not per-song state.
- Per-project override (if wanted) would go in
  [`ProjectSettings`](../packages/native/src/project/ProjectConfig.hpp) alongside
  `layout`/`midiRouting`/`zoom`. Recommend **global-only** to start; add a
  per-project override only if a real need appears.
- `autoSaveSram` (bool) is subsumed by D2's `sramMirror` enum — migrate it (the
  pre-release format allows a straight replacement, no shim).

## Open questions for review

- Is `chunk-authoritative` on DAW load the right default, or do enough users edit
  `.sav` files externally to warrant `ask`/`newer-disk-wins` visibility?
- Is a per-project override (D-anything) worth the surface area, or is global fine?
- Should `sramMirror` default to `off` (chunk-only, least surprise in a DAW) or
  `on-project-save` (matches the "emulator keeps a `.sav`" expectation)?

## Not in scope here

The bugs the audit found (relink duplicate key, savestate phantom, add-pairing
collision, replace-pairing override persistence) are already fixed independently;
this doc is only about the *policy* the fixes should eventually plug into.

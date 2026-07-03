# Step 23 — Plugin state handling policy

**Status:** Implemented — **D1, D2, D4, D5 built**; **D6 documented** (no code by
design); **D3 deferred** (default already ships; not worth the surface yet).
Prompted by the sav-pairing / auto-save / portable-`.rplg` work raising "who owns
the truth: the DAW chunk, or the files on disk?"

Decisions taken (all the recommended defaults): `sramMirror` defaults to
**OnProjectSave**; settings are **global-only** (UserConfig, no per-project
override); load precedence is **hardcoded** (chunk-wins in a DAW, disk-fills-in
for thin/standalone loads) with **no** user-facing `sramOnLoad` enum. See the
per-decision "Built:" notes below.

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

Nothing reconciled them, and the *timing* of the loose-`.sav` mirror was
incidental rather than designed. The audit surfaced three concrete symptoms that
were really one missing policy (all now addressed — see the decisions and the
"Behaviour (as built)" table):

1. In a DAW, the loose `<rom>.sav` only updates while the **editor window is open**
   (`pumpSramAutoSave` is driven from `PluginUI::uiIdle` only) — even though the
   emulator keeps writing SRAM as audio processes. So the loose file silently goes
   stale relative to the live cartridge and the DAW chunk.
2. In **standalone** there is no chunk — the `.sav`/`.rplg` on disk *are* the
   persistence — so the same staleness is a real durability gap (esp. on quit).
3. Moving/sharing a DAW project with an absolute paired `savPath` restores SRAM
   from the embedded bytes but leaves a stale auto-save *target*, with no
   missing-file prompt (the `getState/setState` path doesn't scan).

## Behaviour (as built)

| Aspect | Now |
| --- | --- |
| DAW chunk contents | Self-contained: ROM + SRAM + savestate + kits embedded. (D3 unchanged.) |
| DAW load (`setState`) | Chunk is authoritative; the loose `.sav` on disk is **never** consulted (`addSystem` only slurps the sibling `if (cfg.sram.empty())`). Dangling absolute paired-save targets are dropped (D5). |
| Loose `<rom>.sav` write | Governed by `sramMirror` (global, default **OnProjectSave**): flushed at host save (`getState`) + `deactivate()`/quit; **Continuous** adds the throttled idle-tick writes; **Off** never writes. |
| Loose `<rom>.sav` read | Only on a **thin** `.rplg` load (`resolveSavPath` → sibling or paired override). The DAW chunk / zip carries its own SRAM and ignores the sibling. |
| Standalone quit | Project has an unsaved-changes prompt; SRAM is flushed via the `deactivate()` hook (respecting `sramMirror`). |
| Mirror cadence | Flush hooks (host save / deactivate) always; idle-tick only in Continuous. The DSP learns the mode via a `SetSramMirror` command (pushed on toggle, reconciled by the pump). |

The default (OnProjectSave) keeps the `.sav` fresh like a real emulator **without**
depending on the editor being open, while avoiding a write on every idle tick.

## The decisions (each with a recommended default)

**D1 — Who wins when the DAW chunk and the on-disk `.sav` disagree on load?**
Recommend: **chunk wins in a DAW** (it's the session's truth and self-contained);
**disk wins in standalone** (there is no chunk). Optionally expose a
`sramOnLoad = { embedded-wins | newer-disk-wins | ask }` for users who edit `.sav`
files externally. Default `embedded-wins`.
> **Built:** No code needed — already the behaviour (`Project::addSystem` slurps
> the sibling only `if (cfg.sram.empty())`). The `sramOnLoad` enum was **not**
> added (global-only surface). Regression guard in `PluginRpcServiceTests`
> ("embedded SRAM wins over a conflicting on-disk sibling").

**D2 — When does the loose `<rom>.sav` get (re)written?**
Recommend making this explicit instead of "whenever the editor idles":
`sramMirror = { off | on-project-save | continuous }`.
- `off` — never touch loose `.sav`; the chunk/project is the only truth.
- `on-project-save` (recommended default when mirroring at all) — flush dirty SRAM
  to the sibling on host save (`getState`) and on `deactivate()`/quit, so it stays
  fresh **without** depending on the editor being open.
- `continuous` — today's idle-tick behaviour, plus the flush hooks above.
> **Built:** `SramMirror` enum (`config/SramMirror.hpp`) replaces the
> `autoSaveSram` bool; default **OnProjectSave**. Flush at `getState` /
> `deactivate` via `rp::flushSramMirror`; idle-tick gated on Continuous in
> `pumpSramAutoSave`. The DSP reads the mode from a `SetSramMirror` command.
> Settings menu: 3-state "SRAM Mirror" cycle.

**D3 — What does the DAW chunk embed?**
Recommend keep **self-contained** (portable across machines, survives moved ROMs)
as the default; optionally a `dawChunk = { self-contained | thin }` for users who
want small sessions and keep assets in a stable location. Default `self-contained`.
> **Deferred:** The default (self-contained) already ships, so this is pure
> opt-in surface with the least payoff — and it cuts against the global-only,
> minimal-surface decision. Not built. Revisit only if a real "small sessions,
> assets in a stable location" need appears; then add the `dawChunk` enum
> (getState/setState would honour it, everything else is unchanged).

**D4 — Standalone durability.** Guarantee a **flush on quit**: the existing
project close-prompt covers the `.rplg`; add an SRAM flush (respecting D2) so
in-game progress isn't lost on close. Not optional — it's a correctness fix.
> **Built:** `PluginDSP::deactivate` calls `flushSramMirror` (respecting the
> `sramMirror` mode), so stop/quit spills battery RAM. Covered indirectly by the
> `flushSramMirror` unit test (the deactivate hook is the same call).

**D5 — Stale absolute `savPath` on a moved DAW project.** On `setState`, if a
paired `savPath` override names an unwritable/missing location, **fall back to the
suffix sibling** rather than silently no-op'ing or clobbering. (Scanning for
missing files on the chunk path is heavier; the fallback is the minimal safe
behaviour.) Default: silent fallback + a one-line log.
> **Built:** `rp::sanitizeSavTargets` (in `ProjectMissingFiles.hpp`) clears any
> paired `savPath` whose parent directory is gone; `PluginDSP::setState` calls it
> on the parsed chunk and logs the count. Falls back to the ROM sibling (or no
> loose mirror when the ROM is embedded). Unit-tested (drops dir-gone target,
> keeps dir-present one).

**D6 — Two *independent* instances mirroring to the same `<rom>.sav`.** Within one
project, `savSuffix` already disambiguates duplicate/repeat loads of a ROM
(`game.sav` / `game-2.sav`, plus orphan-file protection). But `assignSavSuffix`
scopes to a single `project_`, so **separate** plugin instances (two DAW plugins,
or two standalone processes) each default to suffix 0 and, with continuous
mirroring on, both write `<rom>.sav` — last-writer-wins. This is inherent:
independent instances can't coordinate without a cross-process lock/registry,
which is disproportionate. Recommend **don't detect it.** Rely on (a) the DAW
chunk being each instance's real truth (no session data loss — the loose file is
only a mirror), and (b) D2 making continuous `.sav` mirroring opt-in, so the
collision only happens when a user deliberately mirrors — the same semantics as
pointing two emulators at one save file. Document it. Note this is a `.sav`-only
concern: the `.rplg` is never auto-written (only `writeSiblingProject` once on
load + explicit Save), so a shared `.rplg` is just ordinary last-writer-wins on
an explicit save.

## Where the settings live

- Global user preferences already exist:
  [`UserConfig`](../packages/native/src/config/UserConfig.hpp) holds
  `autoSaveSram` (bool). D1/D2/D3 fit here as small enums, since they're
  user-workflow preferences, not per-song state.
- Per-project override (if wanted) would go in
  [`ProjectSettings`](../packages/native/src/project/ProjectConfig.hpp) alongside
  `layout`/`midiRouting`/`zoom`. Recommend **global-only** to start; add a
  per-project override only if a real need appears.
- `autoSaveSram` (bool) is subsumed by D2's `sramMirror` enum — **done** (straight
  replacement in `UserConfig` / `config.json`, no shim; pre-release).

## Resolved (were open questions)

- **DAW-load default:** kept `chunk-authoritative` with no `ask`/`newer-disk-wins`
  visibility. External `.sav` editing is niche; the relink flow already covers a
  genuinely missing sibling on a thin load. Revisit if users ask.
- **Per-project override:** not added — **global-only**. Reassess only on a
  concrete need.
- **`sramMirror` default:** **OnProjectSave** (matches the "emulator keeps a
  `.sav`" expectation) rather than `off`. The flush hooks make it fresh without
  the per-tick cost of Continuous, and the two-instance collision (D6) only
  surfaces if a user deliberately opts into Continuous mirroring on a shared ROM.

## Not in scope here

The bugs the audit found (relink duplicate key, savestate phantom, add-pairing
collision, replace-pairing override persistence) are already fixed independently;
this doc is only about the *policy* the fixes should eventually plug into.

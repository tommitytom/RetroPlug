# Docs cleanup: AGENTS.md cut down, spec/07 pruned

Two files have grown with every phase, and agents (and the user) pay for that length on every session:

| File | Now | Problem |
|------|-----|---------|
| [AGENTS.md](../AGENTS.md) | 835 lines, 11.3k words | loaded into every session through CLAUDE.md; most of it describes how things work (test-binary inventories, package mechanics, the Launchpad pack, the SDL legs, the CLI harness's internals) rather than saying what an agent must or must not do |
| [spec/07-remaining-work.md](../spec/07-remaining-work.md) | 1,181 lines | 13 ticked checklist lines left in place; section 5 is about 970 lines of per-phase history (B3b to F5b), each ending in a "What it leaves" list the checklist indexes a second time, and many of those items are done |

User decisions (2026-11-12): AGENTS.md becomes rules and pointers (section 1); what leaves it is merged into the spec
doc that owns the topic, with a new spec/14 for the host legs and the CLI harness (section 2); spec/07 keeps a
checklist and per-area detail but no history (section 3); the move is proved by a fact ledger (section 4).

## 1. AGENTS.md: rules and pointers, 200 lines or fewer

**Header.** One paragraph: this file holds rules and pointers only; how something works goes in the spec/ doc that owns
the topic, and a line here links to it; keep it under 200 lines, and a new rule cuts or moves one.

**Start here.** README, [spec/README.md](../spec/README.md), docs/lsdj.md. The inline package map (about 200 words)
becomes one line linking to spec/06's "The packages", which lists them.

**Workflow rules**, one or two lines each, each with its link:
- Don't push to remotes or open PRs without an explicit ask.
- Commits carry no `Co-Authored-By` trailer and no attribution of any kind.
- When a commit finishes work listed in spec/07, it deletes that item there (checklist line and detail entry).
- Clone `--recursive`; the nested `pnpm install` in `deps/dpf.js/deps/lv_binding_js`; don't bump a submodule pointer
  without checking.
- `deps/sameboy` dirty after a configure is EXPECTED (the tracked patches): never reset, stash, commit or bump it.
- A Mesen edit carries a `RetroPlug:` marker; a Mesen merge runs both modes of `tools/build-mesen-all-consoles.sh`.
- A mquickjs pin bump that fails the configure's hash guard means regenerating the wrapper, not loosening the guard.
- Never `rm -rf build` to fix CMake; build with `build.sh` / `build.bat`, a single target with `-j`.
- Windows is `/W4 /WX` on our targets: fix our code, suppress only what we do not own and by number; CI's compiler
  matches the dev box's.
- Mesen LTO stays off in the dev loop and on in releases.
- `retroplug-sdl` is not in releases until the standalone is release-ready.
- Never commit derived artifacts (the embedded bundle C arrays).
- Config roots are versioned raw JSON: refuse and latch newer, migrate older, keep only the latest schema.
- A `device: true` node keeps to the portable settings types and its `#ifndef RP_DEVICE` guards.
- A package imports only its declared dependencies and exports no wildcard; adding one follows spec/06's checklist.
- Harnesses that can reach audio, video or serial devices run under the self-tested device guard; MIDI harnesses list
  only the fakes.

**Verification.** A "tests pass" claim needs an exit-zero; on Windows run the suites through `test.bat`; a case that
cannot run skips, the skip throws and is counted against the baseline; the test-speed traps (`renderAudio` takes
milliseconds, remove every system a test builds, a slow suite is one slow file); a parity failure is a real audio
change and an intentional one re-records in the same commit; the kit and render goldens compare bytes on
`linux-x64-gnu13` only. Pointers: spec/06's "which command proves which change" and spec/14.

**Gotchas.** lv_binding_js ignores `insertChildBefore` (the two workarounds named); the UI fonts are substituted by
symbol, never switched in `lv_conf.h`. One line each, the detail in spec/03.

**References repointed.** What names an AGENTS.md section moves to the content's new home:
`packages/retroplug/ui/views/TileViews.tsx`, `cmake/packages.cmake`, `scripts/cmake-build.js`,
`.github/workflows/build.yml`, `.devcontainer/post-create.sh`, `.claude/skills/cli-debug/SKILL.md`,
`.claude/skills/feature-dev/SKILL.md`, `packages/retroplug/README.md`, spec/06's two mentions and
`tools/lsdj-manual.py`'s docstring. Design docs and plans are records of their time and are not edited.

## 2. Where the departing content goes

Each paragraph is merged into what its destination already says. A fact the destination already states is dropped,
not repeated; a fact the code contradicts is dropped as stale, with the evidence in the ledger.

| Content | Destination |
|---------|-------------|
| Per-binary test inventories (`retroplug-engine-graph-test`, `-graph-test`, `-device-mode-test`, `-dynparams-test`, the fourteen `test:plugin` binaries): one line each (the binary, what it holds, its tags, the design sections that carry the detail) | spec/06 "The test tiers" |
| Package manifests, the registry generator, products as a dependency order, the boundary checks (`SHELL_IMPORTS`, `UI_IMPORTS`, the native twin), the source-literals and bundle-split tests, `Host<G>` | spec/06 "The packages" |
| The Windows warning policy (its three rules, VS 2026 on CI), Mesen's compile-out, Mesen LTO and its measured cost, the submodule bootstrap, the file watcher's member order | spec/06 "Build gotchas" / "CMake targets" |
| `device:check` and `device:box`, the mquickjs hash guard | spec/06 "The device build" |
| The test pool and longest-first timings, the skip mechanics, the parity, kit and render baselines (their unique sentences) | spec/06 "Hosts → test tiers" and its baseline subsections |
| Device packs on MicroQuickJS, the N8 pack, the Launchpad pack (providers, ABI 2, controller apps, the scenario mode), the audio and MIDI packages and config sections | spec/13, merged into an existing entry where one exists |
| `insertChildBefore` and its workarounds, the font symbol substitution and its regeneration | spec/03, a "Framework gotchas" subsection |
| Config migrations | spec/05 (a merge of any difference) |
| UI and background rendering | spec/11 (a merge of any difference) |
| The Reaper leg (the Linux scripts, `reaper:all`, `RP_JOB_TAG`), Reaper on Windows (its mapping), Renoise, the SDL harnesses (`sdl:smoke`, `sdl:controller`, `sdl:pipewire` and its legs, the device guard and its self-test), `retroplug-cli` as a consumer's test harness | new spec/14-hosts-harnesses.md, listed in spec/README's index |

## 3. spec/07: open work only, a checklist and its detail

**Top note**, replacing the intro: this file lists open work only; the commit that finishes an item deletes it, its
checklist line and its detail entry; never tick an item and leave it; the design docs and git log are the history,
and nothing here records what was done.

**Checklist.** One line per open item in today's groups (Before merge, Package split, Known bugs, Engine, Renders and
kits, Pico, Feature gaps and SDL), each linking to its detail entry. The ticked lines go.

**Detail.** One section per group, in the checklist's order. An entry is a bold title (the checklist's wording), what is
missing and why it matters in the present tense, and a link to the design section that found or deferred it.

**What goes.**
- Section 5's phase narratives are deleted. A "What it leaves" bullet becomes a detail entry only if the ledger finds
  it open, audited against the code; a finished one is cited (commit, file or line) in the ledger and dropped.
- Sections 1 to 3 (feature gaps, deferred or dropped, the pending refactor) fold into their groups; "Deferred /
  dropped" stays the last section, because it records intent, not work.
- Section 4 (the code-comment backlog, "none known") keeps only its standing note that `deferredProject` and
  `kind: "deferred"` are domain terms.

**References repointed:** spec/README's anchor into section 4, `packages/native/plugin/PluginDSP.cpp`'s comment naming
the control-handoff entry, spec/06's "spec/07's open entry", and any spec link to "section 5".

The result is expected at 250 to 350 lines.

## 4. Process and verification

1. **The ledger** (Opus), `.superpowers/docsclean/ledger.md`, gitignored. Every fact of AGENTS.md (one claim each: a
   rule, a command, a number, a trap) and every item of spec/07, numbered, with one disposition:
   - `keep AGENTS`;
   - `merge spec/NN §heading`;
   - `dup spec/NN §heading`, quoting the sentence already there;
   - `done` (spec/07 only), citing the commit, file or line that finished it;
   - `stale`, quoting the code that contradicts it.

   A `keep` or `merge` row carries a probe, a short exact string (an identifier, a number, a command) that must
   appear at its destination. The user sees the counts per disposition and every `done` and `stale` row in full
   before the plan is written.
2. **The plan** (Opus), from the ledger, in batch 1's format. Its author writes each task's text on the real tree,
   runs the checks, saves the cumulative `t<N>.diff`, restores the tree, and replays the plan's text to an identical
   tree. Tasks:
   1. spec/14 and spec/README;
   2. spec/06;
   3. spec/13, 03, 05 and 11;
   4. AGENTS.md and the references to it;
   5. spec/07 and the references to it.
3. **The checks**, `.superpowers/docsclean/check.mjs`, after every task and at the end:
   - every probe is at its destination, and every `dup` quote is in the doc it names;
   - every relative link and `#anchor` in a touched file resolves;
   - AGENTS.md is 200 lines or fewer, and spec/07 has no `[x]`;
   - no added line holds an em dash, and no added prose line (a table row aside) runs past 120 columns.
4. **Execution**, as batches 1 and 2:
   - a Sonnet implementer per task;
   - the spec check is tree identity;
   - an Opus review per task, reading the old text against the new for lost meaning and wrong facts the probes cannot
     catch;
   - follow-up passes, then an Opus final review.
5. **Acceptance.** Code changes only in comments, so:
   - the check script passes;
   - `pnpm check:boundaries` and `pnpm typecheck` pass;
   - a reconfigure and a `retroplug-cli` build pass, for `cmake/packages.cmake`'s comment.

   No device kit is run.
6. **Records.** This design gains an as-built section and an execution section, as batches 1 and 2 did. The ledger
   stays in `.superpowers/`; the commits are the durable record.

## 5. After the ledger

The ledger (1,695 rows: AGENTS.md 46 keep, 281 merge, 476 dup, 11 stale, 90 history; spec/07 23 merge, 129 dup,
272 open, 132 done, 19 stale, 216 history) changed four things, decided by the user on 2026-11-12:

- **Accepted limits leave spec/07.** The rows the ledger filed as Deferred's "accepted limits" and Engine's "accepted
  allocation residuals" describe behaviour we accept, not work: each becomes a sentence in the spec doc of the
  subsystem it limits (spec/02, 04, 13 and the like), and spec/07's checklist keeps about 85 lines.
- **Wrong text is fixed where the move touches it.** The destination docs' own errors (spec/06's test-binary row
  repeating three stale counts, its `PROVIDERS` sentence and `definePackage` list, the ts skip baseline line, spec/05's
  "in migrate.ts") are corrected in the task that merges into them, and three stale code comments
  (`cmake/test-binary.cmake`'s binary count, the root `CMakeLists.txt`'s LTO counts, `cli/sessions/test.ts`'s header)
  in the references task.
- **The map follows the ledger.** The package list is spec/00's "Package layout" (which gains its six missing
  packages), device packs and provider mechanics merge into spec/02's packages section rather than spec/13, the params
  checks into spec/12, and spec/06's pnpm-scripts table, which already holds most of AGENTS.md's test inventory, stays
  as it is (AGENTS.md's copy is a duplicate) but for its errors.
- **References that would dangle are repointed or cut.** About 30 code comments and spec lines name a spec/07 entry
  that is done or moving (the file watcher, the render-snapshot sweep, the press queue, the MI.MAP-forced capture
  among them); each points at the new entry or, for a done one, says what is true now.

The new spec/07 gains one Before-merge line the ledger found owed: a green CI run of the tip (run 43131974297 built
ea2517dc9; nothing after it has run on a runner).

## 6. As built

| File | Before | After |
|------|--------|-------|
| AGENTS.md | 835 lines | 102 lines: rules and pointers, each with its link |
| spec/07-remaining-work.md | 1,181 lines, 13 ticked lines, phase histories | 825 lines: the top note, 85 checklist lines, one detail entry each, Deferred |
| spec/14-hosts-harnesses.md | none | 204 lines: the Reaper leg, Renoise, the SDL harnesses and the device guard, `retroplug-cli test` |

- **spec/07 is longer than section 3 expected** (250 to 350 lines). 245 open ledger rows survived the audit, each
  carried with its fact, and an entry is a heading rather than a bold title so its checklist line can link it. The
  per-task and final reviews spot-checked 46 entries against the code and found none done.
- **AGENTS.md gained back rules the cut first lost**, found by the review of its task: the shell boundary rule, the
  fixture-gated acceptance of a parity or render change, the migration step, mquickjs staying unpatched, the MIDI
  listing pass; and three that had lived only in the specs (fixtures through `__RESOURCES_DIR__`, BlipToaster with
  `--rom` on the rig, the bare build for a standalone UI change).
- **Facts corrected on the way** (each checked against the code): the script-pack heap sizing (3/2 on a 64-bit device
  build, 4x with a 1 MiB floor on the desktop), which product names which `PROVIDERS`, the test-binary and golden
  counts, the rig scripts' default ROM (`n8-midi.nes` unless `--rom` names BlipToaster), the device guard being
  Linux's, `sdl:smoke` and `test:cli-ts` being CI gates, the migrations maps' home, drifted `PluginDSP.cpp` line links,
  and three stale code comments (`cmake/test-binary.cmake`, the root `CMakeLists.txt`'s LTO counts,
  `cli/sessions/test.ts`).
- **Left as they are:** design docs and plans that name a moved section (records of their time, among them
  `docs/package-split.md` and `docs/sdl-standalone.md`); spec/13's two sentences that CI builds the SDL standalone.

## 7. Execution

Ledger by Opus (1,695 rows, 1,703 after the plan's updates); plan by Opus, every block applied on the tree against
`check.mjs` red then green, and replayed from the plan's text to an identical tree. Six tasks, each by a Sonnet
implementer and spec-checked as tree identity with the validated diff: 80851a1c4, 38b2d6df1, e7ad71036, 54a8d9285,
166d89a7d, 9d0e84152. An Opus review per task asked for changes on all six (2 Critical, 28 Important in all); four
Sonnet follow-up passes, each verifying a finding against the code before applying it, fixed them: 4fa64ea0e,
ed4c6b678, 5876484ff, 74821c799. The Opus final review found two Important (the migrations line, doubled list
markers) and Minor items, settled in 4778e2075. Acceptance at the tip: `check.mjs` all checks pass, `pnpm
check:boundaries` and `pnpm typecheck` exit 0, a reconfigure and a build of `retroplug-cli`, `retroplug-clap`,
`retroplug-watcher-test`, `retroplug-engine-graph-test` and `retroplug-audio-test` exit 0.

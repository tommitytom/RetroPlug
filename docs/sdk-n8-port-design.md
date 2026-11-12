# Porting `feat/sdk-n8` onto `feature/modularize`

`feat/sdk-n8` is six local commits (2026-10-18 and 19, never pushed) on top of an old main (cfeb80e00), in the worktree
`/home/vscode/orca/workspaces/RetroPlug-main/marakihau`. They gave consumer scripts the real N8 and a keep-alive, fixed
a real N8 hardware bug, and hardened the CLI launcher and the TAP harness against errors they dropped. None of it is on
`feature/modularize`, which has since moved every file the branch touched (the N8 into `packages/n8`, the harness into
`packages/testing`, the CLI tools per package). The port map behind this design is `.superpowers/sdkn8/port-map.md`
(gitignored; every claim below is checked there against the code at 3fe37401d).

| Concern | Today on `feature/modularize` |
|---------|-------------------------------|
| `Edio::listDir` asks for every record in one `DIR_GET`; the real N8 answers "dir read error 0x82" past some size (a 181-entry directory failed; pages of 32 list it in 116 ms) | still one request (`packages/n8/native/src/n8/Edio.cpp:366-371`), behind `n8ListDir`, every `Ops::readFile` and the cart jobs' listing |
| The CLI launcher drops a session's uncaught error | a throw after keep-alive hangs; a timer throw is lost and the process exits 0; a plain throw exits 1 without its text (`packages/native/cli/main.cpp:309`, `:326`) |
| The `tjs.exit` override never takes (txiki's `tjs` is frozen) | its failed set leaves an exception pending; `g_exit` is dead; a normal exit skips `Teardown` |
| The TAP harness (`packages/testing/ts/harness.ts`) | an unawaited rejection passes; a case awaiting a dead task hangs under keep-alive; a stray rejection under `tjs run` kills the file with no output |
| Consumer scripts | reach the n8 facet already (the CLI starts the providers before reading `argv[1]`) but have no `connectN8`, and keep-alive only as the undeclared `globalThis.__rp_keepAlive` |

The consumer is nesvj's `retroplug-cli/tests/nesvj-raw.ts`: it runs unchanged if the port keeps the branch's names and
shapes, and its clip-set directory is the over-32-entry case.

User decisions (2026-11-12): `connectN8` lives in the n8 package (section 1); the resync lives in `N8Rpc::Session`
(section 1); the `tjs.exit` exception is cleared now and the skipped teardown is its own later change (section 2); the
test host's loop is a follow-up (section 3). The other choices take the port map's recommendations: the branch's device
surface, the wrapper's closed-device words, `keepAlive` as a plain re-export, the harness's hooks installed on the first
`test()`, `FakeN8` honouring the requested range without inventing the silicon's threshold, and `failureWatch`'s 3 s and
5 failures.

## 1. The N8 side, in `packages/n8`

1. **Paging.** `Edio::listDir` reads in pages of 32 records. `FakeN8` (`packages/n8/native/test/FakeN8.hpp`) answers a
   `DIR_GET` for the start and amount asked, not the whole list, so a fake directory over 32 entries frames the next
   command right. Tests: an `Edio.test.cpp` case pinning the 32-record split, an `Ops.test.cpp` `readFile` in a 40-entry
   directory, a `test-native` `n8-session` `--ls` of 40 entries. `edio-golden.json` stays byte-identical (its three
   listings are of one or two records).
2. **One `pickN8Port` and a `romName` guard** in `packages/n8/ts/index.ts`, replacing the three copies in `bridge.ts`,
   `cli/n8Load.ts` and `cli/n8Play.ts`.
3. **Resync in `N8Rpc::Session`** (`packages/n8/native/src/n8/desktop/N8Rpc.cpp`). A failed call marks the session
   dirty; the next call drains the line and re-handshakes before it runs (up to about 0.4 s and 0.3 s), so every CLI
   session and bridge recovers from a hiccup. Tests in `N8Provider.test.cpp`: a line model with late bytes, and a dead
   port, each shown red by removing what it guards.
4. **`@retroplug/n8/device`**, a new `exports` entry of the n8 package: `openN8Device` over `createN8Client`, with the
   branch's surface (`listDir`, `readFile`, `writeFile`, `makeDir`, `deleteFile`, `load`, `sniffRaw`, `writeFifo`,
   `close`, and `port`) and a closed-device guard in the wrapper's own words ("N8Device.<method>: the device is
   closed"), which also covers the zero-byte writes the client never sends. Tested over a fake `call`.
5. **The bridge's `failureWatch`** in `packages/n8/ts/bridge.ts`, shared by `n8-bridge` and risa's `n8-sync`: 3 s and 5
   failures, as the branch had them. Test: `bridgeWatch.test.ts`.

Nothing here touches the rig: the paging was verified on the real N8 when the branch was written, and is proved here
against `FakeN8`.

## 2. The SDK, the harness and the launcher, in this order

6. **The SDK surface.** `packages/retroplug/cli/sdk.ts` exports `connectN8` (re-exported from `@retroplug/n8/device`),
   `keepAlive` (re-exported from `cli/session.ts`) and the N8 types; `cli/sdk-types.d.ts` declares them;
   `cli/sdkSurfaceCheck.ts` gains the branch's identity `Exact` check (it catches a `readonly` mismatch), spelling no
   ROM extension so `test/source-literals` stays clean. One `SHELL_IMPORTS` line, with its reason, lets the shell import
   `@retroplug/n8/device`. Tests: `pnpm typecheck`, `pnpm check:boundaries`, `pnpm test source-literals`, a new
   `test-native` `n8-sdk` test driving the SDK over the fake N8; `retroplug-cli` is rebuilt (the SDK's bytecode is
   embedded).
7. **The harness**, in the branch's final form: a stray or unawaited rejection fails its case, a case awaiting a task
   that already died is raced against an abort its record triggers, every record is printed, and the hooks install on
   the first `test()`. New `tools/run-cli-ts-tests.sh` sections: a late-handled rejection, a top-level stray, a dead
   background task, which case a timer throw is blamed on. Then all four `pnpm test*` suites; a latent stray they
   surface is fixed in this task.
8. **The launcher** (`packages/native/cli/main.cpp`): `pumpOnce` checks for a pending exception after every pump in the
   bounded and the keep-alive loops, takes over txiki's stop request, and clears the exception the failed `tjs.exit` set
   leaves; a thrown session exits 1 with its error printed. It lands after 7 and 5, or test files with stray rejections
   would die with no TAP and `n8-bridge`, `n8-sync` and `launchpad-probe` would exit on their first timer throw. New
   `run-cli-ts-tests.sh` sections: a keep-alive throw through `run` and through `test`, a plain throw printing its text,
   a timer throw exiting 1; the whole `pnpm test:cli-ts` (its kit, render and Ctrl-C legs) still passes.

## 3. Docs, process, and retiring the branch

9. **Docs.** spec/14's "retroplug-cli as a test harness" gains `connectN8`, `keepAlive` and what a thrown session does;
   spec/13's N8 entry the paging and the resync; spec/09 only if it names the SDK's exports. AGENTS.md is unchanged.
   spec/07 gains two entries: a normal CLI exit skips `Teardown` (the `tjs.exit` override never takes), and the test
   host's loop (`packages/native/src/main.cpp`) drops uncaught errors the same way.

**Process**, as the known-bugs batches: this design; a plan by Opus whose every block is applied on the tree red then
green and replayed from its text; a Sonnet implementer per task, the spec check as tree identity, an Opus review per
task and follow-up passes; the serial verification kit as acceptance, then the parallel kit on the tip; an Opus final
review; this design's as-built and execution sections.

**After it lands**, and only on the user's word: delete the local branch `feat/sdk-n8` and remove its worktree
(`/home/vscode/orca/workspaces/RetroPlug-main/marakihau`, clean but for `deps/sameboy`). nesvj needs a CLI built from
`feature/modularize`; syncing it into `/workspaces/nesvj` is the user's step.

## 4. As built

Every item of sections 1 to 3 landed as designed, with these differences:

- **`connectN8` is a one-line wrapper** in `cli/sdk.ts` (`openN8Device(makeCall(), opts)`), not a bare re-export: the
  n8 pack cannot import the shell's `makeCall`.
- **Which failures dirty a session:** every failure but ok, file-not-found and the refusals made before the first byte
  (one shared `refusedBeforePort` helper names those for `portSaid` and the resync). `n8FlushInput` and
  `n8SetReadTimeout` neither resync nor dirty. A resync keeps the session's own handshake timeout, and a handshake on a
  dirty session is the resync, run once with the caller's timeout.
- **Hardened past the branch**, from the reviews: `romName` also refuses control bytes (as the host's `plainFileName`
  does); the device's `close()` is retryable and a throwing close no longer hides a failed handshake; a fatal bridge
  exit runs once; a throw in a `launchpad-probe` timer sends the Launchpad back to Live mode before exiting; a case that
  skips or throws owns the rejection it left; a throwing Ctrl-C hook ends a session.
- **The CLI harness checks match whole result lines** and the CLI's per-file verdicts, so a broken exit code turns them
  red (they first matched substrings, and `ok 2` matched `not ok 2`).
- **spec/07** gained "A normal CLI exit skips the teardown" (with the throw lost in the same loop turn as
  `tjs.exit(0)`) and merged the test host's dropped error with D3's "retroplug-host swallows a top-level throw".
- **Not proved on hardware:** the paging was verified on the real N8 when the branch was written; here it is proved
  against `FakeN8` only, and nothing runs `connectN8` through the shipped module (on this box the CLI lists the real
  serial ports).
- **Python:** the tracked `tools/run-cli-ts-tests.sh` (three snippets in its render leg), `tools/run-sdl-controller.sh`
  and `tools/run-sdl-pipewire.sh` run `python3`; the implementers ran the CLI check through a python-free copy, and the
  kits run the tracked scripts as before.

## 5. Execution

Port map by Opus; plan by Opus (every block applied on the tree red then green, replayed to an identical tree). Nine
tasks by Sonnet implementers, each spec-checked as tree identity with the validated diff: f283de0b0, c78928630,
bb93aeb48, f7a432fa3, 797ecdd3f, 886dde3bc, dc39e032f, b0cc7dc0e, b5d06160a. An Opus review per task: seven approved
with Minor findings, two asked for changes (Task 7: the CLI checks proved nothing; Task 9: a duplicated spec/07 entry),
none Critical. Four follow-up passes, each verifying a finding before fixing it test-first: d38788f02 and 3ad23c57e (the
N8 C++), 7573c594f, 8af0f43de, b9811df0c and da0664cf8 (the TS N8, the SDK, launchpad-probe), edd4ea2bd (the harness
and the launcher), 99e2a74bc (docs). The Opus final review found the whole READY, with Minor findings settled in
97d67b71f. Acceptance: the serial kit at 99e2a74bc, 45/45 (BlipToaster 57/57 and NESVJ 19/19 against the rebuilt CLI,
strict parity with no skips, SDL PipeWire 27/0/0); the parallel kit at 97d67b71f, 47/47 (sdlctl run in the serial kit).

# Plan: download mGB on demand (remove the embedded ROM)

Status: **design / not yet implemented.** To be implemented on Linux (better
runtime/network testing setup for this feature). This doc is the agreed plan
from the design discussion; the "Open decisions" at the bottom still need to be
settled before/while implementing.

## Motivation

Two reasons, licensing first:

1. **Licensing (primary).** RetroPlug's shipped binaries are **GPL-3.0** (Mesen
   is statically linked). The mGB Game Boy ROM is **GPL-2.0** (trash80/mGB, ©
   Timothy Lamb), and GPL-2.0-only is incompatible with GPL-3.0 when combined
   into one work. Today mGB is `bin2c`-baked *into* every binary → the GPLv2 ROM
   and the GPLv3 emulator live in the same executable. We have the author's
   permission to bundle it, but **not embedding it at all** is cleaner: a
   ROM the user downloads at runtime is *mere aggregation* — a separate GPLv2
   file next to the GPLv3 binary, no combination.
2. **Footprint.** The ROM leaves the binary (minor; it's only 64 KB), and it
   stops being duplicated across the per-target zips.

## Reality check: mGB is 64 KB

`resources/roms/mGB.gb` is 65,536 bytes. A download of that size is effectively
instant on any connection, so the in-menu **progress percentage will mostly
just blink**. The value of this feature is licensing + a reusable download
pattern, *not* download-time UX. Right-size the progress UI accordingly (a
"Downloading mGB…" state that's allowed to complete instantly is fine; a precise
progress bar is optional polish). See Open decision #4.

## How it works today (starting point)

- mGB is `bin2c`-baked into a `mgb-rom` static lib
  ([CMakeLists.txt](../CMakeLists.txt) `mgb-rom` target) →
  `rp::embeddedRom()` ([EmbeddedRoms.hpp](../packages/native/src/EmbeddedRoms.hpp))
  → consumed by `SameBoyBackend` / `EngineRpcService`, and re-supplied on
  project load.
- The menu entry is one line:
  `action("start-mgb", "Load mGB (GB MIDI Synth)", () => …loadMgb())`
  ([menuDefs.ts:527](../packages/retroplug/ui/screens/menu/menuDefs.ts#L527)).
- **The control-plane JS context is sandboxed** — `TjsHostRuntime` exposes only
  `__rpcSend` to the bundle; there is no `fetch`/`http` in the TS control plane.
- **An HTTP client is already compiled in**: txiki's, backed by **libwebsockets**
  ([txiki/src/httpclient.c](../deps/dpf.js/deps/lv_binding_js/deps/txiki/src/httpclient.c)),
  surfaced as JS `fetch` in a *full* txiki context (not the control-plane one).
  No libcurl is linked.
- `configDir()` already resolves per-OS
  ([HostRpcService.cpp:21](../packages/native/src/host/rpc/HostRpcService.cpp#L21)):
  `%APPDATA%\RetroPlug`, `~/Library/Application Support/RetroPlug`,
  `$XDG_CONFIG_HOME/retroplug`.
- `RP_MGB_ROM_PATH` compile-def already points a test at
  `resources/roms/mGB.gb`.

## The core architectural fork: where does the download run?

The control plane can't fetch directly (sandboxed). Two viable paths:

- **A — Native RPC download.** New `downloadMgb`-style RPC; native does the HTTP
  GET on a background thread, reports progress via the existing observation
  pattern (atomics polled over RPC, like the audio driver). Keeps the control
  plane pure-RPC and robust, but the only compiled-in client is txiki's
  lws-based one (tied to txiki's event loop, awkward from plain C++), so this
  likely means pulling in a **platform-native** client (NSURLSession / WinHTTP /
  libcurl) — new per-OS code, and it does *not* "use txiki to download."
- **B — Expose txiki `fetch` to a JS context and download in TS** (writing to
  disk via the fs RPC). Matches the "txiki downloads it" intent, gives one HTTP
  path (lws) across all platforms, and progress is natural in JS. Cost:
  consciously wiring `fetch` into the (deliberately sandboxed) control-plane
  context or a small networking-enabled sub-context, plus streaming-to-file with
  progress.

**Recommendation: B** — matches intent, avoids three platform HTTP backends.
It means punching networking into the sandbox, which is acceptable (our own
trusted bundle). **See Open decision #1 — settle this first; it drives the rest.**

## Storage location (per-OS best practice)

A downloaded ROM is **data/cache, not config**:

| OS | Config (today, `configDir()`) | Data (ideal for mGB) |
|---|---|---|
| macOS | `~/Library/Application Support/RetroPlug` | same (fine) |
| Windows | `%APPDATA%` (roaming) | **`%LOCALAPPDATA%`** (don't roam a binary) |
| Linux | `$XDG_CONFIG_HOME` | **`$XDG_DATA_HOME`** (`~/.local/share`) |

Reusing `configDir()/roms/mGB.gb` is the least code; adding a proper `dataDir()`
sibling is more correct (Windows roaming / Linux XDG split). **Open decision #2.**

## Download source + integrity

- **Do not hotlink trash80's GitHub asset** — fragile (can move/404). Host a copy
  on RetroPlug's own release assets / a pinned URL under our control (the author's
  permission covers redistribution). **Open decision #3.**
- **Pin a SHA-256 in the binary and verify** post-download; reject + retry on
  mismatch. Covers corruption, tampering, and wrong-file. Pin a specific mGB
  build so behaviour is deterministic.

## Load-path refactor (the non-UI bulk of the work)

- `rp::embeddedRom()` → "read the resolved mGB file"; `SameBoyBackend` /
  `EngineRpcService` / `SameBoyConfig` supply bytes from that path instead of the
  baked array.
- Drop the `mgb-rom` bin2c target (and the mGB use of `EmbeddedRoms`).
- Project model: the `embeddedRom: "mgb"` marker stays, but on load it resolves
  to the downloaded file; if absent → **needs-download** state.
  [projectMissing.ts](../packages/retroplug/src/projectMissing.ts) currently
  treats `embeddedRom` as always-OK — that check becomes "the mGB file exists."
- Introduce a single **"resolve mGB path" seam**: (1) explicit override/env,
  (2) downloaded location, (3) needs-download. Keeps CLI/tests/dev working.

## Menu UX + reactivity

- Menu labels are **recomputed each render from stores**
  ([menuDefs.ts](../packages/retroplug/ui/screens/menu/menuDefs.ts) header
  comment), so a small `mgbDownload` store
  (`absent | downloading{pct} | ready | error`) drives the label directly.
- The item must be **`keepOpen: true`** while downloading (normal actions close
  the menu — you'd never see progress). Cyclers already use `keepOpen`.
- States:
  - absent → `Load mGB — Download`
  - downloading → `Downloading mGB… 42%`
  - ready → `Load mGB (GB MIDI Synth)` (as today)
  - error → `mGB download failed — Retry`
- Throttle progress → store updates, and mind the lv_binding_js re-render
  gotchas in [Menu.tsx](../packages/retroplug/ui/screens/menu/Menu.tsx) (re-keys
  on the visible set; frequent label churn is just Text updates but must not
  thrash focus).
- Progress transport depends on Open decision #1: native path = background thread
  + observation atomics polled by the control plane; txiki-fetch path = progress
  in JS updating the store directly.

## Cross-cutting: CLI / tests / headless

- CLI `loadMgb`, the author-mgb sessions, and native tests use mGB and **must not
  depend on the network**. Keep `resources/roms/mGB.gb` in the repo and have the
  "resolve mGB path" seam prefer it in dev/test/CLI (via `RP_MGB_ROM_PATH` / an
  env override). Only the **shipped GUI binary** drops the embed and downloads.
- Licensing net effect: the *distributed binary* no longer contains mGB (goal
  achieved); the repo keeps the file for dev/test (source-level aggregation,
  covered by the author's permission).

## Edge cases to handle

- Offline / download failure → error state + retry; a project referencing mGB
  while offline → graceful "needs download," not a crash.
- Concurrency: two systems both want mGB → single shared download.
- Cancel mid-download (user re-clicks)?
- Corruption recovery — driven by the SHA-256 check (re-download on mismatch).

## Open decisions (settle before/while implementing)

1. **Download layer: A (native RPC) or B (expose txiki `fetch` to JS)?**
   — biggest fork; recommendation is B.
2. **Storage: reuse `configDir()` or add a proper per-OS `dataDir()`?**
   — recommendation is a `dataDir()` for correctness (esp. Windows/Linux).
3. **Host mGB where** — RetroPlug release assets vs a dedicated pinned URL.
4. **Progress fidelity** — real percent vs an instant "Downloading…" flash
   (mGB is 64 KB).
5. Keep `resources/roms/mGB.gb` in-repo for dev/test — assumed **yes**.

## Rough implementation checklist

- [ ] Settle Open decisions #1–#5.
- [ ] Add the download mechanism (per #1) with SHA-256 verify.
- [ ] Add the storage path resolver (per #2) + "resolve mGB path" seam.
- [ ] Refactor `embeddedRom()`/`SameBoyBackend`/`EngineRpcService` to load from a
      file; drop the `mgb-rom` bin2c target.
- [ ] Update project load / `projectMissing` to the needs-download state.
- [ ] Add the `mgbDownload` store + wire the menu item states (`keepOpen`).
- [ ] Keep CLI/tests/headless working off the on-disk ROM.
- [ ] Verify the distributed binary no longer contains mGB bytes.
- [ ] Update THIRD-PARTY-NOTICES / packaging: mGB is no longer *in* the binary
      (adjust the notice from "bundled" to "downloaded at runtime").

# The package split

**Date:** 2026-11-07 · **Branch:** `feature/modularize` · **Status:** roadmap; F1, F3 and F2 built
([modular-graph-phase-f1-design.md](modular-graph-phase-f1-design.md), whose section 11 records it and its follow-up
pass; [modular-graph-phase-f3-design.md](modular-graph-phase-f3-design.md), whose section 12 records it;
[modular-graph-phase-f2-design.md](modular-graph-phase-f2-design.md); what each leaves is
[07-remaining-work.md](../spec/07-remaining-work.md)'s F1, F3 and F2 bullets), F4 not yet designed

[modular-graph-design.md](modular-graph-design.md) section 11 ("Package split") listed what carving RetroPlug into
workspace packages would take. E1 started it (`node-sdk`, `graph-runtime`, `core`, `dsp`), E2 carved `n8`, and E4 made
`bliptoaster` a device script pack. This document turns the rest of that list into four phases, with the order they run
in and the rules they share. Each phase is its own design, prototype, plan and execution cycle, as the A to E phases were.

## 1. Goal

Clean boundaries. Each cart's code lives in its own package, with its dependencies declared, so it can be tested alone and
read as the template a third-party pack would follow. Leaner products, per-product bundles and downloadable packs are
not the driver: they become cheap afterwards, and nothing here is judged by them.

## 2. The phases

- **F1: the TS package mechanism and the leaf TS moves.** A TS half for `core` and `dsp`; new packages `sameboy` and
  `mesen` (TS only), `tracker`, `testing`, `lsdj`, `mgb`, `risa`, `smsggdj`; `bliptoaster` and `n8` widened. The
  dependencies are the packages' `package.json` dependencies (what a package imports, which is less than the first design
  guessed: no cart depends on another, nor on a console package), checked in both languages against accident and drift.
  The registries that are pure data (roles, ROM providers, protocol-node tables, tracker integrations, kit formats) are
  generated from a product's package list, which is both the dependency order and the registration order. No behaviour
  changes: every baseline held unmodified.
- **F3: the Launchpad pack** ([design](modular-graph-phase-f3-design.md), **built** on `feature/modularize`; it was
  designed and prototyped on `proto/f3-launchpad` in five stages). A port, not a merge, of `feature/launchpad` (merge-base `cfeb80e00`, 2026-09-29, before the
  graph existed) into `packages/launchpad` (the two surface nodes, the protocol, the session, the port scan, the
  provider and the facet) and `packages/lsdj-launchpad` (the MI.MAP app, contributed through the new `controllerApps`
  kind), with the provider ABI at 2, `PROVIDERS` per product, `config.json`'s `controlSurface`, and the pack's portable
  core proved by the toolchain's scenario mode. What it leaves is owed rather than undone: the real-device checklist
  (the Pro MK3 and LSDj on a desk) and its Windows items, the plugin's control surface, and the Pico controller.
- **F2: the native carve** ([design](modular-graph-phase-f2-design.md), **built** on `feature/modularize`). `system`
  (the contract), `sameboy`, `mesen` and `host-io` (the host MIDI nodes) become packages with their
  native halves, `retroplug-core` dissolves, the engine names no console (a node carries its package's build function),
  the mGB ROM becomes a package's declared `embed`, txiki and efsw become the shell's opt-in libraries, and Mesen's
  consoles RetroPlug never runs are compiled out (`MESEN_ENABLE_SNES` / `-PCE` / `-WS`). The `[packages]` golden is the
  one re-record. What it leaves: `MemoryType`'s closed wire enum, the TS unions (F4), the Mesen library chosen per
  product, and the Windows run.
- **F4: the UI onto extensions.** A menu item model for the cart-specific menu code, `TRACKER_UI`, the LSDj HD player and
  the overlays move into their packs; the stores' deep imports of carts (`projectStore`, `render.ts`, `systemsStore`)
  go through the registrations; per-product bundles.

## 3. The order

F1, then F3, then F2, then F4. F1 gives `packages/launchpad` somewhere to be (a TS package, a script-pack home, the
boundary checks), so the port lands there directly rather than in `packages/retroplug` and moving again. F3 does not need F2:
it adds a pack beside the native carve, not inside it. F2 is the largest and the riskiest (Mesen is vendored and patched
in about twenty files), so it runs on a tree whose TS side is already settled. F4 needs F1's registrations and F2's
per-product lists, so it is last.

## 4. Rules for every phase

- **Boundaries are checked in both languages.** A pack's TS may import only its own tree, its declared
  `@retroplug/*` dependencies and its declared third-party dependencies; its native targets may link and include only its
  declared packages. A cycle is refused. The checks are tests, not review, and they guard against accident and drift, not a determined author (each one's header lists what
  it does not cover).
- **The one-way shell rule.** `packages/retroplug` and `packages/native` are shells: they may depend on every pack, and no
  pack depends on either. What a pack needs from the shell moves down into a pack or is passed in as a registration.
- **Each phase proves itself against the oracles that already exist** (parity, the kit and render baselines, the
  `buildGraph` goldens, the device images); a phase that changes none of the audio re-records none of them.

## 5. What the survey found already done

Section 11 listed two backlog items that the tree no longer needs:

- *LSDj kit DTOs out of `BackendTypes.hpp`*: gone with the native kit compiler (8aa4e7b51); the kit compiles in TS
  (`packages/retroplug/src/kit/compile.ts`).
- *The risa DMC codec's dependency on LSDj files*: `rp.dsp.dpcm` in `packages/dsp` includes nothing of LSDj's.

What was left of the TS kit side was a coupling in the other direction (`src/kit/compile.ts` imported
`../lsdj/rom/buildKit` and `../risa/rom/kit`), which F1 resolved with registered kit formats: the chain half of the driver
is `dsp`'s, each cart's format is its package's, and the shell keeps the door. Section 11 also gives
`menuDefs.ts` "240 cart-specific lines"; `ui/screens/menu/menuDefs.ts` is now 2532 lines with `TRACKER_UI` at line 2073, and the survey
counted about 650 cart-specific lines in it; F4 will count them afresh.

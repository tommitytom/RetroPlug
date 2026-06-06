# Step 22 — LSDJ legacy sav-format support (semantic decode below fmt 16)

**Status:** Not started.

## Goal

Correctly decode/encode the **semantic** field values of LSDJ savs from older
format versions (roughly fmt < 16, i.e. LSDJ ≲ 8.x down to the earliest), so
loading an old user sav (e.g. a v4-era file) shows the *right* instrument /
command / wave / synth values — not just a lossless byte round-trip. A lot of
people still use old LSDJ, so this is real.

## Depends on

- The LSDJ sav codec (done): `src/lsdj/codec/SongCodec.cpp` + the per-entity
  field maps already extracted from liblsdj (every version conditional is
  documented there).

## Background — why this is needed (and why round-trip isn't enough)

The corpus round-trip already passes byte-identically for **all** fmt2→fmt22
savs. But that only proves **losslessness**: `encode` is the exact inverse of
`decode`, so `encode(decode(x)) == x` regardless of whether the *interpretation*
is right. The current codec implements only the **modern** branch of each
version conditional (the `fmt >= N` side). For an older sav it round-trips
perfectly while reading fields through the wrong transform.

The version conditionals to add the `else` branches for (all documented in the
field maps / liblsdj source) include:

- **Phrase/table commands** — fmt < 8 stores the raw enum (no B-command remap);
  `decodeCommand`/`encodeCommand` currently assume the fmt ≥ 8 B↔1 remap.
- **Wave instrument** — `length` (fmt 7 / ==6 / <6 three-way), `speed` (same),
  `synth` (byte 3 nibble fmt ≥ 16 vs byte 2 nibble below), `playMode` (+1 rotate
  fmt ≥ 10), `loopPos`/`repeat` inversion (fmt ≥ 9).
- **Synth** — resonance start/end nibble location & writability (fmt ≥ 5).
- **ADSR** — `attackSpeed` read width (4-bit fmt ≥ 13 else 3-bit) — *already*
  handled; audit the rest.
- **Vibrato** — fmt < 4 shape/PLV-speed cross-coding with **unencodable
  (shape,speed) combinations** (the liblsdj setter returns false). The model
  must reject or normalize these on encode.
- **Region offsets** — empirically the layout is stable fmt2→fmt22 (rb markers),
  but verify the *earliest* formats; `Regions(fmt)` is the seam if any differ.
  Also note early LSDJ used a 32 KiB SRAM (the corpus has one such sav) — the
  SavCodec currently assumes 128 KiB; handle the smaller image.

## Architecture

- Fill in the `else` branches in `SongCodec` decode **and** encode (keep them
  exact mirrors so round-trip stays byte-identical), threading the already-known
  `FormatVersion`.
- Where a model value is unrepresentable on an old format (fmt < 4 vibrato),
  return `rfl::Error` from encode rather than silently truncating.
- Extend `Regions(fmt)` only if a format genuinely relocates (likely none in the
  common range; confirm the earliest).

## Tasks

1. Add the documented `else` branches to decode/encode, version by version,
   mirroring the field maps.
2. Handle the 32 KiB early-SRAM image in `SavCodec` (size-aware header/block
   layout).
3. **liblsdj differential test** — the key semantic oracle. liblsdj is *correct*
   for fmt ≤ 16, so compile it (vendored at `old/thirdparty/liblsdj`) into a test,
   decode old corpus savs with both, and compare field-by-field for the regions
   liblsdj reads correctly. (Do **not** treat liblsdj as ground truth for fmt22
   offsets, and skip its known bugs — e.g. the wave `is_default` `sizeof` bug and
   the command A/B on-disk collision — flagged in the field maps.)
4. Keep the existing byte-identical round-trip gate green across the whole corpus.

## Verification

- New liblsdj-differential Catch2 test: semantic fields match liblsdj for the
  fmt ≤ 16 corpus savs (header, song settings, instruments where liblsdj is
  correct).
- Existing `[lsdj-sav]` round-trip suite still byte-identical for all 549 corpus
  savs (no regression).
- Spot-check a known old sav (e.g. `lsdj4_*`) decodes to sensible instrument
  types/values.

## Risks / open questions

- **liblsdj's own bugs.** It's the oracle only where it's correct; the field
  maps already flagged specific liblsdj bugs not to replicate.
- **fmt < 4 unencodable combos.** Decide policy: reject on encode, or normalize
  to the nearest legal (shape,speed). Reject is safer.
- **How far back to go.** The shipped ROMs are fmt22/aboy; user savs could be
  anything. Prioritize the formats users actually have (the LSDJ archive the
  corpus was built from covers them); the rarest earliest formats can lag.

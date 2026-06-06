# Step 21 — LSDJ sav inspector (RPC + UI)

**Status:** Not started.

## Goal

Surface a parsed LSDJ `.sav` to the UI as structured data and render it: song
grid, chains/phrases, instruments, tables, synths/waves. The sav codec
(`src/lsdj/codec/`, built — see the design memory / commits `6208e905`..`097bb6c0`)
already turns SRAM bytes into a reflect-cpp model and JSON; this step exposes
that over rpcpp and builds the React view, typed by the generated `SavTypes.ts`
and validated by the generated `SavSchema.ts` (zod).

## Depends on

- [Step 11](./11-memory-snapshots.md) (memory snapshot API; the sav bytes come
  from `getMemory(MemoryType::Sram)` or `SameBoyConfig::sram`).
- The LSDJ sav codec (done): `decodeSav` / `savToJson`, and the
  `sav-regenerate` target (`tools/gen-sav-ts.js`) that emits
  `build/ui/generated/SavSchema.ts` + `SavTypes.ts`.
- [Step 12](./12-ts-extensions.md) is the natural home if this ships as a
  built-in TS extension rather than a core panel.
- **Independent of [Step 22](./22-lsdj-legacy-sav-formats.md)** — neither blocks
  the other. This step works on modern savs without 22; doing 22 first just
  means the inspector also reads pre-fmt-16 savs with correct values.

## Architecture introduced

- **rpcpp method `getSav(systemId) -> Sav`.** `PluginRpcService` DTOs are
  reflect-cpp structs, and `rp::lsdj::model::Sav` already is one — so it can be
  returned directly. The service decodes `SameBoySystem::saveSramBytes()` (or
  `getMemory(Sram)`) via `decodeSav`. **Size guard:** the full model is large
  (256 waves × 16B, 64 instruments, …); prefer either (a) returning the working
  song only by default with stored projects fetched on demand, or (b) a curated
  response DTO (instrument summaries, song grid) and a separate `getSavSong`
  for detail. Decide based on what the first view actually needs.
- **UI types from the codec.** Wire `sav-regenerate` into the UI build (or
  import its output) so the React side imports `SavTypes`/`SavSchema`. The
  `getSav` JSON validates against `SavSchema` on arrival in dev builds.
- **`SavInspector` React component** under `ui/` (or as a TS extension): renders
  the song grid + an instrument list, expandable to per-instrument detail.
- **Mode-dependent field display.** Resolve the raw-aliased codec fields here,
  where the loop/play mode is in hand: `wave.loopPos` (byte-2 nibble) shows as
  *loop position* or *repeat* (`0xF - loopPos`) per `playMode`; `kit.offset2`
  (byte 13) shows as *offset2* or *length2* per the kit's loop mode. The codec
  stores these raw (they round-trip exactly); the semantic label is a UI choice.

## Tasks

1. Add `getSav` (and/or `getSavSong`) to `src/PluginRpcService.{hpp,cpp}` +
   register in `PluginRpcRegistration.hpp`. Decode from live SRAM.
2. Decide and implement the response shape (full model vs curated + on-demand
   detail), mindful of payload size.
3. Wire `sav-regenerate` so the UI build has `SavTypes`/`SavSchema` available;
   validate `getSav` payloads against zod in dev.
4. Build the `SavInspector` React view: song grid + instrument list + detail.
5. Implement the mode-aware display of `loopPos`/`repeat` and `offset2`/`length2`.
6. (Optional) Live-update via the Step 11 subscription channel so the inspector
   reflects edits as LSDJ runs.

## Verification

- Load a real LSDJ sav (or boot one authored via `emu.savFromJson`), open the
  inspector: song grid, instruments, and a kit/wave instrument's mode-dependent
  fields render correctly.
- `getSav` payload validates against the generated `SavSchema` (zod) in a dev
  build with no errors.

## Risks / open questions

- **Payload size / cadence.** Don't live-stream the whole model at 60 Hz; fetch
  on open + targeted live subscriptions for the visible region.
- **Live vs torn reads.** Same torn-snapshot caveat as Step 11; decode a copied
  SRAM snapshot, not live memory.
- **Write-back (editing) is out of scope here** — this step is read/inspect.
  Editing a sav from the UI (encode + write SRAM) is a later step.

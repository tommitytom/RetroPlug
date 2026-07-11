# Design: a pure-TypeScript LSDj `.sav` codec

Status: **IMPLEMENTED** (pure-TS option, all formats fmt 0..22). The codec ships in
[packages/retroplug/src/lsdj/](../packages/retroplug/src/lsdj/) and the architecture is
documented in [spec/05-data-persistence.md](../spec/05-data-persistence.md). This document is
retained as the design rationale + the evidence behind the decision.

Owner decisions taken (resolving §10): **pure TS** is the destination; **all LSDj versions**
are supported (not descoped); the oracle is the revived liblsdj differential + frozen golden
vectors. The hybrid (native RLE) was evaluated and **not** adopted — pure-TS RLE is fast
enough and native would forfeit the environment-reach + SSOT wins (§6).

This explores replacing the C++/reflect-cpp LSDj `.sav` codec with a TypeScript
implementation, and evaluates the "keep the RLE/codec in C++, bulk in TS" hybrid the
brief asked for. The recommendation and the numbers behind it are below.

---

## 1. TL;DR

- **Recommendation: port the whole codec to pure TypeScript** (zod model + binary codec +
  RLE), making it the single source of truth. Retire the reflect-cpp model, the C++ codec,
  and the `savFromJson` RPC once a fidelity gate is green.
- **On the hybrid (C++ for RLE):** explored in depth and **not recommended as a *mandatory*
  seam**. The perf premise doesn't hold in this runtime, and a mandatory native RLE
  re-introduces the exact reach/maintenance problems the port is meant to remove. Instead,
  keep pure-TS RLE as the SSOT and leave the door open to an **optional, transparent native
  accelerator** behind a stable `Uint8Array → Uint8Array` interface — added *only if*
  profiling ever shows the rare bulk transcode hurts. In v1, don't build it.
- **WASM/WAMR hybrid: rejected.** txiki's WAMR is compiled interpreter-only, so a
  C→WASM codec runs interpreted too — no speed win — while adding a toolchain and a shipped
  `.wasm` bundle.
- **The real risk is not code, it's the test oracle.** Decode is dead, untested C++ today;
  there is no golden corpus. Any port must first stand up a ground-truth oracle
  (real LSDj-produced savs + a SameBoy boot check), not just diff against the existing C++.

---

## 2. What exists today

### 2.1 The codec is C++/reflect-cpp, and write-only from TS

The LSDj save model + codec live entirely in native C++ under
[packages/native/src/lsdj/](../packages/native/src/lsdj/):

- **Model** (reflect-cpp structs, the current SSOT): [model/Types.hpp](../packages/native/src/lsdj/model/Types.hpp),
  [model/Song.hpp](../packages/native/src/lsdj/model/Song.hpp),
  [model/Instrument.hpp](../packages/native/src/lsdj/model/Instrument.hpp),
  [model/Sav.hpp](../packages/native/src/lsdj/model/Sav.hpp),
  [model/FixedArray.hpp](../packages/native/src/lsdj/model/FixedArray.hpp).
- **Binary codec**: [codec/SongCodec.cpp](../packages/native/src/lsdj/codec/SongCodec.cpp) (562 LOC, the
  bulk), [codec/SavCodec.cpp](../packages/native/src/lsdj/codec/SavCodec.cpp),
  [codec/Compression.cpp](../packages/native/src/lsdj/codec/Compression.cpp) (150 LOC — the RLE),
  [codec/Regions.hpp](../packages/native/src/lsdj/codec/Regions.hpp) (offsets),
  [codec/SavView.hpp](../packages/native/src/lsdj/codec/SavView.hpp) (bit cursor).
- **JSON bridge**: [SavSerialization.hpp](../packages/native/src/lsdj/SavSerialization.hpp) (`rfl::json`).

The **only** thing crossing to TS is encode, via one RPC method:
`savFromJson(jsonString) → Uint8Array(128 KiB)`
([HostRpcService.cpp:168](../packages/native/src/host/rpc/HostRpcService.cpp#L168),
[BackendRpcRegistration.hpp:29](../packages/native/src/host/rpc/BackendRpcRegistration.hpp#L29)).
TS calls it through [lsdjSav.ts:22](../packages/retroplug/src/lsdjSav.ts#L22).

Critical facts, verified by grep + the research briefs:

- **Decode is dead code.** `decodeSav` / `decodeSong` / `savToJson` / `songToJson` have
  **zero callers** anywhere and are not wired over RPC. The codec is write-only from TS's
  perspective; decode has never been exercised or validated.
- **One production caller.** [dspRoles.ts:204](../packages/retroplug/src/dspRoles.ts#L204)
  calls `caps.savFromJson("{}")` to seed an empty sav at LSDj construct — once per system,
  on the main thread. The other 14 callers are `test-native/*.ts` + CLI fixtures.
- **TS has no typed model.** The `tools/gen-sav-ts.js` promised in
  [SavSerialization.hpp:15](../packages/native/src/lsdj/SavSerialization.hpp#L15) **does not
  exist** (a stale `sav-schema-dump` build artifact remains, but its source is gone).
  Authoring today is stringly-typed `JSON.stringify` against an invisible C++ shape.

### 2.2 Why the current bridge is "cumbersome"

1. No type safety in TS — a raw JSON string in, `Uint8Array` out; a typo or out-of-range
   value is caught only at runtime by native reflect-cpp, returned as an RPC error string.
2. The model lives in C++, far from the TS UI/persistence code that owns every *other*
   serialized root (project/config/bindings/recent) as zod. The sav is the one inversion of
   the repo's "TS owns the model, native owns the bytes" architecture (spec/05).
3. You need a running native host even to *encode* — TS can't produce a `.sav` without RPC.
4. No decode-to-TS at all, so no way to read a sav back into a model in TS.
5. Defaults (Groove 6/6, wave volume `0xA8`, tempo 128, formatVersion 22, …) live only in
   C++ struct initializers, invisible to a TS author.

---

## 3. Runtime reality (this decides the hybrid question)

- **JS engine = QuickJS-ng under txiki**: a bytecode **interpreter, no JIT**. Pure-JS byte
  loops over typed arrays are correct but pay per-access interpreter cost.
- **WAMR is compiled interpreter-only** (`WAMR_BUILD_AOT=0`, `WAMR_BUILD_JIT=0`,
  "interpreter only for maximum portability" in the txiki CMake). A C→WASM codec would
  therefore run interpreted *and* pay linear-memory marshalling → **no speed win**, plus an
  emscripten/wasi toolchain and a shipped `.wasm`.
- **`__rpcSend` is synchronous, in-process, main/control-plane thread** (never audio).
  `savFromJson` marshalling is ~1 string copy in + ~1 128 KiB `Uint8Array` copy out; the RLE
  itself runs at native speed. Boundary cost is not the bottleneck.
- **Four TS runtimes**, differing only in whether native is present:
  1. Plugin (native present), 2. native/CLI test host `retroplug-host` (native present),
  3. **pure-TS mock runner** `run-tests.mjs` on bare `tjs` (**no native** — `savFromJson` is
  stubbed to the 2 bytes `0x6a 0x6b`), 4. a **possible future web/emscripten** target
  (native unavailable; `origin/emscripten` is stale but shows past intent).

**Consequence:** the decisive axis is **environment reach + SSOT + testability**, not raw
codec speed. Only a pure-TS codec works uniformly across all four environments.

---

## 4. Performance — measured, not guessed

Faithful TS ports of the RLE decompress/compress and a representative song byte-walk were run
in the **real `retroplug-host` QuickJS runtime**, fed **real sav bytes** produced by the
native encoder. (Throwaway benchmark; not committed.)

| Operation | Pure-TS (QuickJS) | Native + RPC |
|---|---:|---:|
| Working-song **decode** (32 KB → model) | **1.42 ms** | — |
| Working-song **encode** (model → 32 KB) | **1.13 ms** | 0.25 ms¹ |
| RLE **decompress** one project | 1.75 ms | — |
| RLE **compress** one project | **3.67 ms** (priciest primitive) | — |
| Full-sav decode, 1 working + 3 projects | 9.6 ms | — |
| Full-sav encode, 1 working + 3 projects | 15.5 ms | 0.64 ms¹ |
| Full-sav decode, 1 + **32** projects (worst case) | **~100 ms** | ~few ms |
| Full-sav encode, 1 + 32 projects (worst case) | **~150 ms** | ~few ms |
| `JSON.parse` / `stringify` a working-song model | ~1.5–1.9 ms | — |

¹ `savFromJson` end-to-end incl. reflect-cpp parse + encode + compress + RPC marshalling.

**Interpretation:**

- Native is ~5–7× faster (the QuickJS no-JIT tax), but the boundary copies are cheap.
- **Every realistic hot path is working-song-scale: 1–4 ms.** Fine for main-thread /
  throttled-idle-tick use.
- The only slow case is the **whole 32-project transcode (~100–150 ms)** — but that is a
  user-driven **file load/save event, not a loop**, and it is **structurally avoidable**
  (decode the working song only for dirty checks; touch the RLE archive only on explicit
  save — see §7).
- The "used at high frequency" premise is weak against today's code (the only production call
  is a one-shot `"{}"` seed at construct). The frequency comes from a *future* feature (§7),
  which is working-song-scale.

Caveat: these measured the raw codec ports, **not** zod validation. `Sav.parse` (recursive
fixed-array padding × 32 projects) must be benchmarked before committing; the hot decode path
is kept zod-free (§6).

---

## 5. Options considered

Four architectures were steelmanned and then adversarially red-teamed. Scores 1–5 (5 best);
**perf is deliberately de-weighted** per §4.

| Axis (weight) | 1. Pure-TS | 2. Hybrid (native RLE) | 3. WASM/WAMR | 4. Optimized status quo |
|---|:--:|:--:|:--:|:--:|
| Perf in-QuickJS *(low)* | 3 | 4 | 2 | 5 |
| **Fidelity risk** *(high)* | 2 | 3 | 5 | 4 |
| **Maintainability / SSOT** *(high)* | 5 | 3 | 3 | 4 |
| **Environment reach** *(high)* | 5 | 3 | 3 | 2 |
| **Future-fit (editor/web)** *(high)* | 5 | 4 | 3 | 2 |
| Effort *(tiebreak)* | 2 (L) | 3 (M) | 1 (L+spike) | 4 (S–M) |

- **Option 3 (WASM) — out.** L-effort toolchain for zero benefit today; justified only by a
  web target that doesn't exist and a JIT/AOT WAMR that isn't configured. "One artifact, four
  runtimes" is really two builds (WAMR-WASI ≠ browser). Phase-0 (drag libc++ + reflect-cpp +
  C++ exceptions through wasi under `LIB_PTHREAD=0`) can *hard-fail*.
- **Option 4 (optimized status quo) — the conservative fallback.** Keep the tested C++ codec;
  generate zod from the structs; wire a `savToJson` decode RPC. Lowest risk/effort, but buys
  nothing for the mock runner or web, keeps LSDj the last stringly-typed holdout, and has a
  verified hole: reflect-cpp `to_schema` emits **no default values**, yet ~8 load-bearing
  defaults are the whole "author only the cells you set" contract — so the generator needs a
  second hand-maintained surface anyway.
- **Option 2 (hybrid, native RLE) — the brief's instinct; see §6.**
- **Option 1 (pure-TS) — winner.** It's the only option that pays down *all four* debts at
  once (typed model, live decode, a real mock runner, a credible web/editor future) with zero
  native dependency and one SSOT on the rails the repo already chose. Its single weak axis
  (fidelity) is one **every** option must spend on anyway (the oracle).

---

## 6. The seam recommendation (direct answer to "use C++ for the RLE?")

**Cut the seam in TypeScript, not in C++.** Ship pure-TS RLE as the sole SSOT; treat native
as an **optional, transparent accelerator — and don't build even that in v1.**

Why the *mandatory* native-RLE hybrid is not recommended, despite being the natural instinct:

- **The perf it buys is for the rare case only.** The RLE is on a hot path essentially never;
  the working-song hot path doesn't use it. Pure-TS RLE compress is 3.67 ms/project, so the
  full 32-project save is ~117 ms — on an *infrequent explicit save*, that's acceptable.
- **It re-creates the reach gap the port exists to remove.** A mandatory native primitive
  fails in the mock test tier and in any future web build — exactly where pure-TS is the win.
- **It doubles maintenance.** If web ever ships you must write TS-RLE anyway, at which point
  you're maintaining C++ RLE + TS RLE + a diff-harness between them. The native primitive
  becomes premature optimization.
- **The fidelity argument for keeping RLE in C++ is weaker than it looks** — the C++ RLE has
  no round-trip test today either, so "it's already tested" doesn't hold; both sides need the
  same new oracle.

The instinct isn't wrong about *isolating the fiddly RLE*, so the design keeps that option
open at zero cost: the codec is structured so the RLE sits behind an opaque
`Uint8Array → Uint8Array` block boundary. If profiling ever shows the bulk transcode hurts,
add a native fast-path there:

```ts
// codec/rle.ts — pure-TS is always the fallback SSOT
export function compress(block: Uint8Array, startBlock: number): Uint8Array;
export function decompress(src: Uint8Array, startBlock: number): Uint8Array;

// optional, later: transparent native accelerator over the existing sync __rpcSend
// nativeRleAvailable() gates it; pure-TS runs whenever native is absent (mock/web).
```

The public interface never changes whether or not the accelerator exists.

---

## 7. Where the "high frequency" actually comes from

Today's dirty-detection ([sramAutoSave.ts](../packages/retroplug/src/sramAutoSave.ts)) hashes
the **whole 128 KiB SRAM** (FNV-1a) live-vs-disk — no codec involved, and it already runs at
interactive frequency in pure TS without complaint. Its own comment flags the bug: LSDj
rewrites working RAM every frame, so whole-SRAM hashing yields false-dirty.

The fix — and the real recurring codec workload — is a **working-song decode (fixed `0x8000`
region) per system on a throttled idle UI-thread tick**, comparing semantically and
deliberately ignoring the RLE archive at `0x8200` (which changes only on explicit LSDj SAVE).
That's **1.4 ms**, zod-free, on a throttle → trivially affordable in pure TS. A full 32-project
decompress and an LSDj song editor are further-out ambitions (no editor exists in the UI
today); neither is a per-frame loop.

---

## 8. Model design (zod, from the reflect-cpp contract)

The repo uses zod ^4.4.3. The reflect-cpp JSON contract maps cleanly:

| reflect-cpp feature | zod equivalent |
|---|---|
| `rfl::TaggedUnion<"type", Pulse/Wave/Kit/Noise>` | `z.discriminatedUnion("type", [...])` |
| `rfl::Literal<"pulse">` arm tag | `type: z.literal("pulse")` |
| `rfl::Flatten<InstrCommon>` (inlined) | spread `{ ...instrCommonShape, ...armFields }` |
| `Nibble/U5/U3/U2` (`Validator<Maximum<N>>`) | `z.number().int().min(0).max(N)` (reject, not clamp) |
| enum serialized as **name string** | `z.enum(["None","Left","Right","LeftRight"])` (names, not values) |
| `std::optional<T>` → `null` | `elem.nullable().default(null)` |
| `FixedArray<T,N>` pad-short / error-if-`>N` | `z.array(elem).max(N).transform(pad-to-N-with-default)` |
| struct field defaults | `.default(v)` so `Schema.parse({})` yields a full default sav |
| `rfl::DefaultIfMissing` lenient parse | every field defaulted ⇒ `parse({})` is a full object |

The model is a **standalone module** (`packages/retroplug/src/lsdj/model.ts`), **not** a
`migrate.ts` persistence root — it transcodes binary and defers to LSDj's own on-disk
`formatVersion` (an explicit exception to the zod-migration rails in AGENTS.md / spec/05).
`KitInstrument` (song metadata) is **in scope**; the kit/sample *compiler* is not (§ scope).

**Scope boundary.** In scope: `model/*` + `codec/*` + `SavSerialization.hpp`. Out of scope:
the kit/sample pipeline (`Effects`, `KitUtil`, `KitCompiler`, `SampleCache`, `SampleUtil`,
`OffsetLookup`) — compiled-but-dead, no model dependency, heavy native deps
(r8brain/miniaudio/enkiTS). Only the word "kit" is shared.

---

## 9. Fidelity strategy (the #1 risk) and phased plan

**The oracle must anchor to ground truth, not to the existing C++ codec.** Diffing TS against
the C++ *encoder* clones any latent C++ bug into TS and stays green forever; round-trip
`decode(encode(x)) === x` is circular over the encoder's output image; and decode — the new
capability the whole effort delivers — has *no* ground-truth anchor unless we add one.

So the oracle has **two independent legs**:
- (a) **Encode**: byte-exact vs the current C++ `savFromJson` over a fixture matrix (frozen as
  golden `.bin`). Encode is the one direction we can trust the C++ for (it's the shipped path).
- (b) **Decode**: **real LSDj-produced savs** (SameBoy/hardware SAVE output — incl. ≥1 legacy
  `fmt<22` and ≥1 full 1+32-project archive), with **field-level assertions** on the stock
  `lsdj9_4_2.sav` + a **SameBoy boot/sync** check. This is the only thing that catches a bug
  symmetric across encode+decode.

Phased plan (oracle first; nothing ships until the gate is green):

- **Phase 0 — mint the oracle.** Golden encode `.bin` matrix + ground-truth decode fixtures +
  the differential harness as a `pnpm test` (mock) and `test:native` (boot) leg. Acceptance gate.
- **Phase 1 — `model.ts`** (zod SSOT) + a `fixed(elem,N)` recursive-pad parity test (`>N`
  throws; padding reproduces per-element defaults; cover `"{}"` and partial nested arrays).
- **Phase 2 — `bits.ts` + `rle.ts`.** BitReader/BitWriter with mandatory `& 0xFF` after every
  `~`/`<<` (the int32 hazard choke-point) + high-bit fuzz; port Compression's RLE.
- **Phase 3 — `song.ts` (the risk sink).** One named `quirks.test.ts` case per quirk, pinned to
  golden bytes *and* decode fixtures: fmt≥8 command-`B` insert/shift; transpose stored
  inverted; length `~x&0x3F` decode vs `&0x1F` encode; wave playMode `(raw±1)&3`; tempo
  `<40 ⇒ +256`; synth resonance nibble split; limit `0xF−x`; sparse `SyncMode`; the two
  1-byte-rotation `DEFAULT_INSTRUMENT` constants (pinned against a *decode* fixture).
- **Phase 4 — `sav.ts`.** Assemble working song + 32 RLE projects + header/alloc-table.
  **Hard gate: `encodeSav == golden` byte-for-byte; decode fixtures pass field-level + boot.**
- **Phase 5 — cutover.** Rewire `lsdjSav.ts` to local `encodeSav`/`decodeSav` (signatures
  unchanged → all 15 callers untouched); delete the mock `0x6a6b` stub; add a no-native
  mock-runner CI leg asserting the real codec matches golden.
- **Phase 6 — retire native.** Delete the `savFromJson` RPC + `lsdj/{model,codec,SavSerialization.hpp}`;
  keep the C++ differential outputs frozen as **permanent** golden vectors; drop the dead
  `gen-sav-ts.js` reference.
- **Phase 7 (product win).** Replace sramAutoSave's whole-SRAM FNV hash with a working-song
  `decodeSong(sram, 0)` semantic dirty check on a throttled tick (§7).

---

## 10. Open questions for the owner

1. **Do we need to import pre-existing / legacy user savs?** If a web editor or "open an old
   `.sav`" flow is real, `fmt<22` **decode** moves from "throw loudly, descoped" to in-scope
   (with its own ground-truth fixtures). This is the single biggest scope lever. Default
   assumption: **descope legacy decode, throw on `fmt<22`**.
2. **Can we get real LSDj-produced savs for the oracle?** (Hardware or emulator SAVE output —
   ideally one legacy + one full 32-project archive.) The decode fidelity story depends on a
   ground-truth oracle; if we can only get C++-encoder output, the decode guarantee weakens to
   semantic-only.
3. **Is pure-TS (Option 1) the destination, or is Option 4 the pragmatic pick this cycle?** If
   Phase 0 won't be funded now, Option 4 (typed model via codegen + a `savToJson` decode RPC)
   ships sooner with zero codec risk; the oracle work is identical, so it's not wasted.
4. **RLE non-canonicality tolerance:** LSDj's own compressor may make different-but-valid run
   choices than ours, so strict `encode(decode(real)) == real` can fail on the compressed
   region for reasons unrelated to correctness. Propose: gate the compressed-projects region on
   a **byte-stable fixpoint** (`decode(e)` ≡ `decode(real)` and `encode(decode(e)) == e`);
   reserve strict byte-identity for the uncompressed `0x8000` working-song region.

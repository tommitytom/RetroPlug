# 10 — Multi-channel audio output (per-console channel stems)

**Status: in progress — the host seam and the SameBoy GB tap are built (§10 steps 1–2); the CLI/plugin
exposure and NES work remain.** This doc designs outputting the *individual console sound channels*
of a single emulator instance instead of its mixed stereo — 8 outputs for a Game Boy (a stereo pair
per channel), and 5-plus mono channels / the hardware "stereo-mod" pins for an NES. It is a design
only; no code exists yet. It builds on [02-native-host.md](02-native-host.md) (the `Engine` /
`BlockRunner` / `AudioRouter` seam and the `BackendFacade` RPC surface it extends),
[05-data-persistence.md](05-data-persistence.md) (the project-level `audioRouting` setting it adds a
value to), and [09-cli-debugging.md](09-cli-debugging.md) (the CLI render + WAV path it extends).

The guiding constraint from the request: **make the single Game Boy → 8 outputs case excellent; do
not generalize** the per-channel path to many instances (the existing per-instance routing modes
already cover that). NES per-channel is a secondary, CLI-first target.

## 0. Thesis and feasibility

Multi-channel output is achievable, and the codebase is partly pre-scaffolded for it:

- The plugin already declares **8 outputs as four stereo pairs** (`Out 1..4 L/R`,
  [PluginDSP.cpp](../packages/native/plugin/PluginDSP.cpp) `initAudioPort` / `DISTRHO_PLUGIN_NUM_OUTPUTS = 8`).
- `AudioRouter::bus(slot, streamIndex)` already reserves a `streamIndex` argument with a comment
  pointing at "the 4 Game Boy channels" ([BlockRunner.hpp](../packages/native/src/system/BlockRunner.hpp)).
- There is **no realtime resampler** in the audio path — both cores render at the host sample rate
  (SameBoy `GB_set_sample_rate`, Mesen `audioCfg.SampleRate`; r8brain is LSDj-kit-only), so per-channel
  streams need no per-stream resampling.

Per-console feasibility (both facts verified against the vendored source):

| Console | What's possible | Emulator edit |
|---|---|---|
| **Game Boy** (SameBoy) | 8 out = a faithful **stereo pair per channel** (Pulse1/Pulse2/Wave/Noise), matching the user's highpass mode | **Small tap recommended** (1× CPU). A mod-free fallback exists (4 muted lockstep instances, 4× CPU) but cannot honour the *Remove DC Offset* highpass mode — see §3. |
| **NES** (Mesen) | 5+ mono stems, **or** the hardware stereo-mod pins (pulse \| TND) + expansion | **Small `NesSoundMixer` edit required** — no public per-channel audio getter exists. |

The verified crux facts:

- **SameBoy `render()`** ([apu.c:316-350](../deps/sameboy/Core/apu.c)) computes, per channel, a fully
  band-limited, NR51-panned + NR50-scaled `GB_sample_t channel_output` (via `band_limited_read`) and
  then does `output.left/right += channel_output.left/right`. The four discarded `channel_output`
  summands **are** the eight requested outputs, and they provably re-sum to `output`.
- **Mesen `NesSoundMixer::GetOutputVolume`** ([NesSoundMixer.cpp:179-191](../deps/mesen/Core/NES/NesSoundMixer.cpp))
  computes `squareVolume = f(Square1 + Square2)` and `tndVolume = g(DMC + 2.75·Triangle + 1.85·Noise)`
  as two **non-linear-DAC** terms, then adds the six expansion terms linearly and casts to `int16`.
  Those two terms are exactly the two 2A03 output pins the hardware "stereo mod" taps; the non-linear
  DACs over *group* sums are why the five individual channels **cannot** be linearly summed back.

## 1. The one host seam (shared by plugin and CLI)

A render unit may emit **N > 1 output streams**, described by a new backend query
`SystemBase::channelLayout()` → an ordered list of `{ name, width ∈ {mono, stereo} }`. The **default
is one stereo `"Mix"` stream** = today's behaviour, so every existing backend is unchanged and the
default path stays byte-identical.

- **Game Boy**: 4 × stereo streams (Pulse1, Pulse2, Wave, Noise, in `GB_channel_t` order) = 8 lanes.
- **NES**: mono streams; a sub-mode selects *5 individual channels (+ expansion, see §4)* or *the 2–3
  stereo-mod pins*.

`finishBlock` gains a **lane count**: `finishBlock(info, float* const* outs, size_t laneCount)`
([SystemBase.hpp](../packages/native/src/system)). `BlockRunner::runUnit` sizes the flat `outs` array
from `router.streamCount(slot)` (a new `AudioRouter` method, default `1`), so a wide `outs` is
**structurally impossible** on the default path.

### The load-bearing correctness rule (do not skip)

**The split must be router/mode-driven, never an intrinsic always-on system trait.** `channelLayout()`
is *inert* unless the `Engine` builds a split router. If a system emitted 4 streams into a normal
router — which returns the *same* pair for every `streamIndex` ([BlockRunner.hpp](../packages/native/src/system/BlockRunner.hpp)) —
`finishBlock` would sum four *pre-highpass* stems into pair 0 and silently replace the shipped
highpassed, gain-smoothed mix. So the authority is the `Engine`-built router, and the backend branches
on `laneCount` (`≤2` → today's mixed path; `≥8` → per-channel). Add an assert that `laneCount` matches
the router width.

Two placement layers, mutually exclusive in the 8-lane budget:

1. **Per-system** (today: `AudioRouting::Stereo / TwoPerInstance / OnePerInstance`,
   [AudioRouting.hpp](../packages/native/src/system/AudioRouting.hpp)) — fans *many* systems across the
   8 fixed lanes. `streamCount(slot) = 1` always.
2. **Per-channel** (new: `ChannelSplit`) — fans *one* system's streams across lanes. Two realizations:
   - **Plugin**: a fixed, GB-shaped `ChannelSplitRouter` mapping stream *k* → stereo pair *k* (8 lanes).
     GB-only, gated to `systemCount() == 1`.
   - **CLI**: a width-flexible `PerChannelRouter` that hands each stream its own buffer (the system
     decides count/width) — serves GB 4-stereo *and* NES 5-mono / pins.

`ChannelSplit` is a 4th `AudioRouting` value gated to `systemCount() == 1`, **not** a combination with
the per-instance modes. Do not generalize the per-channel router to many systems.

## 2. Game Boy design (the primary case: 1 GB → 8 outputs)

8 outputs = a faithful **stereo pair per channel**.

**Panning is faithful, not mono-duplicated.** Each pair is `{ L = the channel's sample if
NR51-panned-left else 0, R = if panned-right else 0 } × NR50 side volume` — exactly what
`band_limited[i]` already holds (`update_sample`, [apu.c](../deps/sameboy/Core/apu.c) DMG/CGB path).
This preserves LSDj's hard per-channel L/R/center panning, which is musically load-bearing.
**Consequence:** an 8-mono export of a hard-panned channel yields one silent file — faithful, not a
bug (documented in §5, decision-locked).

**Highpass reflects the user's system-menu option (decision).** RetroPlug already exposes the SameBoy
highpass / DC mode in the system menu (Off / Accurate / *Remove DC Offset*,
[SameBoySystem.cpp](../packages/native/src/system/sameboy/SameBoySystem.cpp)). The per-channel stems
are highpassed **per-stem using the same mode the user selected**, so each channel output is filtered
the way the main output is. The recommended implementation applies the active mode per-stem inside the
SameBoy tap (4 extra `highpass_diff` states; for *Remove DC Offset*, a per-stem DC target computed
over that one channel), exposing *post-highpass* per-channel samples. Because the accurate highpass is
a linear leaky integrator, per-stem filtering still sums to the highpassed mix; *Remove DC Offset*
applied per-stem is the correct per-channel behaviour (each stem removes its own DC) but will not
bit-exact-sum to the mix's *Remove DC Offset* output — acceptable and expected for isolated stems.

### Extraction — is the SameBoy patch necessary?

No, not strictly — but it is **recommended**, and decision (1) makes it the clear choice.

- **Recommended — a small tracked SameBoy edit (1× CPU, bit-aligned).** `render()` already computes
  the four `channel_output` pairs ([apu.c:346-349](../deps/sameboy/Core/apu.c)). The edit stashes them
  (and applies the active highpass mode per-stem, per the decision above) and adds a public accessor
  (`GB_get_channel_sample`, or a parallel per-channel sample callback alongside
  `GB_apu_set_sample_callback`). Cost: a few extra `GB_sample_t` + highpass states, effectively-free
  CPU. `SameBoySystem::writeAudioSample` reads all four stems in the *same* `render()` tick
  (time-aligned) into four per-channel accum buffers, **alongside** the existing mixed sample (neither
  reconstructs the other: stems are per-stem-filtered, the mix is the summed-then-filtered output).
- **Mod-free fallback — 4 muted lockstep instances (4× CPU).** Run four SameBoy instances fed
  byte-identical input, each muting three of four channels via the public `GB_set_channel_muted`
  ([apu.h:220](../deps/sameboy/Core/apu.h)); muting zeroes a channel's band-limited output without
  perturbing envelopes/timers, so the cores stay in lockstep and each emits one channel's stereo pair.
  **Limitation that makes it lose to the patch under decision (1):** the *Remove DC Offset* highpass
  mode computes its DC target across **all DAC-enabled channels ignoring mute**
  ([apu.c:376-394](../deps/sameboy/Core/apu.c)), so a muted instance removes the wrong DC from an
  isolated channel — that specific user-selectable mode cannot be honoured mod-free. Plus 4× CPU/memory
  and a lockstep input fan-out.
- **Rejected — `GB_get_channel_amplitude`** ([apu.h:231](../deps/sameboy/Core/apu.h)): a raw
  instantaneous 0–15 DAC index, pre-band-limit / pre-pan / pre-highpass. It aliases and will not
  reconstruct the mix — fine for meters/scopes, unusable as an audio stem source.

SameBoy is a **pinned vendored submodule**, so the tap ships as a tracked patch
(`cmake/patches/sameboy-per-channel-audio.patch`) applied idempotently at configure by
`cmake/sameboy.cmake` — no submodule-pointer bump, durable on a fresh clone, and a future bump that
invalidates the patch fails loudly at configure rather than silently dropping per-channel output
(resolved, §8/§9).

## 3. NES design (Mesen) — secondary, CLI-first

There is no public per-channel audio getter (the per-channel arrays, `GetChannelOutput`, and
`EndFrame` are private, and the audio device only receives the post-mix stereo). A **small
`NesSoundMixer` edit** is required for any faithful single-pass tap. The data is already separable:
every channel funnels signed deltas keyed by `AudioChannel` ([NesTypes.h:449](../deps/mesen/Core/NES/NesTypes.h))
into `AddDelta`; the edit emits extra per-**stream** blip buffers alongside the existing mix. Each
exposed stream needs its **own** `{ blip buffer, previous-output accumulator, blip_end_frame }`
(`EndFrame` tracks a single one today), reset each frame, all band-limiting at the same fixed rate so
they stay phase/rate-aligned with the mix. It stays behind a flag; the default mixed output is
untouched. Mod-free fallback = N-pass solo via the public `NesConfig.ChannelVolumes[]` (Nx cost, still
non-summing, offline-only) — recommend the edit instead.

NES is **CLI-only in v1**: five mono streams cannot map onto the plugin's fixed four *stereo* pairs
without more than 8 compile-time ports. NES-in-plugin (mono-lane packing) is a deferred follow-on
(§8).

### 3a. Stereo-mod mode (the flagship faithful NES output)

Model the real hardware mod: the 2A03 emits on two pins with separate non-linear DACs — pin1 = the two
pulse channels jointly, pin2 = TND (Triangle+Noise+DMC) jointly. Mesen already computes these as
`squareVolume` and `tndVolume` ([NesSoundMixer.cpp:182-183](../deps/mesen/Core/NES/NesSoundMixer.cpp)).
Emit them as two mono streams; because they are the exact terms summed for the mono mix, `pulse + TND`
reproduces the mix. Emit at the same scale/rounding as the mix path (mind the `*4` headroom and the
`int16` clamp) and avoid the cosmetic stereo/panning post-effects. An optional 0..1 "separation" blend
param mirrors the mod's stereo pot (open decision, §8).

- **Bit-exactness:** exact for a plain (non-expansion) ROM (`squareVolume` + `tndVolume` are `uint16`);
  with expansion present, an independently-truncated expansion stream re-sums within ~1 LSB (the six
  expansion terms are doubles truncated once in the combined `int16` cast).
- **Expansion in stereo-mod mode = a single lumped channel (decision).** When an expansion mapper is
  active, sum the six per-chip expansion terms (Mesen's coefficients: FDS×20, MMC5×43, N163×20, S5B×15,
  VRC6×5, VRC7×1) into **one** additional mono stream. Expansion mixes linearly on the cartridge side,
  so summing is correct here. Gate on a mapper with expansion audio; silent (absent) for a plain ROM.
  So stereo-mod mode = **2 outs** (pulse | TND), or **3** (+ lumped expansion).

### 3b. Multiple-mono mode (each channel individually)

Output every channel on its own mono stream:

- The **5 core channels** (Pulse1, Pulse2, Triangle, Noise, DMC) — from the mixer's per-channel deltas.
  These are honest **pre-DAC linear levels** and are labelled **"does not sum"** (the non-linear DACs
  over group sums mean channels sharing a pin cross-compress — a loud DMC ducks triangle/noise; SMB
  exploits this). Good for isolation/analysis, not a bit-exact decomposition.
- **Each individual expansion sub-channel (decision — "a lot for VRC7 but that's ok").** Mesen's
  `AudioChannel` enum is **per-chip** (`VRC6`, `VRC7`, … are single entries), so the individual
  expansion voices (VRC6 = 2 pulse + 1 saw; VRC7 = 6 FM; N163 = up to 8; MMC5 = 2 square + PCM; FDS = 1;
  S5B = 3) live *inside* the mapper audio classes ([Vrc6Pulse.h](../deps/mesen/Core/NES/Mappers/Audio/Vrc6Pulse.h) /
  [Vrc6Saw.h](../deps/mesen/Core/NES/Mappers/Audio/Vrc6Saw.h) / [Vrc7Audio.h](../deps/mesen/Core/NES/Mappers/Audio/Vrc7Audio.h) /
  …), before they sum into the chip's `AudioChannel` delta. Multiple-mono mode therefore requires a
  **per-mapper audio tap** (deeper than the mixer edit) to surface each sub-channel. This is the
  higher-effort part of the NES work; because it is CLI-only, an arbitrary channel count (e.g. 5 + 6 =
  11 for a VRC7 ROM) is fine — the exact tap point per expansion chip is worked out at implementation
  time. EPSM is a separate `NesConfig.EpsmVolume` path outside the `AudioChannel` enum (open decision).

## 4. Plugin output option

One new project-level `AudioRouting` value, **GB-scoped, single-system** — not over-generalized.

- **Native enum:** `AudioRouting::ChannelSplit = 3` ([AudioRouting.hpp](../packages/native/src/system/AudioRouting.hpp)).
- **Router:** a fixed GB-shaped `ChannelSplitRouter` returning `streamCount(slot 0) =
  channelLayout().size()` (4 for GB) and `bus(0, k) → { lane 2k, lane 2k+1 }` — GB channel *k* → plugin
  stereo pair *k* over the existing 8 DPF lanes. `Engine::processBlock` already zeroes all 8 lanes
  before routing, so unused lanes are silent for free.
- **Gating (authority in native).** The `Engine` builds `ChannelSplitRouter` **only** when
  `audioRouting_ == ChannelSplit && systemCount() == 1` (and unlinked — link groups round-robin
  multiple GBs into their own buses); otherwise it builds the normal `MultiOutRouter` and the layout is
  inert. A multi-instance project can never mis-route. The TS UI mirrors this gating purely for UX
  (hide/disable the row when > 1 system).
- **RPC/command:** widen the guard `mode > 2` → `mode > 3` in
  `EngineRpcService::setAudioRouting` ([EngineRpcService.cpp](../packages/native/src/host/rpc/EngineRpcService.cpp));
  the `SetAudioRouting` `DspCommand` path is unchanged.
- **Port labels: generic (decision).** Keep the static `Out 1..4 L/R` labels with a documented mapping
  (Out1 = Pulse1, Out2 = Pulse2, Out3 = Wave, Out4 = Noise). Mode-aware relabeling is a deferred
  follow-on.
- **Scope: GB only in v1.** The NES sub-mode belongs in the per-system role-config if/when NES-in-plugin
  ships — not in the project-level enum.

## 5. CLI output option

One native pull-path RPC feeds all three export shapes; the WAV writer needs no structural change.

- **Shared prerequisite:** the CLI leg is *not* decoupled from the plugin — both ride the §1 seam
  (`channelLayout()`, the widened `finishBlock`, the `runUnit` stream loop, `AudioRouter::streamCount`)
  plus the SameBoy/Mesen taps. `renderAudioPerChannel` is a thin RPC over that seam.
- **RPC:** `EngineRpcService::renderAudioPerChannel(id, ms)`, modelled line-for-line on the existing
  `renderAudioPerSystem`: allocate per-**stream** block buffers, drive the same `processBlock` loop
  through a width-flexible `PerChannelRouter` (sibling of `PerSystemRouter`) via a new
  `Engine::processBlockPerChannel` (sibling of `processBlockPerSystem`), and return
  `std::vector<rfl::Bytestring>` — GB: 4 stereo-interleaved buffers (the source of truth); NES: 5+ mono
  (stems) or 2–3 mono (pins + lumped expansion). Use the **synchronous control-thread pull** path (like
  `renderAudio`), not the free-running audio thread, so it stays deterministic for tests. Wire it the
  standard way (EngineRpcService decl/impl → `BackendFacade` passthrough → one
  `server.addMethod<…>()` → an `audioDriver.ts` wrapper returning `Float32Array[]`, mirroring
  `renderAudioPerSystem`).
- **Three shapes — pure TS over the buffer set.** `encodeWav` ([wav.ts](../packages/retroplug/src))
  already derives `NumChannels`/`ByteRate`/`BlockAlign` from a `channels` arg, so N-channel output
  works with no writer change:
  - **(a) one multichannel WAV** — interleave the N streams → `encodeWav(pcm, sr, N)`. GB N=8; NES
    N=5+ (or 2–3 pins).
  - **(b) individual stereo WAVs (4 for GB)** — each GB stream is already stereo → `encodeWav(buf, sr,
    2)` → 4 files (Pulse1/Pulse2/Wave/Noise).
  - **(c) individual mono WAVs (8 for GB)** — deinterleave each GB stereo buffer into L,R →
    `encodeWav(mono, sr, 1)` → 8 files.

  GB-as-4-stereo-buffers is the source of truth; (a)/(c) derive by trivial TS interleave/deinterleave.
  NES stereo-mod pin grouping must come from native (the non-linear DAC cannot be recomposed in TS).
- **Format: 16-bit PCM, positional channels (decision).** No `WAVE_FORMAT_EXTENSIBLE` channel mask.
  Document the positional channel order and that hard-panned GB channels produce silent mono files.
- **Correctness threading:** pass the real engine `sampleRate()` into `encodeWav` (existing callers rely
  on the 44100 default and are latently mislabeled); add a round-trip test that writes + re-reads an
  8-channel file before relying on > 2-channel output (untested today); `renderTimeline`
  ([timeline.ts](../packages/retroplug/src)) needs a parallel per-stream concat for timed renders.

## 6. Data flow

`CORE TAP → BACKEND CAPTURE → RUNNER (stream loop) → ROUTER → SINK`, all at the host sample rate.

1. **Core tap** — GB: SameBoy `render()`'s four per-stem (post-per-stem-highpass) pairs, read in the
   same tick into four accum buffers alongside the mixed sample. NES: `NesSoundMixer::EndFrame`'s
   per-stream blip buffers.
2. **Backend `finishBlock(info, outs, laneCount)`** — sums into caller-zeroed buses; `laneCount ≤ 2` →
   today's mixed stereo (byte-identical), `> 2` → the per-channel stems. `gainSmoother_.next()` is
   called **once per frame** and the same gain applied across all lanes (a per-channel `next()` would
   advance the ramp 4× too fast).
3. **Runner `runUnit`** — reads `router.streamCount(slot)`, fetches that many buses via `bus(slot, k)`,
   packs a flat `outs` of `2 × streamCount` lanes, calls `finishBlock` with `laneCount`. Default
   `streamCount 1` → `outs[2]` → unchanged; non-split routers can never produce a wide `outs`.
4. **Router** — plugin: `ChannelSplitRouter` (Engine-built only under `ChannelSplit && systemCount()==1`);
   CLI: width-flexible `PerChannelRouter`.
5. **Sink** — plugin: DPF's 8 output pointers (master gain looped across all 8); CLI:
   `renderAudioPerChannel` → `Float32Array[]` → `encodeWav` in the three shapes.

## 7. Persistence & config

TS-owned **project setting** (not per-system role-config), consistent with the existing `audioRouting`:

- `projectConfig.ts` — widen the `audioRouting` clamp `clampedInt(0, 2, 0)` → `clampedInt(0, 3, 0)`.
- `projectStore.ts` — `SETTING_MAX.audioRouting` `2` → `3`; the `setAudioRouting → pushAudioRouting →
  backend.setAudioRouting` flow is reused as-is.
- `menuDefs.ts` — add an `AUDIO_ROUTING_NAMES` entry (e.g. `"Channels (1 GB)"`); the cycler is unchanged.
  The UI hides/disables the row when `> 1` system.
- Native — `AudioRouting::ChannelSplit = 3` + the widened RPC guard.

**Migration:** additive (a new enum value + a widened clamp) — an older project simply never carried
value 3 and zod `.default()`/clamp fills it, so **no** raw `migrate.ts` step and **no** version bump
are needed (per the additive rule in [05-data-persistence.md](05-data-persistence.md) / CLAUDE.md). A
raw step would be required only if some serialized root shape became non-additive — not the case here.

Any **future** NES sub-mode (pins vs 5-mono vs +expansion) belongs in the per-system role-config (the
reflect-cpp `DefaultIfMissing`-tolerant config that crosses to native), **not** the project-level enum
— and only once NES-in-plugin actually ships. In v1 the project-level enum stays a single value.

## 8. Risks & open questions

- **Structural (resolved by design):** the split is router/mode-driven and `finishBlock` is
  lane-count-branched, so a wide `outs` cannot reach a backend on the default path; the fidelity
  invariant is `Sum(GB stems) == a same-mode reference` (per-stem-highpassed), never a naive equality
  against the mixed `renderAudio()`.
- **NES non-summing (labelled):** the 5 individual channels do not sum back (non-linear DACs); only the
  stereo-mod pins sum. NES-in-plugin is deferred (mono streams can't fill the stereo-pair layout).
- **NES expansion sub-channels (higher effort):** multiple-mono mode's individual expansion voices need
  per-mapper audio taps beyond the mixer's per-chip `AudioChannel`; exact tap points per chip are an
  implementation-time task. EPSM lives outside the enum.
- **SameBoy pin (resolved):** the tap is a tracked patch reapplied at configure (§8/§9); a submodule
  bump that invalidates it fails loudly at configure instead of silently dropping per-channel output.
  `OfflineRender` per-unit buffer disjointness must be preserved for the new per-stream buffers.
- **WAV:** > 2-channel `encodeWav` is real but untested — needs a round-trip test; thread the real
  sample rate; hard-panned GB channels yield silent mono files (faithful, documented).

**Open decisions still to make** (the rest were locked, §9):

- NES stereo-mod "separation" pot: expose an automatable 0..1 pulse/TND blend, or ship a fixed hard
  pin-split for v1.
- NES EPSM: fold into the lumped expansion out, or expose separately.

## 9. Locked decisions (from review)

1. **GB highpass:** apply the highpass **per-stem, reflecting the user's system-menu option** (Off /
   Accurate / Remove DC Offset) — §2.
2. **SameBoy patch:** not strictly necessary (a mod-free 4-instance fallback exists), but **recommended**
   — it is 1× CPU and, unlike the fallback, can honour the Remove DC Offset mode from decision 1 — §2.
   **Tracking (resolved): a tracked patch (`cmake/patches/sameboy-per-channel-audio.patch`) applied
   idempotently at configure by `cmake/sameboy.cmake`** — no submodule-pointer bump, durable on fresh
   clone, loud configure-time failure if a bump invalidates it.
3. **NES in plugin:** **deferred** — NES per-channel is CLI-only in v1 — §3/§4.
4. **NES 5-mono stems:** raw pre-DAC linear levels with an explicit **"does not sum"** label — §3b.
5. **Plugin port labels:** **generic** `Out 1..4 L/R` + a documented mapping — §4.
6. **WAV format:** **16-bit PCM**, positional channels — §5.
7. **NES expansion:** **stereo-mod mode → one lumped expansion channel; multiple-mono mode → each
   individual expansion sub-channel** (accepting the VRC7-scale channel count) — §3a/§3b.

## 10. Phased build order (steps 1–2 built)

1. **Host seam only** — `channelLayout()` (default 1 stereo stream), widened `finishBlock(…, laneCount)`,
   `AudioRouter::streamCount`, `runUnit` stream loop, `AudioRouting::ChannelSplit` + the GB
   `ChannelSplitRouter` (plugin) + `PerChannelRouter` + `Engine::processBlockPerChannel` (CLI). No
   emulator change, no behaviour change. Prove the default path byte-identical (existing tests +
   `screenshot` still green; assert `laneCount` matches router width).
2. **SameBoy GB tap — DONE.** The tracked `apu.c`/`apu.h` patch adds a per-channel sample callback that
   emits the four `channel_output` stems, each highpassed per-stem in the active mode via its own
   `per_channel_highpass_diff[4]` (the mixed bus is untouched → byte-identical). `SameBoySystem`
   registers it, captures the four stems alongside the mix, reports a 4-stereo-stream `channelLayout()`,
   and `finishBlock` fans them into 8 lanes when `laneCount ≥ 8` (one gain per frame across all lanes).
   Guarded by `retroplug-audio-test` (`SameBoyStems.test.cpp`): `Sum(4 stems) == mix` per-sample on a
   real mGB boot render (Off + Accurate), plus the golden mGB mix render staying byte-identical.
3. **CLI GB export** — `renderAudioPerChannel` RPC + wiring; a CLI session emitting the 3 shapes; the
   8-channel WAV round-trip test; thread the real sample rate.
4. **Plugin GB option** — surface `ChannelSplit` (the §7 TS/native widenings + `systemCount()==1`
   gating). Verify a single GB → 8 outputs headlessly in a host (via `reaper:editor` / a render).
5. **NES tap** — the `NesSoundMixer` edit emitting the 5 core stems + the 2–3 pin/expansion group terms
   into per-stream blip buffers; expose via `renderAudioPerChannel` (CLI). Pin-mode fidelity check.
6. **NES stereo-mod grouping in the CLI** — pulse | TND (+ lumped expansion) as the flagship faithful
   mode; the 5-mono (+ per-mapper expansion sub-channel) mode as the secondary "does not sum" mode.
7. **Follow-ons** (decisions permitting) — upstream-vs-diff for the SameBoy patch; NES-in-plugin
   (mono-lane packing); the NES separation pot; EPSM; mode-aware plugin port relabeling.

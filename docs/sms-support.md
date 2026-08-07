# Adding Sega Master System / Game Gear via the vendored Mesen core

**Scoping document: prospective work, none of it implemented.** Third pass. Two decisions from the repo owner are folded in as settled (two sibling platforms; sample-accurate stepping is mandatory), and every claim from the previous pass that an empirical probe overturned is corrected in place rather than left standing.

---

## 1. Bottom line

**Tier 1 (boots, renders, makes sound, takes input, and syncs sample-accurately) is 8 to 10 days, not the 4 to 5 the previous pass estimated. Tier 1 + Tier 2 is three to four weeks of calendar. Tier 3 (stems, roles, tracker) is a quarter, and most of it is optional.**

The increase over the previous pass is entirely the sample-accurate sync requirement, and it splits three ways: the input transport does not exist in stock Mesen and needs a vendored edit that the previous design missed (section 2.4), the guard that would prove sample accuracy cannot be a clone of any existing test (section 2.8), and the teardown use-after-free the previous pass found still has to be fixed (Tier 1 item 8).

The big lever is unchanged: you already paid for the core. `deps/mesen/CMakeLists.txt:9-11` globs `Core/*.cpp`, so all 17 `Core/SMS/**` TUs compile today, and `Emulator.cpp:570` calls `TryLoadRom<SmsConsole>` unconditionally so none of it is dead-stripped: `nm -C build/bin/retroplug | grep -c Sms` returns exactly **873**. Adding SMS does not add that code; it makes shipped code reachable.

What is **no longer** true is that `MesenGbaSystem.{hpp,cpp}` is a near-verbatim template. Its boilerplate still is (construct, ROM load, framebuffer, savestate, RAM access, audio routing, `SystemFactory` wiring), but its `stepIfBelowTarget` is not, and that is the one function this project exists to get right.

### Decisions taken (no longer open)

**D1. Two platforms: `"sms"` and `"gg"` as siblings, not one platform with a model knob.** Rationale, all verified: GG differs in visible geometry (`GameGearOverscan` must be `{48,48,48,48}` or the 160x144 image floats in a 256x240 frame, section 4), in region handling (a separate `GameGearRegion` field at `SmsConsole.cpp:185-189`), in stereo (`GameGearPanningReg` hard-pans PSG voices, `SmsPsg.cpp:76-91`), and in the sync transport itself (`$DD` controller port versus the `$01` EXT port, and the two need different vendored edits). The model is selected by the filename extension you synthesize anyway (`SmsConsole.cpp:46-59`), so two `Platform` members cost nothing in the config schema and keep a machine-identity decision out of a settings cycler. Consequences: `RomFormat::Sms` **and** `RomFormat::Gg`, two `MesenBackend` branches, two `DEFAULT_CORE` rows, two `platformSchema` members, two overscan configurations, two test ROMs. All of these are folded into the Tier 1 table below as settled work.

**D2. Sample-accurate stepping is mandatory.** RetroPlug exists to provide sample-accurate DAW sync. SameBoy steps one SM83 instruction at a time (`SameBoySystem.cpp:690`); NES steps one 6502 instruction at a time (`MesenNesSystem.cpp:370`); GBA steps one video frame at a time (`MesenGbaSystem.cpp:262-264`) and gets away with it only because nothing syncs to GBA. smsggdj has a real sync contract (`/workspaces/smsggdj/GGSYNC.md`, `src/engine.asm:555-591`), so SMS/GG must match SameBoy and NES. **The previous pass's Tier 1 item 2 recommendation to clone the GBA `RunFrame()` shape is rejected.** Its stated premise was correct and its conclusion was not: `SmsPsg::Run()` (`deps/mesen/Core/SMS/SmsPsg.cpp:60-62`) is a lazy catch-up keyed on `_console->GetMasterClock()`, which is just the Z80 `CycleCount` (`SmsConsole.cpp:249-252`), so the host can pull it forward at any instant. Section 2 is the design.

### Three things that push Tier 1 past a weekend, from the previous pass, all still true

**(a) SMS teardown is a use-after-free that segfaults roughly half the time.** `SmsFmAudio` is constructed unconditionally for every SMS/GG cart (`SmsConsole.cpp:66`) and registers with the SoundMixer in its constructor (`SmsFmAudio.cpp:19`); its destructor calls back into the mixer (`SmsFmAudio.cpp:22-26`). `Emulator.h:66` declares `_console`, `Emulator.h:77` declares `_soundMixer`, members destruct in reverse declaration order, and `Emulator::~Emulator()` is empty (`Emulator.cpp:82-84`). Verified twice independently: 10/20 runs in the first pass, and 3/5 runs of 40 boot/destroy cycles in the probe pass, both with the stack `SoundMixer::UnregisterAudioProvider` <- `SmsFmAudio::~SmsFmAudio` <- `SmsConsole::~SmsConsole` <- `Emulator::~Emulator`. SMS would be the first console RetroPlug hosts that registers an audio provider unconditionally (every other `RegisterAudioProvider` call site is mapper- or HD-pack-conditional), so this fires on every close-system / reset / project-unload / plugin teardown.

**Correction to the previous pass: the fix is host-side, not vendored, and ASan does not catch it.** `Emulator::Stop(bool sendNotification, bool preventRecentGameSave, bool saveBattery)` is public at `Emulator.h:148`. Measured: `emu_->Stop(false, /*preventRecentGameSave=*/true, /*saveBattery=*/false)` before `emu_.reset()` gives 5/5 clean runs against 3/5 crashes for the bare `emu_.reset()` that `MesenNesSystem.cpp:211` and `MesenGbaSystem.cpp:172` both do today. The `preventRecentGameSave` flag is load-bearing, not cosmetic: `Stop()` otherwise runs `SaveStateManager::SaveRecentGame`, which serialises and writes to disk (and see H10 in section 2.6 - that serialise is itself a second detonation path). Both frames of the crash live in the uninstrumented `libmesen.a`, so `tools/run-sanitizer.sh` will **not** name this. Prove it with a construct/destruct loop in Catch2 instead.

**(b) The synthesized ROM filename is an unresolved three-way design conflict.** `SmsConsole::GetSupportedSignatures()` returns `{}` (`SmsConsole.h:39`) and the model comes purely from `romFile.GetFileExtension()` (`SmsConsole.cpp:46-59`). The same VirtualFile name also keys the battery-file stem via `FolderUtilities::GetFilename` (`Emulator.cpp:591`), read unconditionally into cart RAM at boot (`SmsMemoryManager.cpp:92-96`), and `Reset()`, which is `_emu->ReloadRom(true)` -> `LoadRom((string)info.RomFile, ...)` (`SmsConsole.cpp:148-152`, `Emulator.cpp:347`) and re-reads **from disk**. Measured, same bytes, name only differing:

```
name "rom.sms"  (not on disk): console ptr unchanged after Reset; workRam marker 0xA5 survives -> Reset is a SILENT NO-OP
name "/tmp/real.sms" (on disk): console ptr changes; workRam cleared -> real power cycle
```

So a constant `"rom.sms"` makes every SMS title in the process share the battery stem `rom` and kills the Reset menu row. See Tier 1 item 4.

**(c) The #1 risk ships with no guard unless the audio test comes first.** `SmsConfig::ChannelVolumes[4] = {}` (`SettingTypes.h:713`) times `SmsPsg.cpp:75,85`'s `volumes[i]/100` renders the PSG silent with a **correct sample count**, so a boot smoke test passes. The audio guard was in Tier 2 in the first draft; it is Tier 1 position 1 now.

The unchanged headline non-sync risk is **ROM identification policy**. Extension-derived model selection collides with the contract stated twice in your own code (`RomFormat.hpp:6-11`, `platform.ts:10-12`): classify by magic bytes, never by extension. Both staged test ROMs carry `TMR SEGA` at `$7FF0` with a model discriminator in the region nibble at `$7FFF` (`0x4c` = SMS-Export, `0x6c` = GG-Export), both 131,072 bytes, `size % 0x400 == 0` (no copier header). So detection **and** the SMS-versus-GG platform choice are byte-derivable, which is what makes D1 cheap. Residual exposure: headerless dumps and SG-1000.

---

## 2. Sample-accurate sync: the primary constraint

This section exists because D2 makes it the design's centre of gravity rather than a Tier 3 nicety. Everything here was measured against the real vendored `build/deps/mesen/libmesen.a`, mostly with the real `smsggdj_v0_45.{sms,gg}` at `/workspaces/resources/roms/smsggdj/`.

### 2.1 The verdict, stated honestly

**Sample-accurate stepping of the SMS core is achievable at a measured event-injection bound of 30 Z80 T-states = 0.40 output samples at 48 kHz, with byte-identical audio to `RunFrame()` at every flush cadence tested (with FM disabled). It costs roughly +9% CPU over `RunFrame`, and it needs two vendored `deps/mesen` edits for the `.sms` path and three for `.gg`.**

Three claims from the design pass do not survive the probes and are corrected here rather than carried:

- **"ZERO `deps/mesen` edits for the `.sms` path" is false.** The TH sync line is unreachable through Mesen's input model. Section 2.4.
- **"Cheaper at runtime than `RunFrame`, 25.03x versus 23.5x" did not reproduce.** Measured parity at best, and the variant you actually need for block exactness is **+9%**. Section 2.3.
- **"The cadence knob is pure performance and FM fidelity, not accuracy" is only true with FM off.** With `EnableFmAudio` on (its default, `SettingTypes.h:715`) the flush cadence changes the output at every setting, and the cadence that fixes block exactness is 6x worse for FM than the one that does not. Section 2.6 H3.

And one framing correction that matters more than any of them:

**Sell this as determinism, not as resolution.** smsggdj polls its sync lines exactly **once per video frame**, from the main loop, never from an ISR: `src/main.asm:308-326` halts on VBlank then calls `engine_frame` (`src/engine.asm:455`), which calls `sync_in_delta` (`src/engine.asm:646`) exactly once. `DESIGN.md:47` states the pins have no interrupt capability, and the GG build deliberately disables the one available PC6 NMI. Corroborated empirically: `IN A,($DC)` and `IN A,($BF)` each execute exactly 1.00 times per frame over 60 frames of the real ROM. One NTSC frame is 262 x 228 = 59736 T = **801.0 samples at 48 kHz**. The proposed 0.402-sample injection bound is roughly 2000x finer than anything the ROM can observe.

So sample-accurate stepping does not buy this ROM finer clock resolution. What it buys is exactly this, and it is worth having:

| | naive `RunFrame` clone | sample-accurate |
|---|---|---|
| host-side jitter on top of the ROM's 801-sample poll | **0..803 samples**, pseudo-random per block (measured GBA residues: 353, 134, 717, 497, 276, 56, 639, 419, 199, 782) | **0..1 sample** (block exact with the predictive tail, section 2.3) |
| which console frame observes a given counter change | free-running race between the console frame grid and the DAW block grid | a deterministic function of the offset and the accumulated sample count |
| reproducible run to run | no | yes |
| PDC-compensable | no (the mean is not stable) | yes (fixed mean, bounded 0..1 frame residual) |

Worst-case end-to-end error roughly halves, and the remaining term becomes reproducible and compensable rather than drifting. That is the defensible claim.

### 2.2 The four-console comparison

All granularity figures at **48 kHz**. "Injection bound" is the worst-case slip between the sample offset an event was scheduled for and the emulated instant it takes effect, excluding the ROM's own readiness or poll window.

| | **SameBoy** | **NES (Mesen)** | **GBA (Mesen)** | **SMS/GG as proposed** |
|---|---|---|---|---|
| Stepping primitive | `GB_run(gb_)` = 1 SM83 instruction (`SameBoySystem.cpp:690`, `deps/sameboy/Core/gb.c:1190`) | `cpu->Exec()` = 1 6502 instruction (`MesenNesSystem.cpp:370`) | `console->RunFrame()` = 1 **video frame** (`MesenGbaSystem.cpp:262-264`) | `cpu->Exec()` = 1 Z80 instruction (`deps/mesen/Core/SMS/SmsCpu.h:261`) |
| Max step size | 24 T-cycles (48 8 MHz-units), measured; 228-cycle SGB-boot outlier at `gb.c:1195-1207` | 6 CPU cycles, measured over 8410 instructions | 280896 master cycles | **30 T-states** measured over 8,019,512 steps on the real ROM (13 T over 2M steps on a synthetic ROM with a narrower instruction mix; take 30 as the real-ROM figure) |
| Step in samples @48k | **0.275** | 0.16 | **803.6** | **0.402** |
| Gate metric | `audioFrameCount_`, ++ per APU sample (`SameBoySystem.cpp:600`) | `audioDevice_->availableFrames()`, advances only at APU flush | none | **Z80 `CycleCount` -> samples** (section 2.3); ring depth only as the loop terminator |
| **Achieved event granularity @48k** | **<= 0.275 smp** | **67.1 smp** (default `apuLatencyMs 1.4` = 2504 cycles); floor 1.72 smp at `MinCycleLength 64` (`deps/mesen/Core/NES/NesSoundMixer.h:26`) | **803.6 smp**, and block-start-latched anyway | **<= 0.402 smp** |
| Block sample count | exact (measured min == max == frames, 200 blocks x 3 rates) | overshoot up to one flush window, carried in ring | overshoot 0..803, carried in ring | **exact** with the predictive tail; 0..44 with the design's literal `<= kFineSamples` predicate |
| Offset-accurate event path | `pushSerialIn(frame, byte)` per-byte gate (`SameBoySystem.cpp:678-688`). **Buttons are NOT offset-accurate**: `pendingButtons_` offsets are synthesized locally at 10 ms spacing (`SameBoySystem.cpp:169,568-571`), not caller-chosen | `pushCoreBytes(frame,...)` -> `NesN8FifoRole::pumpUntil` (`NesN8FifoRole.cpp:45-57`) | **none.** `pressButton` is frameless (`MesenGbaSystem.cpp:197`), all applied in `prepareForBlock` (`:237-244`) | **new** offset-gated level queue, fed over the existing `pushCoreBytes` seam (section 2.4) |
| Resampler in path | **none** (`GB_set_sample_rate` pushes the DAW rate into the core) | 96 kHz -> host, stateful `HermiteResampler` | 32768 Hz -> host, same | 96 kHz -> host, same (`deps/mesen/Core/SMS/SmsPsg.h:13`, `Core/Shared/Audio/SoundMixer.cpp:92-101`) |

Two rows deserve stating plainly rather than being buried:

- **NES is not currently sample-accurate.** It is quantized to the APU flush window and always **late by 0..67 samples** at 48 kHz. `pumpUntil` is called before every instruction, but the value it is fed (`availableFrames()`, `MesenAudioDevice.hpp:31-33`) only moves when `NesSoundMixer` flushes. The `reaper:n8-midi-timing` guard runs at `--tol-ms 30`, so nothing currently measures this. Lowering the `coreRoles.ts:59` clamp floor toward the C++ `MinCycleLength` of 64 cycles gives 1-2 sample granularity for a measured ~6% CPU (9.9x realtime -> 9.4x over 9.29 s of audio); that is a separate, cheap, unrelated win.
- **SameBoy's flagship DAW-clock sync mode is block-granular today.** `MidiSyncArduinoboy` deliberately pushes every 0xF8 at frame 0 (`dspRoles.ts:90-96`, commit `579faa11`) because the offset gate starved LSDj's serial ready window. Only `MidiSync` (`dspRoles.ts:192`) and raw host MIDI (`:34`) keep the real offset. Do not cite SameBoy as a uniform 0.275-sample end-to-end guarantee.

SMS as proposed lands third of the three on injection bound, within 1.5x of the gold standard, and **first on gate-metric quality**, because its gate is the CPU cycle counter rather than the audio ring depth. That is the single most important design decision in this section.

### 2.3 The step loop

Three phases, not two. Unlike NES it separates **stepping cadence** (per instruction, buys event accuracy) from **flush cadence** (coarse, buys CPU and protects the FM resampler), and unlike both NES and GBA it derives the intra-block sample position from the CPU clock instead of the audio ring.

```cpp
// packages/native/src/system/mesen/MesenSmsSystem.cpp

// Coarse flush budget in Z80 T-states. 256 output samples @48k NTSC ~= 19088 T.
// Must stay well under blip capacity (MaxSamples 4000 @96 kHz = 41.7 ms = ~149k T)
// and under SmsPsg's own 20000-T auto-flush so the cadence stays host-owned.
static constexpr std::uint64_t kCoarseCycles = 19088;
static constexpr std::uint64_t kInstructionBudget = 80000;   // RT-thread backstop, see H7

void MesenSmsSystem::prepareForBlock(const AudioBlockInfo& info) {
    // ... device / thread-id boilerplate as MesenNesSystem::prepareForBlock ...
    auto* console  = smsConsole();
    blockStartCycle_ = console->GetCpu()->GetState().CycleCount;
    blockCarry_      = audioDevice_->availableFrames();   // measured 1, NOT 0
    masterRate_      = console->GetMasterClockRate();     // 3579545 NTSC / 3546895 PAL
    pendingCycles_   = 0;
    // No sync-level application here. Levels are released inside the step loop at
    // their offsets; only frameless UI button taps drain here.
}

bool MesenSmsSystem::stepIfBelowTarget(std::uint32_t framesNeeded) {
    if (!activated_ || !emu_) return false;
    auto* console = smsConsole();
    if (!console) return false;
    auto* cpu = console->GetCpu();
    auto* psg = console->GetPsg();

    // Degenerate 1-member unit: run the whole block and report done (false), matching
    // MesenNesSystem. If SMS ever gains a link group, invert to one Exec per call so
    // BlockRunner's round-robin at BlockRunner.cpp:40-44 can interleave members.
    std::uint64_t budget = kInstructionBudget;
    while (audioDevice_->availableFrames() < framesNeeded && budget-- > 0) {
        // Intra-block sample position from the EMULATED CLOCK, not the ring depth.
        // The ring lags by up to one flush; the cycle counter never does. This is
        // what makes SMS finer than NES rather than equal to it.
        const std::uint64_t elapsed = cpu->GetState().CycleCount - blockStartCycle_;
        const std::uint32_t pos = blockCarry_ +
            static_cast<std::uint32_t>((elapsed * sampleRate_) / masterRate_);
        if (syncRole_) syncRole_->pumpUntil(pos);

        const std::uint64_t before = cpu->GetState().CycleCount;
        cpu->Exec();                                  // one Z80 instruction (+ any IRQ/NMI tail)
        const std::uint64_t spent = cpu->GetState().CycleCount - before;
        pendingCycles_ += spent;

        // PREDICTIVE tail: flush finely once the NEXT coarse window would overshoot the
        // target. The design pass's `(framesNeeded - available) <= kFineSamples` predicate
        // is wrong twice - it lands a second full coarse window before engaging (measured
        // maxOvershoot 44), and it is unsigned so it underflows once the ring passes the
        // target and can never re-engage.
        const std::uint32_t have = audioDevice_->availableFrames();
        const std::uint64_t remainCycles =
            (static_cast<std::uint64_t>(framesNeeded - have) * masterRate_) / sampleRate_;
        if (pendingCycles_ >= std::min<std::uint64_t>(kCoarseCycles, remainCycles + spent)) {
            psg->Run();                               // catch up to GetMasterClock()
            psg->PlayQueuedAudio();                   // blip_end_frame -> SoundMixer -> MesenAudioDevice
            pendingCycles_ = 0;
        }
    }
    if (syncRole_) syncRole_->pumpUntil(framesNeeded);  // release anything due through block end
    return false;
}
```

`finishBlock` is a straight copy of `MesenNesSystem.cpp:379-444`: `syncRole_->rebase(blockSize)` for offsets past the block end (mirroring `SameBoySystem.cpp:707-715`), then drain exactly `blockSize` frames and sum into the routed lanes with the smoothed gain (one `gainSmoother_.next()` per sample frame across all lanes, `MesenNesSystem.cpp:398-403`, not per lane).

**Why the cycle-derived position.** Measured over 200 blocks at the recommended cadence: `max |cycleDerivedPos - ringDepth| = 256 samples`. Gating event release on `availableFrames()` NES-style would therefore quantize sync delivery to 256 samples, **worse than NES's 67**. The derivation costs one 64-bit multiply and divide per instruction and decouples the two entirely. Accuracy of the conversion: the PSG's own timebase steps in 16 T-states (`SmsPsg.cpp:65,93-94`) = 0.215 samples @48k, and `blip_add_delta` places edges sub-sample inside blip's 20+32 fixed-point domain, so the map is exact to better than one sample throughout.

**`blockCarry_` is not zero.** The design pass asserted it measured 0; the probe measured **1** at the recommended cadence, and it is nonzero precisely because the naive predicate is not block-exact. The formula handles it either way; the comment claiming zero would mislead.

**Measured cadence matrix.** Two independent runs, one on the real ROM (900 x 512-frame blocks, 9.6 s @48k), one on a synthetic ROM (300 frames, 512-frame blocks). They agree on identity and disagree on the performance sign; the synthetic run is the one to trust for the delta because the real-ROM figures could not be reproduced:

| cadence | audio vs `RunFrame` | block exact | xRT (synthetic, FM off) | vs `RunFrame` |
|---|---|---|---|---|
| `RunFrame` | baseline | n/a | 13.62 | baseline |
| flush per instruction | `maxAbs=0` | YES | 10.71 | +27% |
| flush /75 T (~1 smp) | `maxAbs=0` | overshoot 1 | 12.43 | +9% |
| coarse 256 smp + design's literal fine-tail | `maxAbs=0` | **NO, overshoot up to 44** | 13.58 | parity |
| **coarse 256 smp + predictive tail** | **`maxAbs=0`** | **YES** | **12.44** | **+9%** |

So: **+9%, not the -6% the design pass claimed.** All variants step the identical instruction stream and produce byte-identical PCM with FM off. On the A53/Anbernic target a +9% headwind matters more than a claimed tailwind would have; SMS is a much lighter core than NES (which profiles at 0.79x realtime there), but this has not been measured on-device.

**Where the byte-identity comes from, and why it is safe.** `blip_end_frame` (`deps/mesen/Utilities/Audio/blip_buf.cpp:184-192`) carries the sub-sample remainder in `blip_t::offset`, and `SmsPsg::Run` carries the sub-16-cycle remainder in `_masterClock` (`SmsPsg.cpp:60-63`). Verified byte-identical to `RunFrame` at cadences of 1, 16, 75, 149, 746, 1000, 4772 and 19088 T, on a busy ROM, a quiet ROM, the real `.sms` and the real `.gg`, and at host rates 44100 / 48000 / 96000. Downstream there is no hidden buffering: `SoundMixer::PlayAudioBuffer` calls `Resample(..., maxOutCount = 0x10000/2)` (`SoundMixer.cpp:92`), so `HermiteResampler::_pendingSamples` never fills and the `<false>` path is a pure streaming resampler carrying `_fraction` and its 4-tap history across calls (`HermiteResampler.cpp:92-113`); and `MesenAudioDevice::GetStatistics()` returns `{}`, so `SoundResampler::GetTargetRateAdjustment` pins `_rateAdjustment = 1.0` (`SoundResampler.cpp:67-70`) and there is no dynamic-rate drift sliding offsets.

**Nothing in `RunFrame` is load-bearing.** `SmsConsole::RunFrame` (`SmsConsole.cpp:155-166`) is an `Exec()` loop plus `UpdateRegion` plus one PSG flush. `ProcessEndOfFrame` fires from `SmsVdp::ProcessEndOfScanline` (`SmsVdp.cpp:654-655`), driven by `SmsCpu::ExecCycles -> SmsMemoryManager::Exec(cycles*3) -> SmsVdp::Run` (`SmsMemoryManager.h:82-86`, `SmsCpu.cpp:827-834`), so it fires at its true emulated instant regardless of the caller. IRQ handling is inside `Exec()` itself (`SmsCpu.cpp:57-79`), and `Halted` still burns `ExecCycles(4)` so the VDP keeps running. Verified over 120 measured frames of the real ROM: `RunFrame` and per-instruction Exec both gave `vdpFrames=240`, `pollCounter=241`, exactly one `ProcessEndOfFrame` per frame, identical instruction counts (1,564,921), identical HALT-exit counts (114). `Emulator::ProcessEndOfFrame`'s frame limiter never engages because `_frameLimiter` is only created on Mesen's own emu thread, which `stopRom=false` prevents.

The one real casualty: `SmsConsole::UpdateRegion` is private (`SmsConsole.h:34`) and only called from `RunFrame` (`SmsConsole.cpp:157,174`), so a **runtime** NTSC/PAL change never propagates. Set the region in `SmsConfig` before `LoadRom` and treat it as immutable per construct. (`SmsPsg::SetRegion` calls `blip_clear`, discarding queued deltas, so a mid-block region change would drop audio anyway.) Documented limitation, zero edits.

### 2.4 The event path

**What is different from every existing sync path in this repo.** LSDj is a serial byte stream (`pushSerialIn(frame, byte)`). risa/N8 is a byte FIFO (`pushCoreBytes(frame, data, size, flush)`). smsggdj sync is neither: it is a **2-bit mod-4 binary counter carried as three held input LEVELS**, sampled once per video frame by a single `IN` instruction (`/workspaces/smsggdj/src/engine.asm:555-591`). Nothing measures edges or pulse widths. The capability needed is:

> set input line L of system S to level V at intra-block sample offset F, and **hold** it until changed.

#### The fatal problem the design pass missed: TH is unreachable

The design pass mapped the counter's bit 1 (TH, `$DD` bit 7) to `SmsController::Buttons::Down` on the port-1 device. That conflates the `addr` argument of `ReadRam` with the device index. Confirmed by reading the source and by three independent probes:

- `SmsControlManager::ReadPort(1)` sets bit 7 from `GetTh(true)` (`SmsControlManager.cpp:107`).
- `GetTh(true)` returns `InternalReadPort(**1**) & 0x80` when `ControlPort & 0x08` (`SmsControlManager.cpp:115-119`), which the default `ControlPort = 0x0F` satisfies.
- `SmsController::ReadRam(1)` for `_port == 1` only ever clears bits `0x01/0x02/0x04/0x08` (`Input/SmsController.h:80-86`). **Bit `0x80` is never touched at `addr == 1`.**

So `$DD` bit 7 is pinned high forever. Measured on the real ROM after 180 frames:

```
baseline           $DD=0xFF  ROM decodes counter=3
port2 Down   down  $DD=0xFF  changed=0x00  counter=3     <- the design's TH bit: NO EFFECT
port2 B      down  $DD=0xFB  changed=0x04  counter=2     <- TL, correct
port2 A      down  $DD=0xF7  changed=0x08  counter=2     <- TR, correct
```

The two lines the host **can** drive (TR and TL) are exactly the two the ROM ANDs together into a single counter bit (`engine.asm:562-568`), so the mod-4 counter degenerates to mod-2. Feeding the intended 0,1,2,3 stream, `sync_in_delta` (`engine.asm:646-655`) computes deltas of 1,3,1,3 which `engine_frame` accumulates into `sync_acc` (`engine.asm:461-465`): **double tempo with alternating 1/3-clock jitter**. That is a half-working sync that presents as an intermittent tempo bug, not a dead line. `SmsLightPhaser` is the only SMS device that touches `ReadRam(1)` bit `0x80`, and it derives it from live VDP scanline/cycle and pixel brightness (`SmsLightPhaser.h:88-90`), not a settable level.

**Zero-edit workaround, verified working, not recommended.** `SmsControlManager::WriteControlPort` is public; clearing `ControlPort` bit 3 flips TH-B to the register-driven branch (`SmsControlManager.cpp:120-121`):

```
latch=0x07 want=0 -> $DD=0x77 counter=0 OK
latch=0x07 want=1 -> $DD=0x7F counter=1 OK
latch=0x87 want=2 -> $DD=0xF7 counter=2 OK
latch=0x87 want=3 -> $DD=0xFF counter=3 OK
after 5 more frames of ROM execution, latch = 0x87 (ROM did not stomp it)
```

Three caveats kill it as a shipped design: it models the console driving its own pin; every host-driven TH rise fires `_vdp->LatchHorizontalCounter()` (`SmsControlManager.cpp:159-161`), a real emulated side effect on a register games read; and `engine_stop` writes `out ($3F),$FF` (`engine.asm:417`), restoring bit 3 and killing the backdoor until the host re-applies it. Usable as a spike.

#### Recommended vendored edit: one external-input mask per port

The single cleanest point is `InternalReadPort`, because TH, TR and TL all flow through it and it already uses active-low AND semantics identical to the devices:

```cpp
// SmsControlManager.cpp, InternalReadPort
uint8_t SmsControlManager::InternalReadPort(uint8_t port)
{
	uint8_t value = 0xFF;
	for(shared_ptr<BaseControlDevice>& device : _controlDevices) {
		if(device->IsConnected()) value &= device->ReadRam(port);
	}
	value &= _extInput[port & 1];   // RetroPlug: host-driven external port levels, active low, 0xFF = idle
	return value;
}
// SmsControlManager.h
uint8_t _extInput[2] = { 0xFF, 0xFF };
public: void SetExternalInput(uint8_t port, uint8_t levels) { _extInput[port & 1] = levels; }
```

Three functional lines. It makes TH (`0x80`), TR (`0x08`) and TL (`0x04`) all deliverable at `addr == 1` with correct active-low semantics; it flows through `GetTh`/`GetTr`'s existing device branches unchanged so the `$3F` direction model still applies; and because it is not device state it is **immune to the per-frame `ClearState()`** described below. This is a design proposal derived from confirmed source, not a measured result; it is the one piece of section 2 that has not been run.

#### The other required edits

**Edit 2 (required, 4 lines) - stop the per-video-frame input clobber.** `SmsVdp.cpp:654` calls `_console->ProcessEndOfFrame()`, which is `SmsConsole.cpp:168-172` -> `_controlManager->UpdateInputState()` -> `BaseControlManager.cpp:165-176` `device->ClearState(); device->SetStateFromInput();`. Measured: a held bit is wiped by one emulated frame (`$DD=0xF7 -> $DD=0xFF`, device `shared_ptr` unchanged, so it is the clear and not device recreation). It also calls `KeyManager::RefreshKeyState()` on the audio thread ~60 times a second. `SmsControlManager` has no `UpdateInputState` override (full header read). The fix mirrors the existing RetroPlug edit at `GbaControlManager.cpp:23-36` (and the NES equivalent, the commented-out call at `NesPpu.cpp:1391`): add `void UpdateInputState() override;` with the base call suppressed and the same comment. `SmsControlManager` caches nothing derived from input (unlike GBA's `_state.ActiveKeys`), so the body is empty. `MesenSmsSystem::prepareForBlock` fires `emu_->ProcessEvent(EventType::InputPolled, CpuType::Sms)` itself, as `MesenNesSystem.cpp:338` does.

With Edit 1 in place this is no longer strictly needed **for sync**, but it is needed for ordinary buttons and it is the right place to get `RefreshKeyState` off the audio thread. The reversible alternative is an `IInputProvider` (`Core/Shared/Interfaces/IInputProvider.h`) registered via `BaseControlManager::RegisterInputProvider`, which runs after the clear (`BaseControlManager.cpp:170-175`) and survives `UpdateControlDevices`'s device recreation.

**Edit 3 (required only for `.gg`, ~5 lines) - the Game Gear EXT port is a bare loopback.** `SmsMemoryManager.cpp:396` stores `_state.GgExtData = value & 0x7F` and `:467` returns it verbatim, both flagged `//TODOSMS GG - input/output ext port`. `_state.GgExtConfig` (`$02` direction, 1 = input) is stored and never consulted on read. `$04`, the serial receive register, is a literal `return 0xFF` (`:470`). smsggdj's GG startup writes `$02=$FF` then `$01=$FF` (`engine.asm:420-421`), so `sync_read`'s `in a,($01)` (`engine.asm:564`) returns a constant `0x7F` forever: counter pinned at 3, `sync_in_delta` = 0. Measured on the real `.gg`: `write $01=0xAA -> read $01=0x2A`, pure loopback; ROM decodes counter=3 and nothing external can change it. **GG sync is dead in stock Mesen.** Minimal honest fix, filling in an acknowledged stub:

```cpp
// SmsMemoryManagerState: uint8_t GgExtInput = 0xFF;   // externally driven levels (pull-ups high)
case 1: return (_state.GgExtData & ~_state.GgExtConfig) |
               (_state.GgExtInput &  _state.GgExtConfig);   // $02 bit set = input
```
plus `void SetGgExtInput(uint8_t v)` on `SmsMemoryManager`. No regression: with no external driver `GgExtInput = 0xFF`, so input pins read high (the real pull-up), which is what the ROM's own high-latch initialisation already produces today. Note the scope is understated by calling it a direction-mask fix: the whole GG link, parallel and serial, is unimplemented. Mesen **does** implement the GG `$DC`/`$DD` controller mirror (`SmsMemoryManager.cpp:483-487`), which GGSYNC.md documents as working on real hardware, but the shipped GG ROM reads `$01`, so the mirror does not help without a ROM change.

#### Vendored surface, measured against upstream

`deps/mesen` has no `.git` (it is vendored by copy, not a submodule; `git submodule status` lists catch2 / dpf.js / efsw / portable-file-dialogs / rtmidi / sameboy only). Diffed against upstream `SourMesen/Mesen2` HEAD `b9fa69d`: **`Core/SMS` is byte-identical to upstream, zero files, zero lines.** For comparison, the RetroPlug-attributable edits already carried:

| console | files | added | removed |
|---|---|---|---|
| NES (`NesSoundMixer.{cpp,h}`, `NesExpansionAudioState.h`, 11 mapper/expansion headers, `NesApu.cpp`, `NesPpu.cpp`) | 17 | ~401 | ~9 |
| GBA (`GbaControlManager.cpp`, `GbaConsole.cpp`) | 2 | 16 | 3, of which exactly **one line is functional** |
| **SMS/GG as proposed** | **4-5** | **~12 functional** | **0** |

So the vendored surface is roughly **3% of the NES one**, in files no other RetroPlug edit touches, so there is zero merge interaction. Every SMS edit is an input-model addition or a single-line stub-fill, nearest in class to `NesPpu.cpp:1391` (one line commented out) and `NesExpansionAudioState.h` (a new file with no upstream counterpart, which costs nothing on rebase), rather than the structural `NesSoundMixer` rework that is what actually makes NES painful. **Whatever the argument against SMS is, it is not the vendored surface.**

One caveat AGENTS.md implies but does not state: `deps/mesen` has no equivalent of `cmake/patches/sameboy-per-channel-audio.patch`, so unlike SameBoy there is no configure-time guard that fails loudly if an edit is lost. The 21 `// RetroPlug:` markers across 9 files are the only inventory; all three SMS edits must carry one to stay findable by `grep -rn RetroPlug deps/mesen`.

#### The host seam: use `pushCoreBytes`, not a new virtual

The design pass proposed a new `pressButtonAt(frame, button, down)` virtual plus plumbing across five files. That is not needed for sync, because the sync payload is a one-byte level word and `pushCoreBytes` already carries the frame:

```
sms-sync role (TS)  --pushCoreBytes(frame, [levelByte], false)-->
  Engine.cpp:106-111 (frame preserved)  -->
    MesenSmsSystem::pushCoreBytes  -->  deque<{offset, levels}>  -->
      SmsSyncRole::pumpUntil(pos)  -->  SetExternalInput(1, levels)   [.sms]
                                   -->  SetGgExtInput(levels)        [.gg]
```

Zero new `SystemBase` virtuals, zero `Engine.cpp` change, zero `dspKernel.ts` change. `SmsSyncRole` is a near-clone of `NesN8FifoRole` (63 lines) with `pushBytes` / `pumpUntil` / `rebase` intact and `flushAll` dropped (a held level has no barrier semantics; a DAW seek is simply a counter that keeps counting). This removes roughly half a day from the design pass's estimate.

**The button-frame plumbing gap is real but separable.** The field exists end to end and is dropped at both ends:

- `DspRuntime.hpp:107` `struct ButtonOut { system; frame; button; down; }` - field present.
- `DspRuntime.cpp:174-186` the `pressButton` thunk parses and stores the frame.
- `dspKernel.ts:438` `pressButton: (button, down) => this.sink.pressButton(id, 0, button, down)` - **hardcodes 0**.
- `Engine.cpp:113-115` `t->pressButton(bo.button, bo.down)` - **`bo.frame` read nowhere**.

A reader who greps for `frame` in the button path will wrongly conclude it is plumbed. Closing it is worth doing as an additive `pressButtonAt` overload with a forwarding default (so `SameBoySystem` / `MesenNesSystem` / `MesenGbaSystem` are bit-for-bit unchanged, and SameBoy's load-bearing 10 ms synthesized spacing survives), but it is Tier 2, not on the critical path.

#### Transport A: SMS controller port 2 (`$DD`)

smsggdj's SMS `sync_read` is `in a,($DD)` with bit 3 = TR, bit 2 = TL, bit 7 = TH (`engine.asm:577-582`). With Edit 1 the host writes one level byte per counter change:

| ROM sees | `$DD` bit | mask bit in `SetExternalInput(1, ...)` |
|---|---|---|
| **TH** = counter bit 1 | 7 | `0x80` |
| **TR** = counter bit 0 | 3 | `0x08` |
| **TL** (ANDed with TR) | 2 | `0x04`, left high |

**Active LOW.** `ReadRam` starts at `0xFF` and clears the bit when pressed, so *asserted = line low = logic 0*. Per counter value the role emits `levels = 0xFF & ~((counter & 1) ? 0 : 0x08) & ~((counter & 2) ? 0 : 0x80)`, or more readably: bit 3 low when counter bit 0 is 0, bit 7 low when counter bit 1 is 0. Getting this inverted produces a counter that decrements or sticks. TL is left high, so `sync_read`'s "TR AND TL" reduces to TR, which is exactly what a straight 3-wire DE-9 cable produces and the case the AND was written for (`engine.asm:550-554`).

`_state.ControlPort` defaults to `0x0F` (`SmsControlManager.cpp:19`), so both direction bits already select the device path, and smsggdj never writes `$3F` in a slave mode (the only write is `$FF` on stop, `engine.asm:417`, whose low nibble is still `0x0F`). The default configuration is already correct.

This delivers true sample accuracy at the `IN` instruction because `SmsController::RefreshStateBuffer()` is **empty** (`Input/SmsController.h:53`) and `ReadPort` reads devices live, so a level change is visible to the very next `IN`. Measured end-to-end injection slip on the reachable line (TR), with a ROM polling `$DD` in a tight loop: **0-2 samples** at offsets 0/64/128/256/400.

#### Transport B: Game Gear EXT (`$01`)

The GG build reads `$01` directly, PC4 = TL bit 4, PC5 = TR bit 5, PC6 = TH bit 6 (`engine.asm:564-573`). With Edit 3 the host drives PC5 and PC6 from the same offset-gated drain and leaves PC4 high so `PC4 AND PC5` reduces to PC5, matching GGSYNC.md's note that for a direct bridge connection pin 6 may remain unconnected. The ROM holds `$02 = $FF` at rest and while slaved, so every sync pin is on the input side of the new mask.

#### The TS role: `sms-sync`

A near-clone of `risaSync` (`dspRoles.ts:221-249`), attached by ROM marker in `romProviders.ts`. It needs no new kernel machinery: `walkTicks` (`dspKernel.ts:183-207`) already yields drift-free per-tick sample offsets, `block.transport` gives the edges, `ppqStart` discontinuity gives seek detection, and `setNextTick` re-phases after one. State machine, mirroring the reference encoder at `/workspaces/smsggdj/adapter/src/sync_protocol.c` exactly:

- MIDI **START** -> `counter = 0`, running.
- **CONTINUE** -> running (counter untouched).
- **CLOCK** -> `counter = (counter + 1) & 3`, **only while running**.
- **STOP** -> `running = false`, **counter FROZEN, not reset**. Zeroing it injects a spurious clock on the ROM's next poll.

On a transport rise, hold the current level and begin counting from the next tick: the ROM arms on Play and starts on the *first* observed counter change (`engine.asm:371-390` latches `sync_last` from the live level, `:655-665` forces the first nonzero delta to exactly 1). While playing, `c.eachTick(24, ...)` for IN24 or one tick per intended row for IN. There is **no locate barrier**: the ROM has no song-position pointer (`DESIGN.md` section 11.3, the LSDj live-sync model), so unlike risa there is nothing to flush.

**Set-and-hold, never pulse.** A level must survive at least one full frame poll to be guaranteed observed; a press+release collapsed into one block would be invisible. The counter model sidesteps this naturally, which is also why the LSDj Arduinoboy failure mode (`dspRoles.ts:90-96`) cannot occur here: there is no readiness window on a level.

**PDC.** `__rp_syncLatencyMs` (`pluginControlPlane.ts:99-107`) currently knows only `lsdj-sync` in `MidiSync`/`MidiSyncArduinoboy` at a measured 33 ms. `sms-sync` needs an entry or SMS renders land off-grid exactly as LSDj did before that constant was added. **`risa-sync` is already missing from that table** despite `reaper:risa-sync` shipping in `RENDER_SCENARIOS` (`tools/run-reaper-suite.sh:35`), so the same fix covers both. Also note `updateLatency()` only runs on load / activate / autoload (`PluginDSP.cpp:129,139,252`), so a runtime sync-mode change does not re-report PDC.

#### Out of scope: `SYNC: MIDI` takeover

The Z80 is the **clock master** of a bit-banged 2-wire shift-in and samples DAT within ~3 microseconds of driving CLK high (`/workspaces/smsggdj/src/midi.asm:74-78`: `out ($3F),a` immediately followed by `in a,($DD)`, roughly 8-11 T = under 1/8 of an audio sample). Serving that needs an emulated peripheral reacting synchronously to a `$3F`/`$01` write **inside the core**, a write-callback device, not anything schedulable from a host block. Sample-accurate stepping does not help; only in-core peripheral emulation does. Additionally `MIDI_CAP=16` events at ~1.7 ms each is up to ~27 ms of bit-banging, exceeding one 16.7 ms frame and starving the sample DAC feed. **Defer.**

### 2.5 What the sample-accurate path costs

Delta over the naive `MesenGbaSystem` clone. Both share the same ~400 lines of `MesenSmsSystem` boilerplate, so that is not in the delta.

| item | lines | effort |
|---|---|---|
| `stepIfBelowTarget` three phases | +30 | 0.5 day |
| `SmsSyncRole` (offset queue + `pumpUntil` + `rebase`) | +80 | 0.5 day |
| vendored Edit 1: `SetExternalInput` in `SmsControlManager` | +3 | **0.5 to 1 day of discovery** |
| vendored Edit 2: `UpdateInputState` override | +4 | included above |
| vendored Edit 3: GG EXT input model (`.gg` only) | +5 | included above |
| host-side teardown `Stop(false, true, false)` | +1 | 0.5 day (ASan-blind, needs a cycle loop to prove) |
| `sms-sync` TS role + its TS tests | +40 plus tests | 1 day |
| Catch2 sync/cadence/exactness guard (**not a clone**, section 2.8) | ~250 | 1 to 1.5 days |
| test ROMs into `resources/roms/` | - | 0.25 day plus a licensing note |
| `reaper:sms-sync` render leg | 1 script + 1 lua + 1 `.rpp` | 0.5 to 1 day |
| PDC entry (`sms-sync` and the missing `risa-sync`) | +6 | plus a measurement pass |

**Effort delta: 4 to 5 days on top of the 4 to 5 the naive clone would take, so Tier 1 is 8 to 10 days.** That is up from the design pass's own +2 to +3 estimate, and the increase is concentrated in the three places it did not cost: the TH input path, the guard that cannot be a clone, and the teardown fix.

**Risk delta.** Not negative as the design pass claimed, because the performance tailwind did not reproduce. It is roughly neutral: +9% CPU against `RunFrame`, offset by the naive clone's own unfixed risks (an unbounded `while` on the RT thread, and 0..800 samples of nondeterministic per-block jitter no test would catch). The audio is proven byte-identical to `RunFrame` at every cadence on both `.sms` and `.gg` **with FM off**, which is the qualifier that matters (section 2.6 H3).

### 2.6 Hazards

**H1 - a bare `Exec()` loop starves, then corrupts the heap.** Proven twice: a ROM that programs the PSG once then loops produced **0 samples over 98235 instructions** (and 0 over 400,000 in the second probe) with no explicit flush. Worse, on the real ROM the bare loop **segfaults**: once ~149147 T-states elapse without a flush, the next ROM PSG write drives `blip_add_delta` past `blip_new(4000)` capacity, and the `assert` at `blip_buf.cpp:191` is compiled out under `NDEBUG` (`-O3 -DNDEBUG` in `build/deps/mesen/CMakeFiles/mesen.dir/flags.make`). *Handled:* the flush is unconditional on the cycle budget, not conditional on ROM behaviour. This is the real hazard the first scoping pass sensed; the fix is one line, not `RunFrame`.

**H2 - per-frame input clobber.** Edit 2, section 2.4. Confirmed by two independent probes.

**H3 - FM degradation under fine flushes, and it fights block exactness.** `SmsFmAudio` is an `IAudioProvider`; `MixAudio` resamples with `fillToMax=true` then clears `_samplesToPlay`, force-fitting 49716 Hz OPLL input into whatever the PSG flush produced. Two compounding mechanisms: `HermiteResampler.cpp:71-73` clears `_pendingSamples` when `size() >= maxOutSampleCount`, and `:116-121` pads unfilled slots with a zero-order hold. Measured with FM enabled and a keyed YM2413 note (signal rms ~3910, reference = `RunFrame`):

```
per-instruction                    maxAbs=500  rmsErr=38.2
/75T (~1 smp)                      maxAbs=902  rmsErr=75.1
coarse 256smp + naive fine-tail    maxAbs=530  rmsErr= 3.5
coarse 256smp + predictive tail    maxAbs=530  rmsErr=21.9
```

**Two corrections to the design pass.** First, coarse flushing does **not** restore `RunFrame` equivalence; there is a residual at every cadence, with instantaneous excursions of ~13% of signal rms. Second, and more awkwardly, **the variant that gives block exactness is 6x worse for FM than the one that does not**. So with FM on, the cadence knob *is* an accuracy knob, and it trades against exactness. With FM off the output is byte-identical (`maxAbs=0`) at every cadence. *Handled by:* `SmsConfig::EnableFmAudio = false` (`SettingTypes.h:715`, defaults **true**) if smsggdj does not use the YM2413, which makes the whole path provably cadence-invariant. **This is why open question 3 is not an optional nicety: it decides whether the design's central identity claim holds.** Note FM never *drifts* under any cadence, because `fillToMax` rate-locks it to the PSG stream; it degrades in timbre.

**H4 - total silence on a naive port.** `SmsConfig::ChannelVolumes[4] = {}` (`SettingTypes.h:713`) and `SmsPsg::Run` multiplies every channel by `volumes[i]/100` (`SmsPsg.cpp:75,85`). Measured A/B driving the PSG directly: default gives `frames=143893 peak=0 meanAbs=0.00`; `=100` gives `frames=143737 peak=8121 meanAbs=62.38`. *Handled:* set all four to 100 at construct. `FmAudioVolume = 100` on the same struct, so an FM cart emits FM but no PSG, a sharper symptom.

**H5 - deliberate nondeterminism.** `SmsVdp.cpp:37` does `_state.Scanline = RandomHelper::GetValue(0, 200)` on construct, and `SmsConfig::RamPowerOnState` defaults to `Random` (`SettingTypes.h:702,717`). Two identical `RunFrame` runs diverge. *Handled:* pin `RamPowerOnState` before `LoadRom`; pin the scanline via the public `SmsVdp::GetState()` in test harnesses. **Required before any byte-identity guard can be non-flaky.**

**H6 - savestate divergence (cosmetic but confusing).** The Exec-driven path and `RunFrame` produce byte-identical *audio* but differ in exactly one savestate field: `SmsFmAudio::_prevMasterClock`, lagging by one instruction (12-13 T) when the final flush emitted 0 samples so `MixAudio` did not run. Proven to be the only divergence. *Handled:* assert audio equality, never savestate equality across stepping models.

**H7 - unbounded `while` on the RT thread.** GBA's loop (`MesenGbaSystem.cpp:262-264`) has no cap and no `audioRunning` guard. *Handled:* the `budget` counter. At ~1000 instructions per 64 samples, a 512-frame block is ~8000 instructions; a budget of ~10x that is a safe backstop.

**H11 - a coarse window wider than the block starves the step loop entirely, and no analysis in this
document predicted it.** Found by measuring the shipped loop rather than reasoning about it. The
fine tail only engages within `kFineSamples` (64) of the target, so a block SMALLER than the coarse
window takes a single ~256-sample flush and sails past. Measured residue by block size, before the
fix:

| block size | 32 | 64 | **128** | **199** | 256 | 512 | 1024 | 2048 |
|---|---|---|---|---|---|---|---|---|
| residue | 0 | 0 | **131** | **172** | 3 | 3 | 3 | 3 |

At 128 the residue EXCEEDS the block, so the next block is satisfied entirely from the ring and
`stepIfBelowTarget` never enters its loop. Audio stays correct - nothing is lost, the ring just runs
ahead - which is exactly why this would have shipped unnoticed. The damage is to sync: `pumpUntil`
is called from inside the step loop, so **a block that never steps never releases its scheduled
events**. A sync event would be silently dropped rather than delivered late. 128 and 192 are
ordinary DAW buffer sizes.

*Handled:* clamp the coarse budget to one block, `min(kCoarseCycles, cyclesFor(framesNeeded))`.
Derived from `framesNeeded`, a per-block constant, NOT from the shrinking remainder - a
remainder-derived budget is the predictive tail, which forces a fine flush near the target on every
block and is the variant that costs FM. The clamp is inert at or above the coarse window, so the
common path stays bit-identical: residue after the fix is 28 at bs=128, 11 at 199, unchanged at 3
elsewhere, and every block still steps. Guarded by "SMS step loop never leaves a block's worth of
audio in the ring", which asserts both `residue < blockSize` (the no-stall invariant) and
`residue <= 64`.

**Note this invalidates the "0..44 overshoot" figure** quoted in the comparison table and the cadence
matrix above: that was measured at one block size on a different harness. The real behaviour is
block-size dependent, and was pathological in a band nobody sampled.

**H8 - the host does not fully own the cadence.** `SmsPsg::Write` calls `Run()` on every PSG port write (`SmsPsg.cpp:127`), which will itself flush if `_clockCounter >= 20000` (`:104-106`). Harmless (accumulation is exact) but *any test asserting an exact chunk pattern will be flaky*. Assert sample totals and audio bytes, never flush counts. Measured in one per-instruction run: 23470 of 46941 flushes produced 0 samples and 23472 produced exactly 1, with no drift.

**H9 - `Buttons::Pause` on port 0 drives NMI, not a button.** `SmsVdp.cpp:602` polls `IsPausePressed()` into `SetNmiLevel`. Whatever the new `SmsButton` remap puts on `Pause` fires the Z80 NMI on SMS; on GG the same bit reads as Start at port `$00` bit 7 (`SmsMemoryManager.cpp:456-464`). Two different behaviours from one wire button, which is a second, independent argument for D1.

**H10 - savestate serialisation is a second heap-corruption path, and RetroPlug takes one every block.** `SmsPsg::Serialize` calls `Run()` when saving (`SmsPsg.cpp:168`), so taking a savestate after a long un-flushed gap replays the whole gap into blip in one go and overruns the 4000-sample buffer. Caught in a probe backtrace: `blip_add_delta <- SmsPsg::Run <- SmsPsg::Serialize <- SmsConsole::Serialize <- SaveStateManager::SaveRecentGame <- Emulator::Stop`. RetroPlug calls `saveStateBytes()` from `publishStateSnapshot` **every block** (`MesenNesSystem.cpp:437`, `MesenGbaSystem.cpp:290`), so an SMS system inherits this on the hot path. `kCoarseCycles = 19088` bounds the gap to ~512 blip samples and is safe, but the invariant is stronger than "flush so audio appears": it is **never let the PSG fall more than ~149,000 T behind, because three unrelated call sites will silently corrupt the heap if you do**. This is also why the teardown fix must pass `preventRecentGameSave=true`.

### 2.7 Residual inaccuracy, ranked

**1. The ROM's own poll rate: 801 samples NTSC / 966 PAL. Dominant, and NOT removable.** Section 2.1. Every sync path in smsggdj is polled once per video frame from the main loop; the 2-bit mod-4 counter exists specifically to survive that losslessly for up to 3 clocks between polls. Consequences that fall out and bound the product: max 3 clocks between polls before the counter aliases; IN24 caps at 450 BPM NTSC / 375 PAL; the slave advances at most **one row per frame** in every mode (`engine.asm:742-750` has no loop), so IN tops out at 60 rows/s.

**2. Event injection slip: 0.402 samples.** Measured max 30 T-states over 8,019,512 single `Exec()` steps on the real ROM (typical 4-24 T; 30 is an `EX (SP),IX`-class instruction plus an IM2 IRQ tail; a synthetic ROM measured 13 T max over 2M steps, consistent with a narrower instruction mix). Third of the four consoles, within 1.5x of SameBoy.

**3. `walkTicks` offset truncation: up to 1 sample early, systematic.** `dspKernel.ts:203` `Math.trunc`, mirrored by the uint32 cast in `PpqUtil.hpp:53`. Intentional (the native and TS twins must agree bit for bit) and shared by every system including SameBoy. **Larger than the SMS stepping slip itself.**

**4. The 96 kHz -> host-rate `HermiteResampler`: SMS is at NES parity here, not SameBoy parity.** SameBoy has no resampler in the path at all. Two mitigating facts: `_fraction` is carried across calls so there is no per-flush re-phasing, and `_rateAdjustment` is pinned at 1.0 so there is no dynamic-rate drift. But it is a real buffering stage SameBoy does not have, and its group delay is uncompensated.

**5. The FM provider path.** See H3. Not introduced by the sample-accurate path (`RunFrame` has its own OPLL discard), but the cadence changes it and the exact variant makes it worse.

**6. The poll's phase within the frame is variable.** It sits after `read_input` / `handle_pause` / `editor_input` / `smp_housekeep` (`main.asm:321-325`), whose cost varies row to row. The *rate* is exactly one per frame; the *instant* is not a fixed offset from VBlank. Any host model assuming "the ROM polls at VBlank + K samples" will be wrong by a variable amount. Another reason PDC must be calibrated by measurement, as the LSDj 33 ms was, rather than derived.

**Not a residual, contrary to the obvious worry: the flush cadence does NOT blur the audio timebase.** Byte-identical output at per-instruction, /16, /75, /149, /746, /4772 and /19088 cadences, on a busy ROM, a quiet ROM, the real `.sms` and the real `.gg`, at 44100 / 48000 / 96000. This is the property that makes the design safe, and it is the one an SMS integration would most reasonably have feared losing.

### 2.8 What would actually prove it

**The sharpest consistency gap in the whole plan: no existing Catch2 guard proves sample accuracy of any system today. Both "sync timing" tests deliberately bypass the emulator.**

- `packages/native/test/audio/SameBoySerialTiming.test.cpp:65-80` sets `sys->audioFrameCount_` **by hand**. Its own header comment says so.
- `packages/native/test/audio/NesN8FifoTiming.test.cpp` constructs a bare `NesN8FifoRole` with **no emulator at all**.

Both prove the queue contract (gate at offset, release in sorted order, rebase past block end). Neither proves that the step loop advances the gate metric at the correct rate. **A straight clone into `SmsSyncTiming.test.cpp` would validate `SmsSyncRole::pumpUntil` and prove nothing whatsoever about section 2.3**, which is the entire thesis. Three assertions are needed and none of them exists in any shape today:

1. **Block exactness.** Drive N blocks through `prepareForBlock` / `stepIfBelowTarget` / `finishBlock` and assert the ring residue is 0 after every block. This is the property that distinguishes the design from the `RunFrame` clone (GBA leaves 0..803). Directly falsifiable; it is exactly what caught the naive fine-tail predicate's 44-sample overshoot.
2. **Gate-metric fidelity.** Assert `blockCarry_ + (elapsedCycles * sampleRate) / masterRate_` tracks `availableFrames()` to within the flush window across a block. This is what proves SMS is finer than NES rather than equal to it. **Requires a real console instance**, so it is a genuinely new test shape.
3. **Cadence invariance.** Render the same ROM at per-instruction, /75 T and /19088 T cadences and assert byte-identical PCM. Cheap, high-value, and it converts the design pass's unreproducible probe numbers into a guard. Pin H5 first or it is flaky by construction. Per H8, assert PCM bytes and sample totals, never flush counts.

**The ROM problem.** `RP_MGB_ROM_PATH` / `RP_N8_MIDI_ROM_PATH` (`packages/native/CMakeLists.txt:645-646`) point at `${CMAKE_SOURCE_DIR}/resources/roms/`, which contains only `mGB.gb`, `n8-midi.nes`, `n8-midi-vrc6.nes`, `n8-midi.dbg`. **The smsggdj ROMs are not in the repo**: they are at `/workspaces/resources/roms/smsggdj/smsggdj_v0_45.{sms,gg}`, the sibling tree. The first scoping pass's `RP_SMS_ROM_PATH="${CMAKE_SOURCE_DIR}/resources/roms/smsggdj/..."` points at a path that does not exist. Two options with very different verification value: **vendor the ROMs** into `resources/roms/` as mGB and n8-midi are, so the guard runs in CI (`pnpm test:plugin` is a CI step, `build.yml:74,176`); or use the sibling-tree pattern, in which case it skips silently exactly as `tools/author-risa-rplg.js:24-27` does (`console.log("SKIP"); process.exit(0)`, so `pnpm reaper:all` reports `risa-sync` green on a resource-less checkout). **Vendor them.** They are MIT (`/workspaces/smsggdj/LICENSE`, (c) 2026 Seb Tomczak / little-scale), 131,072 bytes each, and `release.yml:92-112,164-177,236-253` packages only `build/bin` plus the license bundle, so `resources/` never ships and no `THIRD-PARTY-NOTICES.txt` row is needed.

**The Reaper leg, and what it cannot do.** A `reaper:sms-sync` render leg clones `reaper:risa-sync` almost exactly: an author script + a `.lua` + an `examples/reaper/sms_sync.rpp` + `--drift` analysis, wired into `RENDER_SCENARIOS` at `tools/run-reaper-suite.sh:35` (currently 7 scenarios). But `tools/reaper-timing-analyze.py` works on audio-envelope onset detection, and the existing tolerances are `--tol-ms 25` (mgb), `--tol-ms 30` (n8), ~50 ms for drift. **At 48 kHz, 25 ms is 1200 samples.** A Reaper leg cannot resolve 0.4 samples and cannot even resolve NES's 67. Its real job is drift (does the error accumulate over 60 s) and gross regression (did sync die). It is necessary and it is not sufficient; only assertions 1-3 above prove the accuracy figure.

**One more thing the leg needs that nothing budgets: the ROM must be driven into `SYNC: IN` through its own UI first.** `$DD` is untouched at boot; the ROM is not in a slave sync mode until the user sets it. That is a scripted button sequence in the fixture, not a config flag.

**And `tools/run-sanitizer.sh thread`** over the new step loop is not addressed anywhere in the design. `MesenSmsSystem` construction on `RenderJobRegistry` worker threads is the same `mesenGlobalInit` hazard the first scoping pass flags, and TSAN is the only thing that finds it. ASan is *not* the thing that finds the teardown UAF (both frames are in the uninstrumented `libmesen.a`).

---

## 3. The GBA yardstick, git-measured

State this explicitly so the numbers above are judgeable.

**The original add:** `7624c0dd` "GBA support", 2026-05-16 12:22, **17 files, +592/-22**. GBA-specific new code was **379 lines** (`GbaSystem.cpp` 264 + `GbaSystem.hpp` 73 + `GbaConfig.hpp` 42); the vendored `deps/mesen` edit was **15 lines**. Preceding commit `0f0297df` is 10:43 the same day, so **wall clock under 2 hours**.

**The greenfield re-home:** `22e85365` (2026-07-06) wired NES **and** GBA onto today's SystemFactory seam in 13 files, **+141/-5**; its test `48921e96` added 3 files, **+85**. Two already-written Mesen systems re-homed for 226 lines total.

**The tail:** 13 commits, 2026-05-18 to 2026-07-17, **61 days**, as each cross-cutting `SystemBase` feature was re-implemented GBA-side: `a2dd595f` memory access (+34), `6cd31aea` menu options (+88/-2), `049a585f` zip projects (+14/-17), `3894a427` generic CPU state (+113), `26879918` CPU stepping (+50/-17), `df65193b` state snapshots (+18), `2c66c701` per-block triad (+31/-13), `14cffe46` parallel render (+8/-11), `e4c494c8` sibling .sav (+6), `3b2ef2e3` .sav pairing (+6), `46db5a32` dead snapshotConfig (-14), `a9869a4c` channelLayout seam (+6/-2), `da6fad0a` render registry (+5/-4). GBA-attributable tail total: **+379 / -80**, exactly equal to the original add. Cumulative over all history for the host GBA files alone: **41 file-touches, +1833 / -574**; the .cpp grew 264 -> 470 lines (+78%), the header 73 -> 115 (+58%).

**Calibration.** Tier 1 now lands ~20 files and ~1100 lines against GBA's 17/592, so ~1.9x GBA-original on volume, and it carries three things GBA never had: a teardown UAF, a filename identity conflict, and a sync path. GBA also had **no TS layer at all** in May (the `platform.ts` / `projectConfig.ts` / `fileSelection.ts` spine did not exist until `b4e4ae27`, 2026-07-07), and GBA's `RomFormat.hpp +27` was a mechanical logo memcmp where SMS needs offset probing, a copier-header case, and a two-way model discriminator. **8 to 10 days** for Tier 1 including finding nothing new. Tier 2 as listed is 8 rows over ~35 files including 9 TS test files and 11 doc files; with the GBA tail as evidence, the honest Tier 1 + Tier 2 number is **three to four weeks of calendar**. For Tier 3's tracker row, the closest analogue is the risa integration: **69 commits, 445 file-touches, +17,577 / -1,435**.

---

## 4. What you get for free

### Genuinely free, verified

| Thing | Evidence |
|---|---|
| **The whole SMS core compiles and links today** | `deps/mesen/CMakeLists.txt:9-11` glob; 17 `.o` files under `build/deps/mesen/CMakeFiles/mesen.dir/Core/SMS/`; 873 `Sms` symbols in `build/bin/retroplug`. **Zero build-system work.** |
| **`Core/SMS` is byte-identical to upstream Mesen2 HEAD** | `diff -rq` against `SourMesen/Mesen2` `b9fa69d` is empty. Clean rebase baseline; the three proposed edits are the entire future surface. |
| **`registerCoreBackends` is 4 lines and needs zero edits** | `CoreBackends.cpp:9-12`, called identically by the plugin, DPF-jack standalone, `sdl/main.cpp`, `cli/main.cpp`, `RenderHost.cpp`, `UiHarness.cpp`. |
| **`MesenBackend::build` is the one dispatch point** | `MesenBackend.cpp:53` (`"nes"`), `:73` (`"gba"`). `bootMesen<T>` already does onActivate + `activated()` gate + snapshot enable. |
| **`SystemBase` is a narrow seam** | Exactly three pure virtuals: `kind()` (:35), `onActivate(double)` (:37), `onSampleRateChanged(double)` (:39). Snapshot/savestate plane, `runUntilPc`, fused `onProcess` are base-implemented (`SystemBase.cpp:15-153`). |
| **The sync payload needs no new `SystemBase` virtual** | `pushCoreBytes(frame, data, size, flush)` (`SystemBase.hpp:94-95`) already carries the intra-block offset and `Engine.cpp:106-111` preserves it. Section 2.4. |
| **`SystemKind` is write-only** | 7 hits repo-wide: 3 `kind()` overrides + 4 test doubles. Nothing dispatches on it. Adding `MesenSms`/`MesenGg` is free and cosmetic. |
| **No BIOS needed - confirmed empirically** | `FirmwareHelper.h:354-364` returns false silently with no `MissingFirmware` notification; the post-BIOS fake is inline at `SmsMemoryManager.cpp:69-74`. Probe printed `LOADROM OK hasBios=0` and rendered 180 frames. ColecoVision at `FirmwareHelper.h:366-379` is the opposite and hard-requires `bios.col`. |
| **Video is resolution-agnostic below the tile** | `MesenVideoDevice.cpp:18-32` min-clamped copy; `EmulatorTile.tsx:79` reads `frame.width/height`, `:89` uses `LV_IMAGE_ALIGN_CONTAIN`. `SmsConsole.cpp:267,277` returns `SmsDefaultVideoFilter` through the console-agnostic `VideoDecoder`/`VideoRenderer` path. |
| **Savestates round-trip** | Probe `SaveState`/`LoadState` returned ok. `getMemory` targets exist (`MemoryType.h:78-82`), all under `kMaxStreamableBytes`: PrgRom 131072, WorkRam 8192, CartRam 32768, VideoRam 16384, BootRom 0. |
| **PAL 50 Hz is a non-issue** | `BlockRunner.cpp:37-44` round-robins to a sample target; `SmsPsg` emits at a fixed 96 kHz (`SmsPsg.h:13`) and `SoundMixer` resamples. Nothing in `AudioBlockInfo` carries a frame rate. |
| **The Exec/flush primitives are all public** | `SmsConsole::GetCpu/GetPsg/GetVdp/GetMemoryManager` (`SmsConsole.h:45-48`), `SmsCpu::Exec`/`GetState` (`SmsCpu.h:253,261`), `SmsPsg::Run`/`PlayQueuedAudio` (`SmsPsg.h:40-41`). `SmsDebugger.cpp:74` calling `_console->GetPsg()->Run()` is in-tree proof that an external caller pulling the PSG forward is a supported pattern. |
| **`SmsCpu::GetState()` returns a mutable ref** | So `setCpuRegister` writes directly with no pipeline-reload dance, unlike `GbaCpu`. |
| **`SmsController::RefreshStateBuffer()` is empty** | `Input/SmsController.h:53`. Reads are live and unlatched, so an injected level is visible to the very next `IN`. This is what makes section 2.4 sample-accurate at all. |
| **The button wire format already fits** | `InputTypes.hpp` keeps `GameboyButton`/`NesButton`/`GbaButton` position-aligned Right=0..Start=7; `pressButton` takes a raw `uint8_t`. |
| **Bindings are pure TypeScript** | `bindingMap.ts`'s header comment cites a native `BindingMapJson` in `config/UserConfigSerialization.hpp`; that file no longer exists. No C++ binding work for any new button name. |
| **`PerChannelRouter` / `renderAudioPerChannel` size N from `channelLayout()`** | `BlockRunner.hpp:96-105`, `EngineRpcService.cpp:271-312`. |
| **`SnapshotRegistry::claim` is fully generic** | `SnapshotRegistry.cpp:32-90` - frame slot from `framebuffer()`, state slot from `stateSnapshotCapacity()`, SRAM published live from `saveSramBytes()` when `stateRegions()` is empty (the Mesen path). |
| **The tracker/DSP/persistence spine is platform-blind** | Zero platform literals in `src/tracker/`, `dspKernel.ts`, `kernelProjection.ts`, `midiRouting.ts`, `projectPaths.ts`, `projectBinaries.ts`, `recentSerialization.ts`, `unsavedChanges.ts`, the CLI, the store graph. |
| **`validSplits` degrades correctly** | `ui/lvgl/render.ts:108-113` pushes `channels` only for sameboy-core or NES, `pins` only for NES; an unknown platform lands on `["mix"]`. |
| **`src/render/types.ts:12` already has `"other"`** | A genuinely different `Platform` union from `platform.ts:15`; the render library will not crash without a case. |
| **Persistence is additive** | Adding `"sms"` and `"gg"` to `platformSchema` (`projectConfig.ts:91`) needs no `K_PROJECT` bump (currently 3 at `:199`) and no `migrate.ts` step. `projectConfig.ts:91` is the only platform enum in any versioned root. |
| **CI YAML needs zero change** | `build.yml` / `release.yml` run `./build.sh` and package a fixed artifact list; neither enumerates systems, cores, platforms or test names. (Coverage is a different matter, Tier 1 item 17.) |
| **A battery-less system is an already-tested shape** | `detectBattery` (`systemsStore.ts:622`), `SystemView.battery` gating Save-SRAM rows (`menuDefs.ts:478`), the savestate fallback (`ui/lvgl/render.ts:61`), the recents-label drop (`projectStore.ts:180`), `test/systems/battery.test.ts`. |
| **UI is otherwise platform-free** | Grepping all of `packages/retroplug/ui/`: the only platform literals are `lvgl/render.ts:108-111` and `menuDefs.ts:456-457`. |
| **`run-native-tests.mjs` has no test manifest** | No registration row needed; `__REPO_RESOURCES_DIR__` is already injected at `run-native-tests.mjs:109`. |
| **No extension collision** | No other console claims `.sms`/`.gg` in the `TryLoadRom` chain (`Emulator.cpp:566-572`); `Gameboy.h:74` is `.gb/.gbc/.gbx/.gbs`, `SnesConsole.h:75` has `.gb/.gbc` but not `.gg`. |
| **`GameGearPanningReg` is not a second silence trap** | Defaults `0xFF` (`SmsTypes.h:165`), all channels both sides. |
| **Test ROMs need no license bundle row** | `release.yml` packages only `build/bin` targets plus the license bundle; `resources/` never ships. |

### Free only because GBA is also missing it

GBA today has zero role knobs, zero menu rows of its own, no `debugTarget()`, no `writeCpuByte`, no `channelLayout()` stems, no MIDI/serial path, and a `render --help` string that never got `.gba` (`cli/renderArgs.ts:24`). Its boot leg `app-cores.test.ts:19` points at `__RESOURCES_DIR__` rather than `__REPO_RESOURCES_DIR__`; locally that resolves to `/workspaces/resources` which does contain `nanoloop287d.gba`, but **on CI the sibling tree is absent and `be.fileExists()` returns early**, so GBA has effectively zero CI coverage. Copying the GBA shape reproduces that hole.

---

## 5. Tier 1 - MVP: SMS and GG ROMs boot, render, make sound, take input, and sync sample-accurately

Ordered so each phase ends in an exit-zero. Items 3/4/5/6 are the diff from the clone in item 2, not separate work fronts. D1 and D2 are settled, so the old item 0 (decide one platform or two) is gone and its consequences are folded into the rows.

| # | Item | Files | Effort | Risk | Why |
|---|---|---|---|---|---|
| 1 | `MesenSmsConfig.hpp` | new `packages/native/src/system/mesen/MesenSmsConfig.hpp` | trivial | low | Copy of `MesenGbaConfig.hpp` (45 lines). **Must be named `MesenSmsConfig`** - Mesen has a global `struct SmsConfig` at `SettingTypes.h:695`, the exact collision documented at `MesenGbaConfig.hpp:12-17`. Drop `biosPath`. Carries the model discriminator only as a boot-time selector, not a user knob (D1). The `using Tag = rfl::Literal<"gba">` at `MesenGbaConfig.hpp:20` is vestigial: `rfl::TaggedUnion` appears once repo-wide. Copy for symmetry or drop; do not build on it. |
| 2 | `MesenSmsSystem.{hpp,cpp}` | new, in `mesen/` | **medium-large** | **medium** | Boilerplate is a structural clone of `MesenGbaSystem` (115 + 470 lines): `saveStateBytes` / `loadStateBytes` / `clearSram` / `clone` / `setGainDb` / `finishBlock` / `stateSnapshotSize` / `captureStateSnapshot` are literal copies. **`stepIfBelowTarget` is NOT** - see section 2.3, and D2. `onDeactivate` is **not** a literal copy either: it needs `emu_->Stop(false, true, false)` before `emu_.reset()` (bottom line (a)). `emu_->SetEmulationThreadId` in `prepareForBlock` (GBA does it at `:229`) or the parallel render path breaks; `mesenGlobalInit()` before constructing the Emulator or concurrent construction on `RenderJobRegistry` worker threads races. Gain: `gainSmoother_.next()` **once per sample frame across all lanes** (`MesenNesSystem.cpp:398-403`). One class serves both platforms; the `.sms`/`.gg` difference is config plus the sync transport. |
| 3 | `configureSms()` / `configureGg()` | `MesenSmsSystem.cpp` | trivial | **high** | Four zero-defaults, each a silent plausible-looking failure. **(a)** `ChannelVolumes[4] = {}` - see H4. **(b)** `Port1`/`Port2` default `ControllerType::None`, so no device is created, `pressButton` no-ops, **and** the `_controlDevices.size() > 0` guard in `SmsControlManager::UpdateControlDevices` (`SmsControlManager.cpp:50`) never fires, so every frame does `_deviceLock.AcquireSafe()` + `ClearDevices()` + two `shared_ptr` allocations on the audio thread. An RT-safety bug, not just dead input. **(c)** `RamPowerOnState = RamState::Random` (`SettingTypes.h:702`) noise-fills the always-allocated 32 KB cart RAM: measured `nonZeroBytes=32661` versus 0 with `AllZeros`, and the savestate goes **43686 -> 9202 bytes** (4.7x, the compressor cannot squeeze noise), feeding `stateSnapshotSize()` and every Duplicate. Also required for H5 determinism. **(d)** Overscan, item 5. **(e)** `EnableFmAudio` - see H3 and open question 3. Must run **before** `LoadRom` (region resolution depends on it, risk 10; and section 2.3's immutable-region limitation makes it the only chance). |
| 4 | **ROM identity: stage a real file with a real stem** | `MesenSmsSystem.cpp` | small | **high** | Three consumers of the VirtualFile name conflict: model selection (`SmsConsole.cpp:46-59`), battery stem (`Emulator.cpp:591` -> `SmsMemoryManager.cpp:92-96` `LoadBattery`, unconditional for non-SG/non-CV), and `Reset()` (`SmsConsole.cpp:148-152` -> `Emulator.cpp:347`, re-reads from disk). Measured: a constant `"rom.sms"` makes Reset a **silent no-op** and collides every game's battery on one file. **Resolution: materialize the bytes to a per-system temp file named `<real-stem>.<sms\|gg>`** under the existing Mesen tmp dir (`MesenGlobalInit.cpp:13`, `MesenGbaSystem.cpp:43`) and hand `LoadRom` that path. Model correct, battery stem unique per title, Reset a real power cycle, `spec.romBytes` overrides survive Reset. D1 makes the extension choice trivial: it comes from `spec.platform`, not a sniff. |
| 5 | Overscan + framebuffer size, **two configurations** | `MesenSmsSystem.{hpp,cpp}` | small | medium | **Measured: both models emit 256x240 with default (zero) overscan.** `SmsVdp.cpp:648` always emits a 256x240 `RenderedFrame`; `BaseVideoFilter::GetFrameInfo` (`BaseVideoFilter.cpp:33-36`) subtracts overscan; all three `SmsConfig` overscans default `{}` (`SettingTypes.h:717-719`) because Mesen's real per-console defaults live in the .NET UI you do not build. So SMS shows 24 black rows top and bottom and **GG shows a 160x144 image floating in a mostly-black frame** unless `GameGearOverscan` is `{48,48,48,48}`. Settled by D1: two platforms, two overscan configurations, two `FrameBufferTriple(width, height)` ctor dims. `MesenVideoDevice`'s min-clamp means a mismatch is a silently wrong picture, not a crash. |
| 6 | `SmsButton` + `toSmsButton` remap | `InputTypes.hpp`, `MesenSmsSystem.cpp` | trivial | low | Position-aligned Right=0..Start=7 like `GbaButton:52`. Mesen's native order is `{Up=0,Down,Left,Right,B,A,Pause}` (`Input/SmsController.h:58`) so an explicit switch is required as at `MesenGbaSystem.cpp:202-219`. Start -> `Pause`; **Select -> early return**, not `default: return A`, or Select spuriously fires button 2. Per H9, `Pause` on port 0 drives the Z80 NMI on SMS (`SmsVdp.cpp:600-602`) and reads as Start at `$00` bit 7 on GG (`SmsMemoryManager.cpp:456-464`), so the two platforms want different labels even though the wire byte is shared. |
| **7** | **Vendored: `SmsControlManager::SetExternalInput`** | `deps/mesen/Core/SMS/SmsControlManager.{h,cpp}` | small | **high** | **New, and on the critical path.** Section 2.4. Without it the sync counter degenerates to mod-2 and presents as an intermittent double-tempo bug rather than a dead line. 3 functional lines at `InternalReadPort`, plus a `// RetroPlug:` marker (there is no configure-time patch guard for `deps/mesen`, so the marker is the only inventory). |
| 8 | Vendored: `SmsControlManager::UpdateInputState` override | `deps/mesen/Core/SMS/SmsControlManager.{h,cpp}` | small | medium | **Confirmed empirically twice.** No override exists (full header read; GBA has one at `GbaControlManager.h:28`), so `SmsVdp.cpp:654` -> `SmsConsole.cpp:171` -> `BaseControlManager.cpp:158-176` runs `ClearState()` + `SetStateFromInput()` and wipes every bit set via `SetBitValue`, plus `KeyManager::RefreshKeyState()` on the audio thread. Probe: `post-frame Right still pressed = 0`, every run; device `shared_ptr` unchanged, so it is the clear not recreation. Same bug patched at `GbaControlManager.cpp:24-37`. No `ActiveKeys` cache to refresh, so the body is empty plus a comment. Item 7 makes sync immune to this; ordinary buttons are not. |
| 9 | **Vendored: GG EXT input model** (`.gg` only) | `deps/mesen/Core/SMS/SmsMemoryManager.{h,cpp}`, `SmsTypes.h` | small | medium | Section 2.4 Edit 3. `$01` is a bare loopback (`:396` write, `:467` read, both `//TODOSMS`), `$02` direction is stored and never consulted, `$04` serial rx is `return 0xFF` (`:470`). Measured: `write $01=0xAA -> read $01=0x2A`; GG sync is dead in stock Mesen. ~5 lines, fills an acknowledged stub, no regression (`GgExtInput = 0xFF` reproduces today's pull-up-high behaviour). |
| 10 | **Host-side: teardown `Stop(false, true, false)`** | `MesenSmsSystem::onDeactivate` | small | **high** | Bottom line (a). `Emulator::Stop` is public at `Emulator.h:148`; `preventRecentGameSave=true` is load-bearing both for the disk write and for H10's serialize-path blip overrun. **ASan does not catch this** (both frames in the uninstrumented `libmesen.a`), so prove it with 40 construct/destruct cycles in Catch2, 5 runs. Measured 5/5 clean with `Stop`, 3/5 crashes without. |
| 11 | `RomFormat::Sms` **and** `RomFormat::Gg` + `detectRomFormat` **and** `platform.ts detectPlatform` | `packages/native/src/system/RomFormat.hpp:13-18`, `packages/retroplug/src/platform.ts:15,21-25,35,62-70` | small | **high** | **One item in two languages, one commit.** `platform.ts:11-12` and `RomFormat.hpp:6-11` state they are deliberately mirrored; if they diverge, `classifyRom` says `"sms"`, TS calls `constructSystem`, and `MesenBackend`'s gate returns nullptr - a failed load with no diagnostic. Probe `$7FF0`/`$3FF0`/`$1FF0` for `TMR SEGA`, discriminate on `$7FFF` (`0x4c` SMS / `0x6c` GG), and handle the 512-byte copier header Mesen strips (`SmsConsole.cpp:38-42`, `size % 0x400 == 0x200`) which shifts every offset. D1 makes the `$7FFF` discriminator load-bearing rather than informational. Headerless SMS and all SG-1000 fail. `DEFAULT_CORE` is a `Record<Platform, Core>` (`platform.ts:21`) and is a hard compile error the instant `Platform` gains a member; two members, two rows. |
| 12 | Two sniff lengths, not one | `platform.ts:35`, `systemsStore.ts:32` | small | medium | `ROM_SNIFF_LEN = 0x134` is all `classifyRom` reads (`systemsStore.ts:50`, `backend.readFilePrefix`). **`ROLE_HEADER_LEN = 0x150` is a second, independent constant**, and it is the buffer fed to both `romHasBattery` (`:624`) and the ROM-provider registry (`:615`) - which is what an `sms-sync` marker role would have to match on. A `$7FF0` header is 32 KB past both. Either raise them (a ~100x read amplification on a path that also runs per `siblingRomCandidates` probe) or add a second offset-targeted read. Update the `platform.ts:1-12` header comment, which currently promises the opposite of what SMS will do. |
| 13 | `MesenBackend` `"sms"` **and** `"gg"` branches | `MesenBackend.cpp:73`-ish | trivial | low | Two blocks mirroring the GBA one at `:73-84`; gated on item 11 (`MesenBackend.cpp:54,74` are 2 of the 5 `detectRomFormat` callers). Update the "two platforms" class comment at `MesenBackend.hpp:22-26` - it becomes four. `SystemKind::MesenSms` (`SystemTypes.hpp:8-12`) is inert cosmetic bookkeeping. |
| 14 | One CMake line | `packages/native/CMakeLists.txt:48` | trivial | low | `MesenSmsSystem.cpp` into `retroplug-core`. Plus `RP_SMS_ROM_PATH` / `RP_GG_ROM_PATH` compile definitions alongside `CMakeLists.txt:645-646`. |
| 15 | The three ROM-extension lists + `platformSchema` | `fileSelection.ts:20-21`, `savPaths.ts:77` (and its doc comment at `:81-83`), `useProjectModals.ts:26`, `projectConfig.ts:91` | trivial | low | Three separate literal arrays with identical `[.gb,.gbc,.gba,.nes]` contents; worth collapsing onto one export while in there. Until the first two are updated the file dialog will not show an SMS or GG ROM; missing `savPaths.ts:77` silently breaks pick-a-`.sav` -> find-its-ROM. `platformSchema` is a bare `z.enum` and `parseConfig` drops failing entries at `projectConfig.ts:283-287`, so until updated an SMS system saved into a `.rplg` **silently vanishes** on load. Two members, per D1. |
| 16 | `MemoryType` support matrix + missing region tags | `packages/native/src/system/MemoryType.hpp:19-51` | small | medium | The 3-column per-core matrix comment (`:19-37`) needs an SMS column: Ram -> `SmsWorkRam`, Rom -> `SmsPrgRom`, Sram -> `SmsCartRam`, Vram -> `SmsVideoRam` (`SmsMemoryManager.cpp:42/52/91`, `SmsVdp.cpp:45`), no OAM (SMS sprites live in VRAM). There is **no tag for SMS colour RAM or the SMS BIOS ROM**; `:39-40` states the integer values are the RPC wire byte, `:53` fixes `kMemoryTypeCount = 9`, and `SystemBase::StateRegionTable` is a `kMemoryTypeCount`-sized array indexed at `SnapshotRegistry.cpp:73`. Adding a tag is a wire-format addition mirrored in `backend.ts:561-573` and `cli/sdk-types.d.ts:202`. |
| 17 | Battery, **both halves** | `platform.ts:80-84` **and** `MesenSmsSystem::saveSramBytes` | small | **high** | Not either/or. `romHasBattery` (currently `return true; // gba / anything else`) drives the **UI menu gating** at `menuDefs.ts:478` and the recents label; `saveSramBytes` drives the **actual `.sav` write** and `dirtySramTargets`. Fix only TS and Mesen's `BatteryManager` still writes `<saveFolder>/<stem>.sav`; fix only native and the Save-SRAM rows still show. Combined with the unconditional 32 KB `SmsCartRam` and `RamState::Random`, shipping neither means every SMS game writes a junk `.sav` under Continuous auto-save and trips the unsaved-changes prompt on every close. Note smsggdj **is** a battery cart (SMDJ4 save format lives in cart SRAM at `$8000-$BFFF`), so "always false" is not a free answer. |
| **18** | **Native guards in `retroplug-audio-test`** | `packages/native/test/audio/Sms*.test.cpp`, `packages/native/CMakeLists.txt:618-651` | **medium** | low | **Highest value per line in the document, and it cannot be a clone.** Section 2.8. Six assertions nothing else covers: non-silent PSG output over a real ROM (catches item 3a, which a boot smoke test passes while silent); a held level still asserted after a `RunFrame` (catches items 7/8 - `app-cores.test.ts` cannot, it reads only `be.getFrame(id)?.published` at `:40`, and `app-input.test.ts:18-19` hardcodes `GameboyButton` and is mGB-only); 40 construct/destruct cycles without a segfault (catches item 10); **block exactness**; **gate-metric fidelity**; **cadence invariance**. The last three are a new test shape needing a live `SmsConsole` in a Catch2 binary, plus H5 determinism pinning. Extend the existing binary, do not add one - the binary list is duplicated in `package.json:20` **and** `packages/retroplug/scripts/run-plugin-tests.mjs:27`, both listing six. `NesStems.test.cpp` is the precedent for driving a real ROM. |
| 19 | Vendor the test ROMs | `resources/roms/` | trivial | low | `smsggdj_v0_45.{sms,gg}`, 131,072 bytes each, currently only at `/workspaces/resources/roms/smsggdj/`. Without them item 18 skips silently like `tools/author-risa-rplg.js:24-27` and CI proves nothing. MIT, `/workspaces/smsggdj/LICENSE`. `resources/` never ships (`release.yml`), so no notices row. |
| 20 | The boot-and-render legs | `packages/retroplug/test-native/app-cores.test.ts` | trivial | low | `bootsAndRenders(SMS, "sms", warmupMs)` and the `"gg"` twin, mirroring `:49-50`. **Source from `__REPO_RESOURCES_DIR__`, not `__RESOURCES_DIR__`**, or they silently skip on CI exactly as GBA does. |
| 21 | `sms-sync` role + `SmsSyncRole` | `dspRoles.ts`, `romProviders.ts`, `mesen/roles/SmsSyncRole.{hpp,cpp}`, `pluginControlPlane.ts:99-107` | **medium** | **medium** | Section 2.4. `SmsSyncRole` is `NesN8FifoRole` minus `flushAll`; the TS role is `risaSync` with a level encoder instead of a byte protocol. Add the PDC entry, and the missing `risa-sync` one while you are there. Needs item 12 first, or the marker role has zero header bytes to match on. |
| 22 | Verification legs the plan must actually run | - | small | low | `pnpm -r typecheck` (`package.json:14`) is in **no** workflow step (`build.yml:67-77,169-179,333-343` run only `test` / `test:native` / `test:plugin` / `test:ui`), and item 11's `DEFAULT_CORE` change is a hard compile error nothing else catches. Plus `tools/run-sanitizer.sh` (`thread` for `mesenGlobalInit()` under concurrent construction on `RenderJobRegistry` worker threads, and for item 3b's audio-thread allocations). **Note ASan will NOT catch item 10.** |

---

## 6. Tier 2 - first class (parity with how NES is treated)

| Item | Files | Effort | Risk | Why |
|---|---|---|---|---|
| `defaultCoreFor` backstop | `EngineRpcService.cpp:48-52` | trivial | low | `if (platform == "nes" \|\| platform == "gba") return "mesen";` needs `"sms"` and `"gg"`. TS always sends core explicitly, but omitting this makes a platform-only wire spec silently route SMS to SameBoy and return nullptr. `BackendTypes.hpp:76` enumerates the platform strings in a doc comment. |
| `pressButtonAt` overload | `SystemBase.hpp`, `Engine.cpp:115`, `DspRuntime.cpp`, `dspKernel.ts:438`, `dspKernelBundle`, `SystemCtx` type | small | low | Section 2.4. Additive overload with a forwarding default, so `SameBoySystem` / `MesenNesSystem` / `MesenGbaSystem` behaviour is bit-for-bit unchanged and SameBoy's load-bearing 10 ms synthesized spacing survives. Closes the three dead lines. Not needed for SMS sync (which rides `pushCoreBytes`), but it is the generic fix and only two role call sites use `ctx.pressButton` today (`dspRoles.ts:72-73,187-188`). |
| Settings-menu gating | `menuDefs.ts:456-467` | trivial | low | NES knob rows are gated on `sys.platform === "nes"`; SMS and GG each need their own gate or they inherit nothing (and must not inherit NES-only rows). |
| Render library | `src/render/render.ts:61-69`, `src/render/types.ts:12` | small | low | `platformOf` classifies by **file extension** here, unlike the store's magic-byte `classifyRom`. Convenient for SMS/GG (extension is what Mesen wants anyway) and it makes D1 trivial on this path; worth a comment. Without a case, `.sms` falls to `"other"`: mix render works, no auto-start gesture (`:494-498`) and no song-end probe (`:440-467`). |
| TS test rows + fixtures | `test/systems/fixtures.ts:43`, `kind.test.ts:17`, `paths/rom-format.test.ts:37-39,65-67`, `paths/siblings.test.ts:79-115`, `systems/battery.test.ts:46`, `store-mutate.test.ts`, `list.test.ts`, `project/migrate-core.test.ts`, `project/migrate-string-enums.test.ts:42-44`, `render/ui-seam.test.ts:166`, `system-settings/store.test.ts:57-60`, `system-settings/registry.test.ts:91` | small | low | Mostly the **same commit** as Tier 1 item 15: `siblings.test.ts` asserts the exact 4-extension candidate list and goes red the moment `savPaths.ts:77` changes. `testing/mockBackend.ts:272` rejects `constructSystem` when `detectPlatform` returns unknown, so an SMS fixture fails at construct until item 11 lands and will look like a store bug. |
| `sms-sync` TS tests | `test/dsp/dsp-sms-sync*.test.ts` | small | low | risa carries three (`dsp-risa-sync`, `-grid`, `-locate`); SMS needs the counter state machine (START/CONTINUE/CLOCK/STOP-freezes), the arm-on-Play hold, seek re-phasing, and the active-low level encoding. The STOP-freeze and the inversion are the two that will actually catch bugs. |
| Real-core Mesen legs | `test-native/app-mesen-persistence.test.ts`, `app-mesen-settings.test.ts` | small | low | The natural home for SMS and GG construct-blob + live-knob proofs, mirroring the NES rows. |
| Per-platform button labels | `menuDefs.ts:1327` (`GB_BUTTONS`) | medium | low | Cosmetic but now motivated by H9: SMS Start is Pause/NMI, GG Start is a real Start. Bindings are one global keyboard profile plus one global gamepad profile (`bindingsStore.ts:115-127`), not per system, so keep the 8 wire names as storage keys and add a display-label map driven off the focused system's platform (SMS: A -> "Button 2", B -> "Button 1", Start -> "Pause", Select hidden; GG: Start -> "Start"). Forking the profile schema would be a `spec/05` version bump for no functional gain. |
| `reaper:sms-sync` render leg | `tools/run-reaper-suite.sh:35` + author script + `.lua` + `.rpp` fixture | medium | medium | Clones `reaper:risa-sync`. **Must script the ROM into `SYNC: IN` through its own UI first** - `$DD` is untouched at boot. Resolves ~25 ms at best (section 2.8), so it is a drift and gross-regression guard, not an accuracy proof. Known failure mode: a stale `.rpp` embedding an old VST3 class id so Reaper loads the FX offline (silent render, not an error). Never CI. |
| CLI + docs + release checklist | `cli/renderArgs.ts:24,59-63`, `cli/timeline.ts:14`, `cli/sdk-types.d.ts:346`, `SystemFactory.hpp:19`, `CoreBackends.hpp:6`, `SystemBase.hpp:23-24,194,211,290-291,362`, `SnapshotRegistry.hpp:86`, `systemsStore.ts:43-45`, `DebugRpcService.{cpp,hpp}` (14 "null on SameBoy/GBA" comments), `spec/00:4,91`, `spec/01:362`, `spec/03:77`, `spec/04:35`, `spec/05:33`, `spec/07:26-29,48`, `spec/09:230`, `spec/README:39-40`, `README.md:166`, `docs/sdl-standalone.md:35`, `RELEASE_TESTING.md:35-41` | small | low | `renderArgs.ts:24` still says "Render a Game Boy (.gb/.gbc), or NES (.nes) ROM" - it never got `.gba`, so fix both. `RELEASE_TESTING.md:35-41` needs SMS **and** GG rows in the manual ROM-kinds checklist. **Stale independent of SMS, fix while you are in there:** `spec/04:34-35` claims "there is exactly one system-role today (sameboy); Mesen exposes no natively-consumed knobs yet" (the NES region / sprite-limit / apuLatencyMs work invalidated it); `spec/05:32` says "`core` is deliberately not stored" while `:181` documents the migration that stores it; `spec/06`'s target table omits `retroplug-sdl` / `-watcher-test` / `-midi-test` / `-lottie-test` / `-render-host-test`, `:193` cites a `BUILD_CLI` block at `CMakeLists.txt:554-574` in a file that is now 312 lines, and it says "6 renders + 3 editor checks" where `run-reaper-suite.sh:35` lists 7 (8 with `sms-sync`); `romProviders.ts:2` cites `sameboy/RomSniffer.cpp` which does not exist; `AGENTS.md` names four Catch2 binaries where `package.json:20` and `run-plugin-tests.mjs:27` list six. |
| Tile geometry (recommend: **do not** do this) | `ui/screens/grid/layout.ts:9-10,28-34` | large | medium | `GB_NATIVE_W/H = 160/144` is the only tile sizing, and NES 256x240 and GBA 240x160 have letterboxed via `LV_IMAGE_ALIGN_CONTAIN` for a year. Per-system means the grid stops being a uniform lattice and ripples into `gridContentSize` / `getTileBounds` / `fitZoom` / `hitTestTile`, `App.tsx:109`, `main.tsx:47` (which must agree with App or the window bounces on first frame), `test/file-drop/hit-test.test.ts`, and the three `reaper:editor*` checks. Pre-existing wart. D1 helps here: Game Gear at 160x144 is a pixel-perfect fit for the existing tile once `GameGearOverscan` is set, so only Master System is affected. |

---

## 7. Tier 3 - the RetroPlug-specific value

| Item | Files | Effort | Risk | Why |
|---|---|---|---|---|
| Region / Revision / FM role knobs | `coreRoles.ts:47-65`, `settingsEnums.ts:118-127`, `Engine.hpp:20-29`, `Engine.cpp:286-301`, `EngineRpcService.cpp:118-145` | small | low | The `"mesen"` role is keyed by **core**, so it already attaches to an SMS system and decodes tolerantly (`rfl::DefaultIfMissing`, `MesenBackend.cpp:39`). Its `region` field is `REGION_VALUES` = Mesen's shared `ConsoleRegion` 0..4, reusable verbatim. But `Engine::applyConfigField` dispatches via `dynamic_cast<MesenNesSystem*>` (`Engine.cpp:287`), so an SMS system fails the cast and every knob silently no-ops while the UI cycler still looks live. **Region is a special case under D2**: `SmsConsole::UpdateRegion` is private and only reachable from `RunFrame`, which the sample-accurate loop bypasses (section 2.3), so a runtime region change cannot propagate at all - the knob must be construct-time or the row must be omitted. GG's separate `GameGearRegion` field (`SmsConsole.cpp:185-189`) is a second, independent knob, which D1 makes clean. Also note a live role-knob apply re-enters `UpdateControlDevices` (`SmsControlManager.cpp:46-71`), which `ClearDevices()`s and rebuilds, dropping held bits - but not the item 7 external-input mask, which is manager state. |
| PSG 4-stem tap | `deps/mesen/Core/SMS/SmsPsg.{h,cpp}` + host `channelLayout()` | medium | medium | **Structurally easier than NES.** The four `channelOutput` values exist at `SmsPsg.cpp:75,85` and are summed with plain linear `int16` addition, so the stems **re-sum exactly** - no "does not sum" caveat like the NES pins in `spec/10` section 3b. Mirror `NesSoundMixer.h:65-127` (`SetChannelCapture` / `AvailableCaptureFrames` / `DrainChannel`, off by default so the mix path stays byte-identical). Complications: `SmsPsg` feeds the **shared** `SoundMixer`, so there is no existing per-console hook to extend; `blip_set_rates` must re-run in `SetRegion` (`SmsPsg.cpp:31-38`) and the stream blips clear in `Serialize` (`:167-173`); `IsPsgAudioMuted` zeroes the whole buffer (`:117-119`) so a tap ignoring it leaks. **Do not forget the mix-path drain-and-discard guard** (`MesenNesSystem.cpp:416-424`) or a capture-armed system driven through the mix path accumulates rings and a later `renderAudioPerChannel` satisfies its block without stepping the CPU. `ChannelStream::name` is a `std::string_view` (`SystemTypes.hpp:18-21`) requiring a static literal. Interaction with D2: a second gating metric needs the same discipline as the NES capture path, and it must not become the sample-position source (that stays `CycleCount`). |
| **PSG stems fit the plugin split** | `Engine.cpp:166-172`, `AudioRouting.hpp` | - | - | `Engine.cpp:168` requires `2*n <= numOutputs` with 8 plugin outputs, so a PSG-only **4-stereo-stream** layout is exactly 8 lanes and drives the existing `ChannelSplit` router with zero new plumbing - while open question 5 already leans stereo for `GameGearPanningReg` fidelity. **SMS/GG would be the first non-GB console usable in the plugin split.** "CLI-only, same as NES" becomes true only once FM is folded in. |
| FM (YM2413) stems | `SmsFmAudio.cpp:34`, `SmsConsole.h:47` | medium | medium | **No emu2413 edit needed.** `OPLL::ch_out[14]` (`emu2413.h:121`) already holds each voice's per-sample output and `mix_output` linearly sums them (`emu2413.cpp:1065-1069`); because `SmsFmAudio` constructs `OPLL_new(clk, clk/72)`, `conv` stays NULL, so reading `_opll->ch_out[i]` right after the `OPLL_calc` at `SmsFmAudio.cpp:34` is an exactly-summing decomposition. **The blocker is one accessor**: `SmsConsole::_fmAudio` (`SmsConsole.h:28`) is private with no getter, unlike `GetPsg()` at `:47`. FM is on a completely different path from the PSG (an `IAudioProvider` resampling `clk/72` straight to the host rate and adding into the already-resampled mix, `SoundMixer.cpp:99-101`), so PSG and FM stems cannot be tapped at one place. Under D2 this interacts with H3: any FM stem inherits the cadence-dependent discard. |
| Lane-count ceiling | `BlockRunner.cpp:55-62` | trivial | medium | `constexpr kMaxLanes = 32; float* outs[kMaxLanes];` guarded **only by an `assert`**, compiled out under NDEBUG, with an unbounded write loop. 4 PSG + 9 FM tone = 13 streams (26 lanes) fits; 4 + 14 (rhythm split) = 18 streams (36 lanes) is a silent stack smash in Release. |
| Z80 CPU-state virtuals | `MesenSmsSystem.cpp` | medium | low | ~80 lines, no core edits. `SmsCpuState` (`SmsTypes.h:22-63`) is a plain Z80 file; `SmsCpu::GetState()` returns a **mutable ref** so `setCpuRegister` writes directly with no pipeline-reload dance; `SmsCpu::Exec()` is a plain single-instruction step; `readCpuByte` -> `SmsMemoryManager::DebugRead(uint16_t)`. Separate from `IDebugTarget`, so basic peek/poke/step works with no debug session. Largely free once D2's step loop exists, since it already holds the `SmsCpu*`. |
| `MesenSmsDebugSession` | new | large | low | Strictly optional. `debugTarget()` defaults to nullptr (`SystemBase.hpp:263`), GBA implements none, and every `DebugRpcService` entry point null-checks. `deps/mesen/Core/SMS/Debugger/` already compiles and links, and the RetroPlug single-threaded `Debugger` patches are console-agnostic. But `IDebugTarget::getApuState`/`getPpuState`/`getExpansionAudioState` are NES-shaped structs (`DebugTarget.hpp:83-197`), so SMS would leave them `{}`. |
| `SYNC: OUT` (tracker as master, DAW follows) | `MesenSmsSystem.cpp`, a new TS role | medium | medium | The mirror of section 2.4. No per-write callback exists, but polling `SmsControlManager::GetState().ControlPort` in the fine phase of the step loop is one read per instruction and would give sample-accurate OUT detection **with no core edit**. Note only export (non-Japanese) SMS hardware can drive the port via `$3F` (`DESIGN.md:452-454`), and Mesen has a `//TODOSMS add UI option for japan vs overseas model` at `SmsControlManager.cpp:104` - the emulated read path currently behaves as export. |
| smsggdj tracker integration | `src/tracker/trackerIntegration.ts:65` + a marker-role provider + an assets role | large | **high** | See section 11. Scale reference: the risa integration was 69 commits, +17,577 lines. |

---

## 8. Suggested first commit

**`MesenSmsConfig.hpp` + `MesenSmsSystem.{hpp,cpp}` + `CMakeLists.txt:48` + `resources/roms/smsggdj_v0_45.{sms,gg}` + `SmsAudio.test.cpp` in `retroplug-audio-test`.**

Tier 1 items 1, 2, 3, 4, 5, 10, 14, 19, and the first three assertions of 18. **No `MesenBackend`, no `RomFormat`, no TypeScript, no `platform.ts`, no vendored edits.** The target already links `retroplug-backend` (which PUBLIC-brings `retroplug-core`) and already takes real ROMs as compile definitions (`CMakeLists.txt:644-646`); add `RP_SMS_ROM_PATH` / `RP_GG_ROM_PATH` and one source line.

This is the right first commit because it isolates the #1 non-sync risk (`ChannelVolumes` silence) before any plumbing exists to hide it, lands the sample-accurate `stepIfBelowTarget` from the start rather than retrofitting it over a `RunFrame` clone, and proves the teardown fix - and it ends in an exit-zero from `pnpm test:plugin audio`. **Vendoring the ROMs is part of this commit, not a follow-up**, or the assertions skip silently.

**Second commit, still zero TS:** the three sample-accuracy assertions (block exactness, gate-metric fidelity, cadence invariance) plus the H5 determinism pinning they require, plus the held-level assertion, which forces vendored items 7 and 8 to land. That is the commit that converts section 2 from a design into a guard, and per AGENTS.md's verification rule it is what makes the numbers in section 2.3 reproducible from the tree rather than from a scratch probe.

**Third visible milestone:** `build/bin/retroplug-cli render <rom>.sms --out /tmp/sms.wav --duration 2s`, mirroring the existing `cli:render-smoke` script. That needs items 1-14 (native) plus 11's TS half, because `render.ts:226` calls `ctx.project.systems.addSystem(o.rom)` which routes through `classifyRom` -> `detectPlatform`. It does **not** need items 15, 17, 20 or 21. It ends with a WAV you can listen to.

One structural note that shapes this: `packages/native/cli/` contains exactly one file, `main.cpp`, and it is a txiki launcher (`:46-50,77-80,150-151` eval a `.js` argv). There is **no C++-only "construct a system from a path" entry point**, and all 5 `detectRomFormat` call sites live inside the two backends. So the native/TS boundary must be crossed before anything is runnable through the normal path; the Catch2 route above is the only way to land and validate the native half first.

---

## 9. Open questions - decisions only you can make

*(D1, one platform or two, was open question 1 in the previous pass and is now settled as two. See section 1.)*

### The injection bound is now measured end to end, and it holds

Section 2.2 predicts a 0.402-sample injection bound from instruction size. That was an upper bound
derived from `Exec()` cost, not an observation. It is now measured through the whole path - role
queue, step loop, control port, emulated `IN` - by a ROM that counts its own polls while the line is
idle and freezes that count the instant it changes, so the count is a clock. The poll rate is
calibrated empirically (1.264 polls/sample measured, not derived from a T-state table) rather than
assumed.

| scheduled offset | 0 | 1 | 64 | 256 | 1024 | 2000 |
|---|---|---|---|---|---|---|
| observed | 0.00 | 0.00 | 63.28 | 255.51 | 1023.60 | 1998.96 |
| **error (samples)** | +0.00 | -1.00 | -0.72 | -0.49 | -0.40 | -1.04 |

Worst error is about one sample, consistently slightly EARLY, which is the expected sign: the ROM
samples the port roughly once per 0.8 samples, so it can catch a level up to one poll before the
measurement origin. Guard tolerance is set at 3 samples - three times the measured worst case, and
still three orders of magnitude tighter than the block-start delivery it must distinguish from.

The negative control is what makes that number mean something. Delivering everything at block start
instead (which is exactly what `MesenGbaSystem` does, having no offset path at all) collapses every
scheduled offset to **0** and breaks the rebase and ordering guards too, while all ten
delivery-only assertions keep passing. That is the difference between "the level arrived" and "the
level arrived on time", and only the second one is the product.

### The FM cadence numbers in section 2.6 H3 are still unverified in this tree

Worth stating plainly, because it is easy to assume otherwise now that a cadence guard exists. The
guard added in commit 2 ("SMS audio is invariant to the host block size") proves byte-identity
across block sizes 32 through 2048 - but **with FM off**. An attempt to measure the FM spread the
same way returned `maxAbs = 0` at every block size, which looked like good news and is actually
vacuous: the synthetic PSG ROM never writes `$F0`/`$F1`, so `SmsFmAudio::_fmEnabled` stays false and
the OPLL contributes silence no matter what `EnableFmAudio` says.

So H3's numbers (`rmsErr` 38.2 per-instruction, 3.5 coarse+naive-tail, 21.9 coarse+predictive) come
from the original probe harness and have not been reproduced here. Reproducing them needs a
synthetic ROM that actually keys a YM2413 voice: `$F2 = $01`, then a register triple to `$F0`/`$F1`
with the chip's write delay honoured. Until that exists, treat the FM-versus-cadence trade as
plausible but unmeasured in-tree, and note that the D3 decision it justified was made on those
numbers.

### FM resolved, and it opens a worse question

Found while implementing the first commit; both halves are verified in the tree.

**smsggdj uses the YM2413 heavily** - FM voices, rhythm mode, 8 user patches, per-track FM channels
(`/workspaces/smsggdj/src/main.asm:565-818`, `src/engine.asm:2967-3056`). So the escape hatch this
document leaned on twice ("set `EnableFmAudio = false` and the path becomes provably
cadence-invariant") is not available for the real ROM.

**And FM does not merely add a resampling residual: on Mesen it silences the PSG outright.** Mesen
models the Japanese SMS rule where port `$F2` MUXES rather than sums -
`SmsFmAudio::IsPsgAudioMuted()` returns true for `_audioControl == 1 or 2`, and
`SmsPsg::PlayQueuedAudio` then `memset`s the entire sound buffer
(`deps/mesen/Core/SMS/SmsPsg.cpp:117-119`). smsggdj writes `$F2 = $01` at boot whenever its FM
option is on (`src/main.asm:572-573`), and its own source comment names the divergence: *"sums with
PSG on real HW / SMSPlus; Emulicious muxes to FM-only"*.

Consequence: an FM-enabled smsggdj under Mesen loses **every PSG channel the tracker plays**, which
is three of its voices plus noise. This was measured, not reasoned: the first version of
`SmsAudio.test.cpp` asserted non-silent audio over the real ROM and got `peak = 0, meanAbs = 0` on
both `.sms` and `.gg`, with the config verifiably applied (the overscan assertions from the same
struct passed).

Handled for now by `MesenSmsConfig::enableFm` (defaults true, matching hardware), which gates the
whole `$F0/$F1/$F2` routing at `SmsMemoryManager.cpp:373` so turning it off restores the PSG. That
is a workaround, not an answer. The real question is now:

**Is Mesen's SMS FM model good enough to ship a tracker on?** Three options, none obviously right:
mux like Mesen and lose the PSG; sum like real hardware, which needs a vendored
`IsPsgAudioMuted` change and diverges from upstream Mesen for every other SMS game; or expose the
choice as a role knob and let the user pick. This also reshapes Tier 3's stem work - a PSG 4-stem
tap is worth much less if the PSG is muted whenever FM is on.

### Two smaller corrections from the same pass

- **ASan is NOT blind to the teardown UAF** (contradicting the note at Tier 1 item 10 and section 1).
  The root `CMakeLists.txt:49-57` applies `-fsanitize` via `add_compile_options` **before** the dep
  subdirectories, with a comment saying exactly that, so `deps/mesen` IS instrumented in
  `build-asan/`. Proven, not inferred: building `retroplug-audio-test` into `build-asan/` with the
  `Stop` call removed aborts **every** run with the exact frames

  ```
  ERROR: AddressSanitizer: heap-use-after-free
    #2 SoundMixer::UnregisterAudioProvider(IAudioProvider*)   SoundMixer.cpp:45
    #3 SmsFmAudio::~SmsFmAudio()                              SmsFmAudio.cpp:26
  freed by: std::default_delete<SoundMixer>::operator()
  ```

  and passes clean with it. So ASan is the **deterministic** detector and the Catch2 loop is the
  cheap probabilistic one, the opposite of what this document said. The "uninstrumented
  `libmesen.a`" claim holds only for the default `build/`. What remains true is that
  `tools/run-sanitizer.sh` would not catch it *as currently written*: it only builds
  `retroplug-host` and runs TS slugs, never the Catch2 binaries. Teaching it to also build and run
  `retroplug-audio-test` under `address` is a small, high-value addition that this document did not
  previously call for.
- **The Catch2 teardown guard is weaker than measured here.** Against a deliberately broken build
  (the `Stop` call removed) it crashed **1 run in 5**, not 3 in 5, and raising the loop from 40 to
  200 cycles did not improve the rate - the crash tracks process heap/ASLR state, not iteration
  count. It is kept because it is nearly free and never false-positives, but it should not be
  described as the primary detector.

- **`tools/run-sanitizer.sh thread` is already red before any SMS work, and Tier 1 item 22 should
  not be read as "make it green".** `dsp-lifecycle` fails under TSAN roughly half the time on a
  clean tree, with no ThreadSanitizer race report at all - just `expected 1, got 2` from
  `audio.systemCount()` at `packages/retroplug/test-native/dsp-lifecycle.test.ts:64`, because the
  preceding `audio.sleepMs(30)` is a fixed wait for a cross-thread removal that TSAN's slowdown
  outruns. Quantified by interleaved A/B of two TSAN host binaries, 14 runs each: **6 pass / 8 fail
  on both**, identical with and without this work. (Measuring the two builds separately rather than
  interleaved gave 4/6 versus 1/6 and looked like a real regression; it was machine-load noise.
  Interleave, or do not bother.) `dsp-threaded` passes consistently. The fix is a poll-until
  condition rather than a fixed sleep, and it is unrelated to SMS.

1. ~~**Does smsggdj use the YM2413?**~~ **ANSWERED: yes, heavily** - and the answer is worse than either branch this question anticipated. See "FM resolved" below.

2. **IN or IN24?** IN24 (divisor 6, 24 PPQN) is the DAW-shaped answer and matches the existing ares-link-sync / RP2040 bridge contract, but caps at 450 BPM NTSC / 375 PAL (3 clocks per frame before the mod-4 counter aliases) and at one row per frame (`engine.asm:742-750` has no loop). IN (divisor 1) gives 1:1 row control but the host must choose the row rate itself. This decides the role's tick resolution. A product question, not a technical one.

3. **Does `detectRomFormat` get an extension fallback?** The only genuinely undecided design point in the native seam, and it is a contract change (`RomFormat.hpp:6-11`), not an addition. Given the verified header data, the recommended shape: the content tier stays authoritative and unchanged for gb/nes/gba/sms/gg; the extension tier is consulted **only** when content returns `"unknown"`. That preserves the mislabelled-`.gb`-is-really-`.nes` guarantee while letting headerless SMS in. It changes `classifyRom`'s signature (`systemsStore.ts:50-51`) and the `readFilePrefix` doc contract (`backend.ts:82`), and it makes `classifyKind` (`fileSelection.ts:27`) start calling arbitrary `.sms`-named files ROMs.

4. **How do the two sniff lengths grow?** `ROM_SNIFF_LEN = 0x134` and `ROLE_HEADER_LEN = 0x150` both to `0x8000` for everyone, or a second offset-targeted read? The former is a ~100x read amplification on a path that also runs per sibling-ROM candidate. This also gates whether an `sms-sync` marker provider has any bytes to match on.

5. **Stems: mono or stereo, and is FM in scope?** Stereo matches the GB decision (`spec/10` section 2), is **required** for GG fidelity given `GameGearPanningReg`, and at exactly 4 stereo streams is the only layout that fits the 8-output plugin `ChannelSplit` path. Mono matches the NES precedent and halves the lane count. Separately: mix-only FM is free (it already lands in the stereo stream `MesenAudioDevice` receives); FM as one lumped stem; or 9 individual tone voices. In rhythm mode, do BD/HH/SD/TOM/CYM get their own streams or fold into one? 4 PSG + 9 FM = 13 streams fits under `kMaxLanes`; 4 + 14 does not. **Recommend stereo, PSG only initially.**

6. **What PDC value for `sms-sync`** (and, separately, for the already-missing `risa-sync`)? The LSDj 33 ms was measured, not derived. Needs a `tools/reaper-timing-analyze.py` pass against a real render once the leg exists. Note residual 6 in section 2.7: the ROM's poll phase within a frame is variable, so the value must be measured rather than computed from the frame rate.

7. **`SmsControlManager::SetExternalInput` versus `IInputProvider` versus the `$3F` hijack?** The external-input mask is 3 lines and immune to the per-frame clobber but is a real (if tiny) vendored addition; `IInputProvider` is reversible and upstream-friendly but still needs a TH model that stock Mesen does not have; the `$3F` hijack is zero edits and verified working but fires `LatchHorizontalCounter` on every counter increment and collides with the ROM's own `out ($3F),$FF` on stop. Owner's call on how much vendored surface is acceptable. Section 2.4 recommends the mask.

8. **Is `SYNC: OUT` wanted?** Polling `ControlPort` in the fine phase of the step loop gives sample-accurate OUT detection with no core edit (Tier 3), but only export SMS hardware can drive the port and Mesen's japan/overseas model option is an open `//TODOSMS` (`SmsControlManager.cpp:104`).

9. **Is a debug session wanted?** GBA set the precedent of none, and `spec/09:230` already documents the degradation. SMS would be the second console in that hole, though Tier 3's Z80 CPU-state virtuals fall out nearly free from D2's step loop.

10. **Does `MemoryType` grow a CRAM tag?** Wire-format addition (`MemoryType.hpp:39-53`, `backend.ts:561-573`, `cli/sdk-types.d.ts:202`). Cheap now, awkward later.

11. **Are SG-1000 (`.sg`) and ColecoVision (`.col`) in scope?** Both fall out of `SmsConsole` free, but SG-1000 has no detectable header and size-sniffs its cart RAM (`DetectSgCartRam`), and ColecoVision hard-requires `bios.col` (`FirmwareHelper.h:366-379`), uses a separate `CvConfig` with its **own** zero-defaulted `ChannelVolumes` (`SettingTypes.h:734`), and hardcodes overscan `{0,0,24,24}`. Recommend rejecting both explicitly at the backend.

12. **Performance on the A53/Anbernic target with the real ROM.** The cadence figures are x86 in this container, and the sample-accurate loop is **+9% over `RunFrame`**, not the -6% the design pass claimed. The profiling memory has Mesen NES at 0.79x realtime on an A53; SMS is a much lighter core, so this should be comfortable, but it has not been measured on-device and the headwind now points the wrong way.

---

## 10. Risks and traps, ranked by "days this eats"

1. **The TH sync line does not exist in Mesen, and the failure is a half-working counter.** `SmsController::ReadRam(1)` never drives bit `0x80` (`Input/SmsController.h:80-86`), so `GetTh(true)` (`SmsControlManager.cpp:115-119`) is pinned true and `$DD` bit 7 is stuck high. The mod-4 counter degenerates to mod-2 and `sync_in_delta` accumulates 1,3,1,3: **double tempo with per-frame jitter**, not a dead line. Whoever hits this first loses hours before suspecting `InternalReadPort`'s parameter, because the two lines that *do* work are the two the ROM ANDs together. Tier 1 item 7. This is the single most expensive thing in the document and it was invisible to the previous two passes.
2. **Teardown segfaults intermittently, and ASan will not tell you.** `SmsFmAudio::~SmsFmAudio` (`SmsFmAudio.cpp:22-26`) calls into a SoundMixer destroyed one member earlier (`Emulator.h:66` versus `:77`, empty `~Emulator` at `Emulator.cpp:82-84`). 10/20 runs in one probe, 3/5 runs of 40 cycles in another. Fires on every close-system / reset / project-unload / plugin teardown, in a DAW, non-deterministically. Both frames live in the uninstrumented `libmesen.a`, so `tools/run-sanitizer.sh` is blind. Host-side fix (`Stop(false, true, false)`), Tier 1 item 10, proven only by a construct/destruct loop.
3. **Silence by default.** `ChannelVolumes[4] = {}` (`SettingTypes.h:713`) times `SmsPsg.cpp:75,85`. Correct sample count, all zeros, so a boot smoke test passes. Reads as a plumbing bug in your audio path with nothing in the block runner or mixer to point at. One line in `configureSms`; a day to find without Tier 1 item 18.
4. **A bare `Exec()` loop starves and then silently corrupts the heap.** H1 plus H10: no host flush means 0 samples on a quiet ROM, and a >149,000 T gap makes the next PSG write (or **any savestate**, which RetroPlug takes every block) overrun `blip_new(4000)` with the `assert` compiled out under NDEBUG. The invariant is stronger than "flush so audio appears". One line to fix, unbounded to debug.
5. **The filename is the machine, the battery stem, and Reset.** No signature (`SmsConsole.h:39`), model from `GetFileExtension()`, battery stem from `FolderUtilities::GetFilename` (`Emulator.cpp:591`), Reset re-reads from disk (`Emulator.cpp:347`). Measured: a name not on disk makes Reset a **silent no-op**, and GG bytes named `.sms` render an entirely black screen (`nonBlackPx=0` versus `501423`). Three consumers, one string.
6. **Input clobbered every frame, plus a lock and two allocs on the audio thread.** `SmsVdp.cpp:654` -> `SmsConsole.cpp:168-172` -> unpatched `BaseControlManager::UpdateInputState`. Probe: held Right gone post-frame, every run. And with `Port1/Port2 = None`, `SmsControlManager.cpp:50`'s guard never fires, so `ClearDevices()` + two `shared_ptr` allocations run per frame in the DSP path. The GBA fix sits at `GbaControlManager.cpp:24-37` if you think to look.
7. **The FM cadence trap.** H3: with `EnableFmAudio` at its default of true, the flush cadence is an accuracy knob, and the variant that makes blocks exact is 6x worse for FM than the one that does not. Anyone who reads the design pass's "byte-identical at every cadence" claim without the FM-off qualifier will build a guard that passes on a synthetic ROM and fails on a real FM cart.
8. **The Catch2 guard you would naturally write proves nothing.** Both existing sync-timing tests deliberately bypass the emulator (`SameBoySerialTiming.test.cpp:65-80` sets `audioFrameCount_` by hand; `NesN8FifoTiming.test.cpp` has no emulator). A clone validates `pumpUntil` and says nothing about section 2.3. Section 2.8.
9. **Junk `.sav` files, a permanently-dirty project, and a 4.7x savestate.** 32 KB `SmsCartRam` allocated unconditionally (`SmsMemoryManager.cpp:85-91`) + `RamState::Random` (measured `nonZeroBytes=32661`) + `romHasBattery`'s `return true` + `SramTarget` having no `battery` field. Savestate `43686` versus `9202` bytes with `AllZeros`, feeding `stateSnapshotSize()` and every Duplicate. Mesen's own `BatteryManager` is live for SMS; the write side is memcmp-guarded (`:154-168`), the read side at `:94` is not.
10. **Silent data loss on the TS side.** `platformSchema` is a bare `z.enum` and `parseConfig` drops failing entries at `projectConfig.ts:283-287` with nothing surfaced. Miss `:91` and an SMS system saved into a `.rplg` disappears on reload, no modal, no log. Same on downgrade. Arguably the right trade versus bumping `K_PROJECT`, but record it in `spec/05` rather than leaving it folklore. D1 doubles the surface: two members to miss.
11. **`configureSms` must run before `LoadRom`, and region is immutable thereafter.** `UpdateRegion`'s `Auto` path sniffs the **filename** (`SmsConsole.cpp:184-195`), so a synthesized name without `(europe)` always resolves NTSC; and under D2 `UpdateRegion` is unreachable at runtime because it is private and `RunFrame`-only. Construct-time or nothing.
12. **The performance sign flipped.** The design pass claimed the sample-accurate loop is 6% *cheaper* than `RunFrame`; measured parity at best, and **+9% for the block-exact variant**. Any argument that rested on "negative risk delta" needs restating.
13. **Adding a Mesen system is cheap to land and expensive to carry.** GBA: 17 files / +592 in under 2 hours, then +379 / -80 more spread over 61 days and 13 commits as each cross-cutting `SystemBase` feature had to be re-done GBA-side. Today's seam is much better and most of that is now generic, but budget a tail of roughly the same size as the landing.
14. **`GgBlendFrames` defaults true** (`SettingTypes.h:708`) and blends consecutive frames for GG only. Any headless screenshot or deterministic render depends on frame history until it is turned off. D1 makes this a `.gg`-only config line rather than a conditional.
15. **`kMaxLanes` is assert-only.** `BlockRunner.cpp:55-62`, compiled out under NDEBUG, unbounded write loop. Only bites if stems go wide, but it is a stack smash.
16. **`ChannelStream::name` is a `std::string_view`** (`SystemTypes.hpp:18-21`) requiring a static literal. Generating `"FM 1".."FM 9"` into a temporary is a dangling-view bug.
17. **One gain per frame across all lanes.** `MesenNesSystem.cpp:398-403`. Per-lane advances the smoother N times too fast and the stems stop being a faithful decomposition.
18. **`pnpm -r typecheck` is not in CI.** `build.yml` runs `test` / `test:native` / `test:plugin` / `test:ui` only. `DEFAULT_CORE: Record<Platform, Core>` (`platform.ts:21`) breaks the instant `Platform` grows a member, and nothing automated catches it. "CI needs zero change" is true of the YAML and misleading about coverage.
19. **`deps/mesen` has no configure-time patch guard.** Unlike `cmake/sameboy.cmake`, which fails loudly if the tracked patch no longer applies, a lost SMS edit is silent. The 21 `// RetroPlug:` markers are the only inventory; all three SMS edits must carry one.

---

## 11. What SMS/GG support would NOT get

**Blunt version: even with sample-accurate sync, Tier 1 + Tier 2 ships an emulator with a clock input, not a RetroPlug system.**

RetroPlug's actual value is the tracker spine: `SongCatalog` / `AssetCatalog` / marker roles / the runtime WRAM overlay / the `retroplug-cli <x>-rom` verb family. After three to four weeks, SMS and GG would have:

- **A working sample-accurate sync leg** (that is the change from the previous pass; `sms-sync` is now Tier 1 item 21, not a Tier 3 aspiration).
- **No songs menu, no assets menu.** `TRACKER_INTEGRATIONS` (`trackerIntegration.ts:65`) gets no entry. No Load/Export/Replace/Delete/Add, no kits, no palettes, no fonts.
- **No MIDI.** `onMidi` / `pushSerialIn` stay default no-ops (`SystemBase.hpp:88-104`). `SYNC: MIDI` takeover is explicitly out of scope (section 2.4): the Z80 is the clock master of a bit-banged shift-in sampling DAT within ~3 microseconds of its own port write, which needs an in-core write-callback peripheral, not host scheduling.
- **No `SYNC: OUT`.** Tier 3, cheap (one `ControlPort` read per instruction in the fine phase, no core edit), but not in this scope.
- **No stems** unless Tier 3 lands, so no `System > Render` split; `validSplits` returns `["mix"]`.
- **No debugger**, second and third consoles after GBA in that hole, and 14 `DebugRpcService` comments to amend.
- **A letterboxed tile** for Master System, exactly like NES today. Game Gear fits pixel-perfect once `GameGearOverscan` is set (D1 makes that a clean two-configuration split).

**The tracker gap is closeable, and that is the genuinely interesting finding.** `/workspaces/smsggdj` is a real, MIT-licensed, source-available LSDj-inspired on-cart tracker for SMS + GG in Z80 assembly, with prebuilt ROMs staged. Its SMDJ4 save format is documented (`SAVEFORMAT.md`): cart SRAM at `$8000-$BFFF`, a 32-byte superblock with a `"SMDJ4"` magic, a 1024-byte directory of 32 x 32-byte entries carrying a valid flag / compression flag / heap offset+length / checksum / 8-char name, then an RLE heap of 6,912-byte song blobs. That maps almost one-for-one onto `SongCatalog` (`songCatalog.ts:9-50`): `list` and `isValidSav` are a header-only directory walk, `delete` is the documented shift-down compaction, `reorder` is a directory permutation, `importSongs` is a heap blob copy. The ROM side (8 sample kits, 8 recolourable UI palettes, 8 FM presets, all patchable without recompiling) maps onto `AssetCatalog`.

Three caveats before treating that as a plan:

1. **The directory has no "currently loaded slot" field**, and the SRAM window is superblock + directory + heap plus a separate 10-byte OPTIONS block at `$BF60`. `SongCatalog.workingName` is **required**, and `workingSongDirty` drives the recents-per-song rows and the destructive-load confirm. If the live song lives in console work RAM, this needs a runtime RAM overlay via `backend.readRam` (the LSDj reader pattern), not pure sav parsing.
2. **The format is still moving.** SMDJ4 is "v0.30+" with an existing SMDJ3 -> SMDJ4 migration tool. RetroPlug's tracker integrations are version-pinned by design (risa carries `isVersionSupported` gated on a bundled per-version RAM layout, `trackerIntegration.ts:59-61`). You would be signing up for that versioning burden against a moving target from day one.
3. **It is a separate project.** The comparable risa integration was 69 commits, 445 file-touches, +17,577 / -1,435. Scope it after generic SMS/GG emulation lands and after the format settles.

**Recommended shape:** Tier 1 + Tier 2 as a self-contained "SMS and GG play, and sync sample-accurately" milestone at three to four weeks, with the test ROMs vendored so the guards actually run in CI (unlike GBA's). Then decide on stems - materially more attractive now that 4 stereo PSG streams fit the plugin `ChannelSplit` path - and smsggdj as separate, independently-justified projects.

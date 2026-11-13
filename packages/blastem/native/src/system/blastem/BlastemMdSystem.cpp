#include "system/blastem/BlastemMdSystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemRom.hpp"

namespace rp::blastem {

// BlastEm's own serializer identifier. Its save_to_file writes these 8 bytes ahead of a state (serialize.c's sz_ident);
// a library build writes the state with genesis_serialize directly and never sees them, so this prefixes the same bytes
// and loadStateBytes demands them. Everything behind them is BlastEm's section stream, and anything else is a state this
// core cannot be: another serializer version, another console, another emulator.
static constexpr char kStateIdent[] = "BLSTSZ\x01\x07";

// A state's sections are a u16 id, a u32 size and exactly that many bytes (start_section/end_section, serialize.c), so a
// truncated or mis-sized body is visible before the serializer is asked to read it. The walk is what makes "refused"
// mean refused rather than "partly loaded": the mirror's latch catches a short FIELD inside an otherwise well-formed
// section, this catches everything else without touching the core.
static bool sectionsFit(const std::uint8_t* bytes, std::size_t size) {
    std::size_t at = 0;
    while (at < size) {
        if (size - at < 6) return false;
        const std::uint32_t len = (std::uint32_t(bytes[at + 2]) << 24) | (std::uint32_t(bytes[at + 3]) << 16) |
                                  (std::uint32_t(bytes[at + 4]) << 8) | std::uint32_t(bytes[at + 5]);
        at += 6;
        if (len > size - at) return false;
        at += len;
    }
    return at == size;
}

static std::uint8_t blastemRegion(std::uint32_t region) {
    switch (static_cast<MdRegion>(region)) {
        case MdRegion::NtscUs: return REGION_U;
        case MdRegion::NtscJp: return REGION_J;
        case MdRegion::Pal:    return REGION_E;
        case MdRegion::Auto:
        default:               return 0;  // the header's own region (set_region's region == 0)
    }
}

BlastemMdSystem::BlastemMdSystem(SystemId id, BlastemMdConfig config, std::vector<std::uint8_t> romBytes)
    : SystemBase(id), config_(std::move(config)), rom_(std::move(romBytes)) {}

BlastemMdSystem::~BlastemMdSystem() { destroyCore(); }

std::uint32_t BlastemMdSystem::masterClock() const { return gen_ ? gen_->master_clock : 0; }

std::uint64_t BlastemMdSystem::masterClockOf(std::uint64_t sample) const {
    if (sample <= sampleAnchor_) return mclkAnchor_;
    const std::uint64_t mclk = gen_ ? gen_->master_clock : 53693175u;
    // floor(n * MCLK / R) in exact integer arithmetic, from the anchor rather than from zero, so a rate change only
    // re-anchors and never introduces a rounding step (design section 2).
    return mclkAnchor_ + ((sample - sampleAnchor_) * mclk) / static_cast<std::uint64_t>(sampleRate_ + 0.5);
}

void BlastemMdSystem::onActivate(double sampleRate) {
    // A second activation on a live object keeps the machine. boot() would overwrite gen_, the only pointer to the
    // context, and so leak it whole with its aligned cart while silently dropping the machine's state; the sibling
    // backends guard the same way.
    if (activated_) return;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    // Sized for a core built outside a plan (a test, a tool), which reserves nothing: the engine's default plan block.
    const std::uint32_t frames = blockFrames(SystemBase::kDefaultBlockFrames);
    mixLeft_.resize(frames);
    mixRight_.resize(frames);
    audio_.prepare(frames);
    audio_.setHostRate(sampleRate_);  // before the chips exist: they ask for their source during alloc_config_genesis
    boot();
    gainSmoother_.setTargetValue(dbToLin(config_.gainDb));
    gainSmoother_.clearToTargetValue();  // the first value is the node's, not a ramp from unity
    SystemBase::reserveAudio(frames);    // LAST, per its contract
}

void BlastemMdSystem::boot() {
    if (!isMdRom(rom_)) return;
    // The callbacks BlastEm makes while allocating (render_audio_source, render_set_video_standard,
    // render_get_framebuffer) belong to this instance: the shim and the sinks read this thread's pointer.
    useInstance(this);
    void* cart = blastemRomCopy(rom_);
    if (!cart) {
        // An allocation failure is a refused build, not a crash: alloc_config_genesis byteswaps the cart in place.
        return;
    }
    // BEFORE alloc_config_genesis: the VDP asks render_get_framebuffer for its buffer at init (vdp.c:524) and points
    // context->output into it immediately (vdp.c:529-530), so a sink with no buffers yet is a null write on the first
    // line.
    video_.prepareBuffers();
    {
        // The allocation is not reentrant (BlastemCore.hpp's coreLifecycleMutex): BlastEm's own file statics are
        // handed through it. Only the allocation takes the lock, so a failed boot can be retried without holding it.
        std::lock_guard<std::mutex> lock(coreLifecycleMutex());
        gen_ = alloc_config_genesis(cart, static_cast<std::uint32_t>(rom_.size()), nullptr, 0, 0,
                                    blastemRegion(config_.region));
    }
    if (!gen_) {
        aligned_free(cart);
        return;
    }
    if (gen_->header.info.load_failed) {
        // The ROM database knows this cart, but only the map of a bigger image than the file we were handed (a trimmed
        // or overdumped copy). romdb.c refuses it the way it refuses a bad state (mirror: rom_info.load_failed) rather
        // than ending the process; the refusal reaches a caller as a system that never activated, which is how every
        // other boot failure here reads (the gate, the cart copy, the allocation), and the backend turns that into the
        // build error with the message.
        destroyCore();
        return;
    }
    useInstance(this);  // the context exists: from here, callbacks it starts belong to this instance
    // Port 2 is the host's device, not a pad: a game that reads player 2 sees the pins' idle state (which is a pad's
    // nothing-pressed state minus the pad's response), and a role claims the lines by pushing levels. Port 1 stays
    // whatever romdb.c chose, which is the pad the wire bytes drive.
    gen_->io.ports[1].device_type = IO_CUSTOM;
    m68k_reset(gen_->m68k);
    // The frame, once, for the region the core just chose: 240 lines on a PAL machine, 224 on an NTSC one (version_reg's
    // HZ50 bit, set by set_region during the allocation above). The shell's frame slot is sized at claim and never
    // resized, so this is the only place its size is decided.
    video_.prepareFrame((gen_->version_reg & 0x40) ? 240u : 224u);
    sampleBase_ = 0;
    sampleAnchor_ = 0;
    mclkAnchor_ = 0;
    nextGridSample_ = 0;
    port_.clear();  // a queued level's sample belongs to the timeline the four anchors above just restarted
    activated_ = true;
    setGainDb(config_.gainDb, true);
    // LAST, as the sibling backends do, and after m68k_reset (a seed laid in before the reset is a pattern the reset
    // overwrites): the battery first, and the savestate over it once this core can take one. The two calls are what
    // SystemBase::seedSramAndState does, except that the battery goes through seedBattery(): that helper writes its seed
    // through getMemory(Sram), which here is the address-ordered copy, so a 16-bit cart's seed would land nowhere.
    stateRefused_ = false;
    seedBattery(config_.sram);
    if (!config_.savestate.empty()) loadStateBytes(config_.savestate);
    if (stateRefused_ && !config_.savestate.empty()) {
        // A refused state: either it was thrown out before it touched the core (the identifier, the section walk) or it
        // latched after writing part of itself in (the serializer's short-field and impossible-field latches). Dropping
        // the seed and rebuilding ONCE from power-on is the one path that leaves no half-loaded core behind in either
        // case. Safe precisely because this is boot()'s last act: the core is fully built, nothing of ours is left
        // outstanding, and the lifecycle mutex is not held here (destroyCore and the retry's own allocation take it in
        // their own scopes, never nested). The retry sees an empty savestate, so it cannot recurse again, and the clear
        // above is what it keys on.
        config_.savestate.clear();
        destroyCore();
        boot();
        stateRefused_ = true;  // the refusal is still this instance's answer to the build request
    }
}

// The frame the sink allocated IS the picture's size from boot on. Before a core exists the region is unknown, so the
// answer is the frame a boot defaults to (the NTSC one), not zero: a caller that sizes anything off these reads a
// picture the machine could actually produce.
std::uint32_t BlastemMdSystem::coreFrameWidth() const { return video_.triple() ? video_.width() : 320u; }

std::uint32_t BlastemMdSystem::coreFrameHeight() const {
    // The region's visible lines (vdp.c's NTSC_INACTIVE_START and PAL_INACTIVE_START); prepareFrame() allocated the
    // frame from version_reg's HZ50 bit, so the triple's height is already the region's answer.
    return video_.triple() ? video_.height() : 224u;
}

std::uint64_t BlastemMdSystem::mclkNow() const {
    return gen_ ? static_cast<std::uint64_t>(gen_->cycle_base) + gen_->m68k->cycles : 0;
}

void BlastemMdSystem::destroyCore() {
    if (gen_) {
        useInstance(this);
        // Frees the aligned ROM copy too, and the chips' own frees hand their audio sources back to audio_ through
        // render_free_source, which is why currentInstance is set before this and the sink outlives the context.
        // The free takes the allocation lock (BlastemCore.hpp's coreLifecycleMutex): vdp_free touches the same
        // file-scope table state the allocation does.
        {
            std::lock_guard<std::mutex> lock(coreLifecycleMutex());
            gen_->header.free_context(&gen_->header);
        }
        gen_ = nullptr;
        activated_ = false;
    }
    // Ends by clearing the thread-local, and not inside the branch: a boot that failed set it too, and a pointer left
    // here outlives the instance it named, so the next BlastEm callback on this thread (a context built by whoever
    // runs next, with no system of its own) would dereference a freed system.
    useInstance(nullptr);
}

void BlastemMdSystem::onDeactivate() { destroyCore(); }

void BlastemMdSystem::onSampleRateChanged(double sampleRate) {
    if (sampleRate <= 0.0) return;
    // Re-anchor at the CURRENT sample, and move the anchor rather than mapping the anchor to itself (which is a no-op
    // and the bug this body exists for): masterClockOf reads sampleAnchor_, so the new anchor's clock has to be taken
    // BEFORE the rate changes, and the sample anchor moves with it so the next sample maps from here at the new rate.
    const std::uint64_t at = masterClockOf(sampleBase_);
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = at;
    sampleRate_ = sampleRate;
    gainSmoother_.setSampleRate(static_cast<float>(sampleRate_));
    // After the re-anchor, so the ring's 16 primed samples are silence at the NEW rate and every blip resamples from
    // the native rate to this one (which is also what restates each blip frame's budget in native clocks).
    audio_.setHostRate(sampleRate_);
}

void BlastemMdSystem::setGainDb(float dB, bool immediate) {
    // ExpSmoother's own shape (util/ExpSmoother.hpp), as MesenSmsSystem::setGainDb uses it: the smoother holds a linear
    // gain, so the dB is converted once, and `immediate` snaps rather than ramps.
    config_.gainDb = dB;  // config() is what a later activation boots from, so the knob is the record
    gainSmoother_.setTargetValue(dbToLin(dB));
    if (immediate) gainSmoother_.clearToTargetValue();
}

// The work RAM in the 68k's address order. BlastEm's storage is a word array whose byte view pairs each address's two
// bytes the other way round (BlastemMemory.cpp's wordOrder says why), so an address-ordered view is a copy: refreshed
// on every ask, into this instance's own buffer, which outlives every accessor it hands out. The registry copies it
// into its triple in the same block it asked in, and the buffer keeps the size it grew to, so no ask allocates after
// the first one.
std::span<const std::uint8_t> BlastemMdSystem::workRamView() {
    if (!gen_ || !gen_->work_ram) {
        ramView_.clear();
        return ramView_;
    }
    // WorkRamView owns nothing but the mapping, so it is made on the spot over this buffer.
    const auto*       raw = reinterpret_cast<const std::uint8_t*>(gen_->work_ram);
    const std::size_t bytes = sizeof(*gen_->work_ram) * RAM_WORDS;  // gen->work_ram is a word array of RAM_WORDS
    return WorkRamView(ramView_).refresh(raw, bytes);
}

rp::MemoryAccessor BlastemMdSystem::getMemory(rp::MemoryType type, rp::AccessType access) {
    if (!gen_) return {};
    switch (type) {
        case rp::MemoryType::Ram: {
            // The refresh and the view come from one call: a second one could refill the buffer the first returned.
            workRamView();
            // READ, whatever was asked: this is the address-ordered copy, so a write through it succeeds and changes
            // nothing in the core. memory() is the write half.
            return {type, rp::AccessType::Read, ramView_.data(), ramView_.size()};
        }
        case rp::MemoryType::ExtWorkRam:  // the Z80's own 8 KiB (genesis.c's Z80_RAM_BYTES, one calloc), not a copy
            if (!gen_->zram) return {};
            return {type, access, gen_->zram, Z80_RAM_BYTES};
        case rp::MemoryType::Sram:
            // The battery in its .sav's order, which is what the registry publishes and what every other emulator's save
            // holds: an odd- or even-byte cart's is already the compacted range, and a 16-bit one is pair-swapped out of
            // BlastEm's storage. A copy, so a write through it goes nowhere: sram() is the write half.
            batteryView();
            return {type, rp::AccessType::Read, sramView_.data(), sramView_.size()};
        default:
            return {};
    }
}

std::span<const std::uint8_t> BlastemMdSystem::batteryView() {
    if (!hasBattery(gen_)) {
        sramView_.clear();
        return sramView_;
    }
    const std::size_t bytes = gen_->save_size;
    sramView_.resize(bytes);
    for (std::size_t i = 0; i < bytes; ++i) sramView_[i] = gen_->save_storage[batteryStorageIndex(gen_, i)];
    return sramView_;
}

std::vector<std::uint8_t> BlastemMdSystem::saveSramBytes() const {
    // The battery is BlastEm's storage re-ordered, so the host's dump is a copy of the same view the accessor hands out.
    // No instance pointer is set: this reads the context's fields and calls no BlastEm function.
    if (!hasBattery(gen_)) return {};
    std::vector<std::uint8_t> out(gen_->save_size);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = gen_->save_storage[batteryStorageIndex(gen_, i)];
    return out;
}

void BlastemMdSystem::seedBattery(const std::vector<std::uint8_t>& sram) {
    if (sram.empty() || !hasBattery(gen_)) return;
    // The seed is in the caller's order (the .sav's) and BlastEm's storage is the pair-swapped one, so each byte goes to
    // its own index. A seed longer than the battery is cut to it, as SystemBase::seedSramAndState cuts one.
    const std::size_t n = std::min(sram.size(), std::size_t(gen_->save_size));
    for (std::size_t i = 0; i < n; ++i) gen_->save_storage[batteryStorageIndex(gen_, i)] = sram[i];
}

// The state, as BlastEm writes it, behind the identifier loadStateBytes demands. The capture costs one buffer of its
// own: BlastEm grows the one init_serialize gave it, and the caller's vector is the copy out of it.
std::vector<std::uint8_t> BlastemMdSystem::saveStateBytes() const {
    if (!gen_) return {};
    useInstance(const_cast<BlastemMdSystem*>(this));  // the serializer's own callbacks are frontend calls
    serialize_buffer buf;
    init_serialize(&buf);
    // The pc the 68k will resume at is the address sync_components is handed at a block boundary (genesis.c passes
    // gen->m68k->pc there), so a state taken between blocks resumes on the next instruction rather than mid-one.
    genesis_serialize(gen_, &buf, gen_->m68k->pc, 1);
    if (buf.failed) {  // the capture could not grow: no state is better than half a state
        free(buf.data);
        return {};
    }
    std::vector<std::uint8_t> out(sizeof(kStateIdent) - 1 + buf.size);
    std::memcpy(out.data(), kStateIdent, sizeof(kStateIdent) - 1);
    std::memcpy(out.data() + sizeof(kStateIdent) - 1, buf.data, buf.size);
    free(buf.data);
    return out;
}

// A seed state, into a core that was just built and has run nothing. Returns false and latches stateRefused_ on any
// refusal; boot()'s tail is what turns a refusal into a machine that booted from power-on instead.
bool BlastemMdSystem::loadStateBytes(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kIdent = sizeof(kStateIdent) - 1;
    if (!gen_ || bytes.size() <= kIdent || std::memcmp(bytes.data(), kStateIdent, kIdent) != 0) {
        stateRefused_ = !bytes.empty();  // nothing was asked for is not a refusal, an unusable state is
        return false;
    }
    useInstance(this);  // the deserializers hand their consoles' audio sources to this instance's sink
    if (!sectionsFit(bytes.data() + kIdent, bytes.size() - kIdent)) {
        stateRefused_ = true;  // truncated, or its sizes are lies: nothing has been touched
        return false;
    }
    deserialize_buffer buf;
    init_deserialize(&buf, const_cast<std::uint8_t*>(bytes.data()) + kIdent, bytes.size() - kIdent);
    genesis_deserialize(&buf, gen_);
    if (buf.failed) {
        // A well-formed section whose payload was short or impossible: the core is now PARTLY loaded, so it has to be
        // rebuilt. That is boot()'s job and not this function's: loadStateBytes runs inside seedSramAndState, which
        // boot() calls as its LAST act, so rebooting from here would recurse through a half-built boot. This only flags
        // the refusal; boot()'s tail sees the flag, drops the seed, destroys the core and boots once more from
        // power-on. The seed stays in config_ because that tail keys on a non-empty savestate, and clearing it here
        // would only leave the half-loaded core in place.
        stateRefused_ = true;
        return false;
    }
    // The state carries the 68k's 32-bit cycle count, not this host's 64-bit clock, and BlastEm's serializer does not
    // touch cycle_base at all: the restored core's own count is an offset from wherever this instance's base already is
    // (the mirror note in the design's stepping study). So the anchor moves to the loaded core's absolute clock, the
    // stop grid restarts at the next sample rather than at one the pre-load timeline had already scheduled, and the
    // port device's queue goes with the clocks that no longer mean anything.
    sampleAnchor_ = sampleBase_;
    mclkAnchor_ = mclkNow();
    nextGridSample_ = sampleBase_;
    port_.clear();
    // The sink's ring, its blips and its filter are HOST state that no state carries: re-prime them, at the size each
    // one was sized for, or the restored core plays the tail of a machine that never existed.
    audio_.reset();
    return true;
}

std::size_t BlastemMdSystem::stateSnapshotSize() const {
    // A Mega Drive state is about 50 KB (the VDP's 64 KB of VRAM dominates) and grows with a cart's battery; a ceiling
    // with headroom is what lets the snapshot triple be allocated once, which is what SystemBase requires of it (its
    // own comment on the ceiling, and the reason a live Mesen state gets headroom too).
    return 256 * 1024;
}

bool BlastemMdSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    // Allocates on the block thread, which is Mesen's precedent (MesenSmsSystem captures a state the same way every
    // kStateSnapshotIntervalSec); the triple this writes into was pre-sized from the ceiling above, so the state's own
    // buffer is the only allocation in the publish.
    dst = saveStateBytes();
    return !dst.empty();
}

// The block triad, and the core's stop: one run a block, ended at the block's own master clock rather than at a frame
// boundary. The audio the run leaves behind is drained by the audio sink.
void BlastemMdSystem::prepareForBlock(const AudioBlockInfo& info) {
    blockFrames_ = info.frames;  // the stop grid's end is sampleBase_ + this
}

bool BlastemMdSystem::stepIfBelowTarget(std::uint32_t /*framesNeeded*/) {
    // One stop per host SAMPLE, not one per block. BlastEm charges refresh delays and interrupt-latency syncs at every
    // sync, so where the stops fall moves the emulation by a few cycles: a block's end as a stop point would make a
    // 1-frame render and a 4096-frame render of the same 2 s disagree. The grid is absolute time, so every partition
    // hits exactly the same stops, and run_until is already a no-op once the clock has passed, so a sample the core has
    // overrun costs nothing here. The resumes are not free (a block of stops costs about a fifth more than one stop a
    // block on the timing images), and they are what buys invariance. Always false: an unlinked system is the 1-member
    // case of the round-robin.
    const std::uint64_t end = sampleBase_ + blockFrames_;
    for (std::uint64_t s = nextGridSample_; s < end; ++s) {
        // The grid's sample is the port device's key: a level queued for sample s is on the pins from this run on, so
        // which block delivered it changes nothing.
        port_.setGridSample(s);
        run_until(masterClockOf(s + 1));
        nextGridSample_ = s + 1;
    }
    // And the levels the grid has passed are retired even when the ROM read nothing this block: a queue that only
    // drained on a read would fill with levels a non-polling ROM never took, and drop the newest instead.
    port_.retire();
    return false;
}

void BlastemMdSystem::finishBlock(const AudioBlockInfo& info, float* const* outs, std::size_t laneCount) {
    if (!gen_) {
        sampleBase_ += info.frames;  // not activated: nothing to drain
        return;
    }
    // The chips first, to the block's end clock: a run that a DMA carried past the stop returned without any sync at
    // the block's end (a stop is only checked at a sync), so the chips can be up to the whole transfer behind the 68k
    // and the drain would read samples they have not made. This is the same absolute clock the grid stops at, so it
    // moves no stop and no sample.
    syncSoundTo(masterClockOf(sampleBase_ + info.frames));
    // Drained EVERY time, lanes or none. A lane-less block still ends the sink's frames: the sink's ring is sized for a
    // block and its blips for one frame, so a `return` before the drain would leave the next block's audio queued behind
    // this one's (the rebase case and the video case both pass no lanes).
    // The mix is sized at activation and at every growth (reserveAudio), never here: the engine's [alloc] cases count a
    // block's allocations, emulation included. A block larger than it is a breach of the engine's contract, so it is cut
    // to the reservation and counted rather than read past the end of the mix.
    const std::uint32_t frames = std::min<std::uint32_t>(info.frames, static_cast<std::uint32_t>(mixLeft_.size()));
    if (frames < info.frames) ++clampedBlocks_;
    audio_.drain(frames, mixLeft_.data(), mixRight_.data());
    const bool wired = laneCount >= 2 && outs && outs[0] && outs[1];
    if (wired) {
        for (std::uint32_t i = 0; i < frames; ++i) {
            const float gain = gainSmoother_.next();
            outs[0][i] += mixLeft_[i] * gain;  // added, not written: SystemBase's contract
            outs[1][i] += mixRight_[i] * gain;
        }
    }
    sampleBase_ += info.frames;
}

void BlastemMdSystem::reserveAudio(std::uint32_t maxBlockFrames) {
    const std::uint32_t frames = std::max(maxBlockFrames, blockFrames(0));
    if (frames > mixLeft_.size()) {
        // The sink first: its sizing builds everything it needs and commits only at the end, so a throw leaves it
        // exactly as it was. The mix follows, and a throw there leaves a sink larger than the mix, which the drain's
        // clamp covers. Either way SystemBase's maximum does not move, so a reactivation at it does not retry the
        // allocation that failed.
        audio_.prepare(frames);
        mixLeft_.resize(frames);
        mixRight_.resize(frames);
    }
    SystemBase::reserveAudio(maxBlockFrames);  // LAST: the maximum moves only once the sizing has succeeded
}

void BlastemMdSystem::switchPsgClock(bool fast) {
    // The two lines genesis.c runs around vdp_test_port_write when the VDP test register's bit 0 changes its mind about
    // the Z80's clock source: the PSG's divider becomes MCLKS_PER_YM * 16 (112) or MCLKS_PER_PSG (240), and
    // psg_adjust_master_clock tells the sink. Writing the test register directly does NOT do this (the before/after
    // comparison is genesis.c's), which is why a case calls this instead. Out of line because it names both
    // psg_adjust_master_clock and gen_->psg, and this header includes no BlastEm header of its own.
    if (!gen_ || !gen_->psg) return;
    // The callback psg_adjust_master_clock makes belongs to THIS instance's sink. It routes by the source's own sink
    // (BlastemAudio.hpp), but the instance pointer is what a future callback reached from here would read, and a
    // switchPsgClock on an instance that is not the last one to run must not name that one.
    useInstance(this);
    gen_->psg->clock_inc = fast ? 112u : 240u;
    psg_adjust_master_clock(gen_->psg, gen_->master_clock);
}

// The chips to an absolute master clock, clamped to where the 68k actually is: a run can stop exactly on its target or
// past it (a 68k-to-VDP DMA moves the clock to the DMA's end), and the chips can never be asked for more than the
// machine has run. Both clocks are 32-bit offsets from cycle_base, the domain genesis_sync_sound takes.
void BlastemMdSystem::syncSoundTo(std::uint64_t mclk) {
    if (!gen_ || !gen_->psg) return;
    const std::uint64_t now = mclkNow();
    const std::uint64_t upto = std::min(mclk, now);
    if (upto <= gen_->cycle_base) return;
    const std::uint64_t offset = upto - gen_->cycle_base;
    if (offset > UINT32_MAX) return;  // unreachable: the rebase keeps the count inside the window
    if (gen_->psg->cycles >= offset) return;  // every sync so far ran both chips to the same clock
    useInstance(this);
    genesis_sync_sound(gen_, static_cast<std::uint32_t>(offset));
}

void BlastemMdSystem::run_until(std::uint64_t mclk) {
    if (!gen_ || mclk <= mclkNow()) return;
    useInstance(this);
    // The stop is a 32-bit OFFSET from gen->m68k->cycles (cycle_base + cycles is the absolute clock), computed in that
    // domain, never as `mclk - cycle_base` truncated to 32 bits: the difference is a 64-bit quantity, and it stays
    // exact only because the rebase keeps the count inside the window. Three terms cap the step: the clocks still
    // wanted, one quarter of the 32-bit space (2^30 clocks, about 20 s at 53.7 MHz, which keeps the arithmetic clear
    // of the top), and UINT32_MAX minus the current count. The third can be the smallest: between crossing
    // MAX_NO_ADJUST and the next top-level sync, which is where the rebase happens, the headroom is under one
    // 8192-frame block at 48 kHz, so it binds and adds a mid-block stop. A stop costs the clock nothing (the loop
    // recomputes the step from the clock it reads after the resume), and with the one-sample grid in stepIfBelowTarget
    // it is not a partition-dependent one either.
    constexpr std::uint64_t kWindow = 0x40000000u;
    while (mclkNow() < mclk) {
        const std::uint32_t cyc = gen_->m68k->cycles;
        const std::uint64_t step = std::min<std::uint64_t>({mclk - mclkNow(), kWindow, UINT32_MAX - cyc - 1});
        gen_->stop_cycle = cyc + static_cast<std::uint32_t>(step);
        // resume_context returns at stop_cycle, or at the frame end if that is sooner. The loop then recomputes the
        // step from the clock it just read, so a rebase inside a pass costs nothing: the rebase moves cycle_base,
        // cycles and stop_cycle together.
        gen_->header.resume_context(&gen_->header);
        gen_->stop_cycle = 0;
    }
}

// Port 1's pad: the button table and BlastEm's io_gamepad_down/up land with the pad's case, and until they do a press
// is dropped rather than remembered and applied late.
void BlastemMdSystem::pressButton(std::uint8_t /*button*/, bool /*down*/) {}

// Port 2's host device (BlastemPortDevice.hpp): every byte of the block's feed becomes one level held from the host
// sample of this block's `frame`-th sample on, so a level reaches the pins when the stop grid gets to its own sample
// rather than at the block boundary. The sample is absolute, so a level staged past this block's end is still on time
// for the block that reaches it: nothing rebases the schedule, and the host's block size chooses nothing.
void BlastemMdSystem::pushCoreBytes(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool /*flush*/) {
    if (!gen_ || !data || size == 0) return;
    const std::uint64_t at = sampleBase_ + frame;
    for (std::size_t i = 0; i < size; ++i) port_.enqueue(at, data[i]);
}

void BlastemMdSystem::writePortControl(std::uint8_t value) {
    if (!gen_) return;
    useInstance(this);  // as every call into the core: a frontend callback the handler makes belongs to this one
    io_control_write(&gen_->io.ports[1], value, gen_->m68k->cycles);
}

void BlastemMdSystem::writePortData(std::uint8_t value) {
    if (!gen_) return;
    useInstance(this);
    io_data_write(&gen_->io.ports[1], value, gen_->m68k->cycles);
}

}  // namespace rp::blastem

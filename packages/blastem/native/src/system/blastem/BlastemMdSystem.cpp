#include "system/blastem/BlastemMdSystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>

#include "system/blastem/BlastemCore.hpp"
#include "system/blastem/BlastemRom.hpp"
#include "system/blastem/InputTypes.hpp"

namespace rp::blastem {

// BlastEm's own serializer identifier. Its save_to_file writes these 8 bytes ahead of a state (serialize.c's sz_ident);
// a library build writes the state with genesis_serialize directly and never sees them, so this prefixes the same bytes
// and loadStateBytes demands them. Everything behind them is BlastEm's section stream, and anything else is a state this
// core cannot be: another serializer version, another console, another emulator.
static constexpr char kStateIdent[] = "BLSTSZ\x01\x07";

// This host's own section, appended after BlastEm's: the cartridge battery, in the .sav's order every other part of the
// host speaks (BlastemMemory.cpp's batteryStorageIndex is the pair swap a 16-bit cart needs). BlastEm's serializer has no
// SRAM section for a plain cart, only a banked cart's mapper registers, and the host's state is what a Duplicate copies:
// without this, duplicating a battery cart would duplicate the game and lose the save. The id is above BlastEm's own
// (serialize.h's SECTION_COLECO_IO is the last) so a BlastEm walk over the stream skips it as a section it does not read.
static constexpr std::uint16_t kSectionBattery = 0x8001;

// A state's sections are a u16 id, a u32 size and exactly that many bytes (start_section/end_section, serialize.c), so a
// truncated or mis-sized body is visible before the serializer is asked to read it. The walk is what makes "refused"
// mean refused rather than "partly loaded": the mirror's latch catches a short FIELD inside an otherwise well-formed
// section, this catches everything else without touching the core.
//
// It also holds the stream to the section set a Genesis state has, which is genesis_serialize's own order: the 68k (1),
// the Z80 (2), the VDP (3), the YM2612 (4), the PSG (5), the bus arbiter (6), the three IO ports (7, 8, 9), the main RAM
// (10) and the sound RAM (11), each exactly once; the mapper's section (12) exactly when this cart is banked, because
// its absence from a banked cart leaves the mapper at power-on; and the model's TMSS section (15) at most once. A
// repeated section, a missing one, or an id this build does not read is a state from another console, another
// serializer or another cart, and a console with a section of itself missing is only half a machine.
//
// This host's own battery section is allowed as well, at most once and last (a state this host writes puts it there, and
// the stream BlastEm reads ends where it starts).
struct StateWalk {
    bool                whole        = false;
    std::size_t         blastemBytes = 0;  // where BlastEm's sections end: this host's own start there, if any
    const std::uint8_t* battery      = nullptr;
    std::size_t         batteryBytes = 0;
};

static StateWalk walkSections(const std::uint8_t* bytes, std::size_t size, bool banked) {
    constexpr std::uint16_t kFirstMandatory = 1;   // SECTION_68000
    constexpr std::uint16_t kLastMandatory  = 11;  // SECTION_SOUND_RAM
    constexpr std::uint16_t kMapper         = 12;  // SECTION_MAPPER
    constexpr std::uint16_t kTmss           = 15;  // SECTION_TMSS
    bool                    seen[kTmss + 1] = {};
    std::size_t             at              = 0;
    StateWalk               walk;
    while (at < size) {
        if (size - at < 6) return walk;
        const std::uint16_t id  = std::uint16_t(bytes[at] << 8 | bytes[at + 1]);
        const std::uint32_t len = (std::uint32_t(bytes[at + 2]) << 24) | (std::uint32_t(bytes[at + 3]) << 16) |
                                  (std::uint32_t(bytes[at + 4]) << 8) | std::uint32_t(bytes[at + 5]);
        const std::size_t payload = at + 6;
        at = payload;
        if (len > size - at) return walk;
        if (id == kSectionBattery) {
            if (walk.battery || at + len != size) return walk;  // once, and last
            walk.battery      = bytes + at;
            walk.batteryBytes = len;
            walk.blastemBytes = payload - 6;
        } else {
            const bool mandatory = id >= kFirstMandatory && id <= kLastMandatory;
            if (!mandatory && id != kMapper && id != kTmss) return walk;
            if (seen[id]) return walk;
            seen[id] = true;
        }
        at += len;
    }
    if (at != size) return walk;
    for (std::uint16_t id = kFirstMandatory; id <= kLastMandatory; ++id) {
        if (!seen[id]) return walk;
    }
    if (seen[kMapper] != banked) return walk;
    if (!walk.battery) walk.blastemBytes = size;  // no host section: BlastEm reads the whole body
    walk.whole = true;
    return walk;
}

// The clocks a state may have between its components and the 68k. Every component's cycle count is an offset from the
// same cycle_base and the machine runs one clock: a sync runs each of them to the 68k's count. The one gap a machine
// this core writes can have is a 68k-to-VDP DMA, which moves the 68k's count to the transfer's end while the sound chips
// wait for the block-end sync, and the longest transfer (65,536 words, display on, H32) costs 11,980,603 master clocks,
// about a fifth of a second (kDmaOvershootSeconds in BlastemAudio.cpp sizes the sink's ring from the same figure). A
// quarter second of master clocks covers that with room for a frame's worth of sync, and it is what a state has to stay
// within: past it the components would have to run forward by seconds to catch up, and they do it inside the next block
// on the block thread (the review's reproducer moved only the 68k's count to 0xF0000000 and the first blocks took 1.8 s
// instead of 5 ms).
static constexpr double kStateClockSkewSeconds = 0.25;

static bool clocksAgree(const genesis_context* gen) {
    const std::uint64_t allowed   = static_cast<std::uint64_t>(double(gen->master_clock) * kStateClockSkewSeconds);
    const std::uint64_t m68k      = gen->m68k->cycles;
    const std::uint64_t counts[4] = {gen->vdp->cycles, gen->z80->cycles, gen->ym->current_cycle, gen->psg->cycles};
    for (std::uint64_t count : counts) {
        const std::uint64_t skew = count > m68k ? count - m68k : m68k - count;
        if (skew > allowed) return false;
    }
    return true;
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

BlastemMdSystem::~BlastemMdSystem() {
    destroyCore();
    // The capture's serializer buffer outlives its core by design (the core is rebuilt on a refused state), so it is
    // freed here and not in destroyCore.
    free(stateSerialize_.data);
}

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
    // whatever romdb.c chose, which is the pad the wire bytes drive. The port carries this instance as its host, so a
    // read answers for the console it came from rather than for whichever instance last ran on this thread.
    gen_->io.ports[1].device_type = IO_CUSTOM;
    gen_->io.ports[1].device.custom.host = this;
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
        // their own scopes, never nested). The retry sees an empty savestate, so it cannot recurse again: the clear at
        // the end of this block is what it keys on, and the retry is the last line here.
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
    // Clears THIS thread's pointer, which is the one the free above could have set, and not inside the branch: a boot
    // that failed set it too. Another thread that ran this instance keeps its own stale value, which is harmless only
    // because every entry into the core sets it first (BlastemCore.hpp's useInstance); this is the one place that
    // clears it, so it is the covering of the destroying thread's slot, not a dangling-pointer sweep.
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

// The whole state container into `out`, which is resized (so a reused vector keeps its capacity): BlastEm's sections
// through `buf`'s storage, which the caller may keep between captures, and then this host's own battery section.
// `battery` is the scratch the battery is copied into on the way.
bool BlastemMdSystem::captureStateInto(serialize_buffer& buf, std::vector<std::uint8_t>& battery,
                                       std::vector<std::uint8_t>& out) const {
    if (!gen_) {
        out.clear();
        return false;
    }
    useInstance(const_cast<BlastemMdSystem*>(this));  // the serializer's own callbacks are frontend calls
    // A reused buffer starts at its beginning again; its storage is what it keeps.
    buf.size                  = 0;
    buf.failed                = 0;
    buf.current_section_start = 0;
    // The pc argument is the old core's resume address, and under NEW_CORE m68k_serialize ignores it: it writes
    // context->pc - 2, which is the instruction that will execute when the state is resumed, and m68k_deserialize adjusts
    // it back. What matters here is that a state is taken between blocks, so the pc is an instruction boundary and the
    // restore resumes on the next instruction (genesis_deserialize's own adjust_int_cycle follows the loaded pc).
    genesis_serialize(gen_, &buf, gen_->m68k->pc, 1);
    if (hasBattery(gen_)) {
        // The .sav's order, which is what a load reads back: this is the same copy saveSramBytes() returns, in the
        // caller's buffer so a snapshot does not allocate one.
        battery.resize(gen_->save_size);
        for (std::size_t i = 0; i < battery.size(); ++i) battery[i] = gen_->save_storage[batteryStorageIndex(gen_, i)];
        start_section(&buf, kSectionBattery);
        save_buffer8(&buf, battery.data(), battery.size());
        end_section(&buf);
    }
    if (buf.failed) {  // the capture could not grow: no state is better than half a state
        out.clear();
        return false;
    }
    out.resize(sizeof(kStateIdent) - 1 + buf.size);
    std::memcpy(out.data(), kStateIdent, sizeof(kStateIdent) - 1);
    std::memcpy(out.data() + sizeof(kStateIdent) - 1, buf.data, buf.size);
    return true;
}

std::vector<std::uint8_t> BlastemMdSystem::saveStateBytes() const {
    // The control plane's copy: a buffer of its own, because a capture on the block thread owns the member one. The
    // state is the same bytes either way.
    serialize_buffer          buf;
    std::vector<std::uint8_t> battery;
    std::vector<std::uint8_t> out;
    init_serialize(&buf);
    const bool ok = captureStateInto(buf, battery, out);
    free(buf.data);
    return ok ? std::move(out) : std::vector<std::uint8_t>{};
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
    const StateWalk walk =
        walkSections(bytes.data() + kIdent, bytes.size() - kIdent, gen_->mapper_type != MAPPER_NONE);
    if (!walk.whole) {
        stateRefused_ = true;  // truncated, mis-sized or not this console's section set: nothing has been touched
        return false;
    }
    deserialize_buffer buf;
    // BlastEm reads its own sections and stops where this host's begin: the battery is not a console register, and a walk
    // that handed it over would only log a section it has no handler for.
    init_deserialize(&buf, const_cast<std::uint8_t*>(bytes.data()) + kIdent, walk.blastemBytes);
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
    if (!clocksAgree(gen_)) {
        // Every section loaded, so this is the same rebuild path a short field takes: boot()'s tail drops the seed and
        // starts the machine from power-on.
        stateRefused_ = true;
        return false;
    }
    if (walk.battery) {
        // The battery section, in the .sav's order, into BlastEm's storage. Its size is the cart's own, so a state with a
        // different one is another cart's; a battery cart's state from a host that did not write this section keeps the
        // build's seed battery instead of being refused.
        if (!hasBattery(gen_) || walk.batteryBytes != std::size_t(gen_->save_size)) {
            stateRefused_ = true;
            return false;
        }
        for (std::size_t i = 0; i < walk.batteryBytes; ++i) {
            gen_->save_storage[batteryStorageIndex(gen_, i)] = walk.battery[i];
        }
    }
    // The state carries the 68k's 32-bit cycle count, not this host's 64-bit clock, and BlastEm's serializer does not
    // touch cycle_base at all: the restored core's own count is an offset from wherever this instance's base already is
    // (the mirror note in the design's stepping study). So the anchor moves to the loaded core's absolute clock, the
    // stop grid restarts at the next sample rather than at one the pre-load timeline had already scheduled, and the
    // port device's queue goes with the clocks that no longer mean anything.
    //
    // What the state does NOT carry is the grid's own phase: the overshoot the pre-load run had passed its last stop by,
    // and the offset within a sample the anchors hold. A restored instance therefore stops at its own timeline's
    // boundaries rather than the source's, and a stop moves the emulation by the cycles it lands on, so a Duplicate is
    // not sample-exact against its source after the load. That is accepted rather than fixed: two instances restored
    // from the same bytes do match each other exactly (the round-trip cases), block-size invariance survives a load,
    // and audio could not match regardless (the sink's ring, blips and filter are host state, re-primed below). The
    // promise a Duplicate keeps is the machine's state, not the source's future sample for sample.
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
    // Measured at 141,639 bytes with the identifier (the VDP's 66,865, the work RAM's 65,537 and the sound RAM's 8,193
    // dominate), so the 256 KiB ceiling is about 1.8x headroom rather than the five a "50 KB" guess suggested. The
    // cart's battery sits on top of that, in this host's own section, and it is the one part of the ceiling that varies
    // by cart, so it is added here: the triple is allocated once from this figure and never resized (SystemBase's own
    // comment on the ceiling).
    return 256 * 1024 + (gen_ ? gen_->save_size : 0);
}

bool BlastemMdSystem::captureStateSnapshot(std::vector<std::uint8_t>& dst) {
    // Runs on the block thread, so the buffers are the members: BlastEm's serializer buffer (allocated at 256 KiB by
    // the first capture and grown if a battery needs it), the battery's scratch and `dst` itself, which SystemBase
    // hands back every publish and whose capacity is kept by the resize. The first capture allocates those three and
    // every later one allocates nothing; Mesen's capture allocates twice every time and this does not.
    if (!stateSerialize_.data) init_serialize(&stateSerialize_);
    return captureStateInto(stateSerialize_, batteryScratch_, dst) && !dst.empty();
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

// Port 1's pad (design section 5). BlastEm's gamepad state is a LEVEL that io_data_read samples live, so a press
// applied before a block is what that block's polls see: no queue, and nothing for prepareForBlock to apply. That is
// the same contract Mesen's apply gives, because the engine applies a block's presses before its core runs.
//
// The three tables below are one fact restated twice, so they live together: kBlastemButton turns the wire byte into one
// of BlastEm's button ids, and kPadState/kPadBit turn the same byte into the bit io_port_gamepad_down/up set with it.
// All three restate io.c's `static` button_defs table (io.c:78-91), which no header exposes, and MdButton::Count is the
// length all three must keep (InputTypes.hpp).
static constexpr std::uint8_t kBlastemButton[] = {
    DPAD_RIGHT, DPAD_LEFT,  DPAD_UP,   DPAD_DOWN,
    BUTTON_A,   BUTTON_B,   BUTTON_C,  BUTTON_START,
    BUTTON_X,   BUTTON_Y,   BUTTON_Z,  BUTTON_MODE,
};

// io.c's three input registers (io.c:48-50: GAMEPAD_TH0, GAMEPAD_TH1, GAMEPAD_EXTRA, `#define`s that never reached a
// header), and the register and bit each button's press sets in them (io_port_gamepad_down ORs the bit in, io.c:150-157).
enum : std::uint8_t { kPadTh0 = 0, kPadTh1 = 1, kPadExtra = 2 };

static constexpr std::uint8_t kPadState[] = {kPadTh1, kPadTh1, kPadTh0, kPadTh0,
                                            kPadTh0, kPadTh1, kPadTh1, kPadTh0,
                                            kPadExtra, kPadExtra, kPadExtra, kPadExtra};
static constexpr std::uint8_t kPadBit[] = {0x08, 0x04, 0x01, 0x02,
                                          0x10, 0x10, 0x20, 0x20,
                                          0x04, 0x02, 0x01, 0x08};

static_assert(std::size(kBlastemButton) == static_cast<std::size_t>(MdButton::Count));
static_assert(std::size(kPadState) == static_cast<std::size_t>(MdButton::Count));
static_assert(std::size(kPadBit) == static_cast<std::size_t>(MdButton::Count));

void BlastemMdSystem::pressButton(std::uint8_t button, bool down) {
    if (!gen_) return;
    if (button >= std::size(kBlastemButton)) return;  // a byte that names no button of this console drives none
    useInstance(this);  // as every call into the core: a callback the device makes belongs to this one
    // The pad is gamepad number 1, not 0: setup_io_devices connects "gamepad6.1" to port 1 and "gamepad6.2" to port 2
    // (io.c:387-388), find_gamepad matches on the digit in that name (io.c:93-115, io.c:286), and number 0 matches the
    // EXT port instead (its gamepad_num is zeroed with the port), so a press on 0 would go to the wrong port.
    if (down) {
        io_gamepad_down(&gen_->io, 1, kBlastemButton[button]);
    } else {
        io_gamepad_up(&gen_->io, 1, kBlastemButton[button]);
    }
}

// The test seam beside pressButton: the bit that button's press set, read straight out of port 1's pad registers. Set
// means held, because the pad's output is logically inverted on the wire (io_data_read's IO_GAMEPAD6 case inverts the
// register it hands the bus). False before a core exists, for a byte that names no button, and when port 1 holds no pad.
bool BlastemMdSystem::padDown(std::uint8_t button) const {
    if (!gen_ || button >= std::size(kPadState)) return false;
    // Port 1's device is the pad setup_io_devices connected there; boot() replaced only port 2's.
    io_port* port = find_gamepad(&gen_->io, 1);
    if (!port) return false;
    return (port->input[kPadState[button]] & kPadBit[button]) != 0;
}

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

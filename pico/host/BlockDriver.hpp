#pragma once

// The block driver (docs/modular-graph-phase-e3-design.md section 5): the seam between what sets a box's cadence and
// the plan it runs. One call, next(): whether a block is due, how many frames it covers and the transport it runs
// under. E3's driver (ElapsedDriver) answers the time since the previous block; an audio driver later answers its fixed
// block when its buffer is ready, and neither the loader nor a node changes with it.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does.

#include <cstdint>

#include "graph/rp_node.h"

namespace rp::box {

class BlockDriver {
public:
    // The next block: true with its length in *frames (at least 1, at most the plan's maxFrames) and *transport
    // filled, or false when none is due.
    virtual bool next(std::uint32_t* frames, rp_transport* transport) = 0;

protected:
    ~BlockDriver() = default;
};

// E3's driver: the time since the previous block at the plan's sample rate, in whole frames, carrying what passes
// maxFrames into the next block. The frames are counted from the start rather than block by block, so the frames run
// add up to the time passed, its fraction of a frame included: no frame is lost or run twice while the carry is
// within kCarryBlocks blocks (21.3 ms at 48 kHz and 256 frames). That carries an 11 ms console line or a 5 ms FIFO
// write whole. A longer hold (an N8 call of 20 ms or more, a boot through the N8's menu, a 12 s install: 576,000
// frames), or passes that keep outlasting a block (one block runs a pass, so a stream whose passes each spend more
// than a block's time in FIFO writes falls behind until it reaches the bound), drops the frames past the bound,
// counted (skipped(), the box's [stats] `skipped=`), rather than running them: run, a long wait would be thousands of
// empty blocks, and under a stream the catch-up would last minutes with every message stamped at its block's last
// frame. Bounded, the driver is at most kCarryBlocks blocks behind the clock, a message lands within 21 ms of its
// frame, and the passes after a wait do no extra work. Nothing in the box's bridge plan reads time; a plan that does
// (a MIDI clock, say) would lose the dropped time under load, which is the audio driver's to settle: a fixed cadence
// has no carry.
//
// A message waits for no block of its own: when a pass comes less than a frame's time (20.8 us) after the last block,
// next() has no block, and what was staged goes to the next pass's block, a pass later.
class ElapsedDriver final : public BlockDriver {
public:
    // Microseconds, monotonic, from any origin.
    using Clock = std::uint64_t (*)(void* ctx);

    // The most blocks' worth of frames a next() finds due and runs (one now, the rest in the passes after); time past
    // it is dropped.
    static constexpr std::uint32_t kCarryBlocks = 4;

    // Starts the timeline at the clock's now: the first block covers the time from here. `maxFrames` is the plan's,
    // which a block never passes (runBlock does not check it).
    void start(Clock clock, void* ctx, std::uint32_t sampleRate, std::uint32_t maxFrames);
    bool next(std::uint32_t* frames, rp_transport* transport) override;

    // The frame a moment (the low 32 bits of the clock's microseconds, as a UART timestamp holds them) falls on within
    // the block the last next() answered: its offset from the block's first frame, clamped into the block. A moment
    // before the block (a message that waited a pass, or one in time the bound dropped) is its first frame and one
    // after it (the block runs behind the clock, with a carry) its last, so a message is never held for a later block.
    std::uint32_t frameOf(std::uint32_t us) const;

    // The frames due and not yet run, as of the last next().
    std::uint64_t carried() const { return due_ - done_; }
    // Blocks run since the start.
    std::uint64_t blocks() const { return blocks_; }
    // Frames dropped for falling past the carry's bound.
    std::uint64_t skipped() const { return skipped_; }

private:
    std::uint64_t frameAt(std::uint64_t us) const;

    Clock         clock_ = nullptr;
    void*         ctx_ = nullptr;
    std::uint32_t rate_ = 0;
    std::uint32_t maxFrames_ = 0;
    std::uint64_t origin_ = 0;   // the clock at the start
    std::uint64_t now_ = 0;      // the clock at the last next()
    std::uint64_t due_ = 0;      // frames from the start to now_
    std::uint64_t done_ = 0;     // frames run
    std::uint64_t first_ = 0;    // the first frame of the last block answered
    std::uint32_t frames_ = 0;   // its length
    std::uint64_t blocks_ = 0;
    std::uint64_t skipped_ = 0;
};

}  // namespace rp::box

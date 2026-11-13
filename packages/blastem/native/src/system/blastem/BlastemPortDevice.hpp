#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "system/InputCapacity.hpp"
#include "util/FixedQueue.hpp"

namespace rp::blastem {

// One port read as the device saw it (a case's own record, through PortDevice::observe): the stop grid's sample the read
// ran under, its absolute master clock, and the level the device answered with.
struct PortAccess {
    std::uint64_t sample;
    std::uint64_t mclk;
    std::uint8_t  answer;
};

// Controller port 2 as the host device (design section 3). A queue of (host sample, level byte) entries; an access
// answers with the last entry the stop grid has reached. The level byte's bits are the pins: bit 5 (TR) carries
// genmddj's counter bit 0, bit 6 (TH) its bit 1, and the rest idle high, which is what a disconnected pad leaves.
// `pushCoreBytes` is the only door in, so an idle port is a port with an empty queue.
//
// A LEVEL, not a byte stream: smsggdj/genmddj hold a 2-bit counter on two lines and the ROM samples it once per frame,
// so an entry means "drive the pins with this value from this sample on, and hold it", and several entries the grid
// passes between two of the ROM's polls collapse to the last one. That is the protocol's own tolerance (it counts
// `(current - last) & 3`), not a shortcut.
//
// The key is a HOST SAMPLE, not a clock, because the stop grid is one stop per host sample: a level becomes visible
// exactly when the grid reaches its sample, whichever block carried it. Gating on an access's own clock instead makes
// the answer depend on the partition: every stop lands at the first instruction boundary at or past its target (up to
// one instruction of overshoot), so in a large block the levels for the next sample are already queued when the 68k
// crosses the target, while in a one-sample block the host pushes them only after that run has already overshot it.
class PortDevice {
public:
    // Sized to what one block's doors can deliver, as SmsSyncRole's is: the bytes input's 1 KiB, or a message the
    // midi input carries (72 KiB) through pushCoreBytes. A level is one byte.
    static constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;

    // [control/block] Queue one level at the host sample it goes on at. A later call can carry an earlier sample (the
    // two doors of one block are not ordered against each other), so the queue sorts before it drains.
    void enqueue(std::uint64_t sample, std::uint8_t level);
    void clear() {
        pending_.clear();
        needsSort_ = false;
        lastApplied_ = 0x60;  // a fresh session starts with the pins released, not holding the last session's level
    }
    // [control/block] The stop grid's sample the next run starts at: a level is on the pins from that run on, whichever
    // block delivered it. stepIfBelowTarget sets it before every run_until.
    void setGridSample(std::uint64_t sample) { grid_ = sample; }
    // [device] Collapses every entry the grid has already reached into the held level, without an access to do it, so
    // the queue is bounded by one block's deliveries whether or not the ROM polls. stepIfBelowTarget calls it at the
    // end of its grid.
    void retire();
    // [device] Applies every entry the grid has reached and returns the LEVEL BYTE in force: bits 5 and 6 are the
    // host's TR and TH, and nothing else is the host's to decide (io_data_read's tail mixes in the ROM's latch and
    // pulls the rest high).
    std::uint8_t level(std::uint64_t mclk);
    // [device, test] Record every read into `log` while it has room, or stop recording when the span is empty. A full
    // log stops recording rather than allocating on the block thread, and the storage is the case's: the device only
    // writes into it.
    void observe(std::span<PortAccess> log) {
        log_ = log;
        recorded_ = 0;
    }
    std::size_t recorded() const { return recorded_; }
    // [device] Whether anything is queued (a test's read of whether a block drained its schedule).
    bool empty() const { return pending_.empty(); }

    // Levels the queue took since construction: clear() empties the queue and releases the pins, and keeps this count
    // (as it keeps dropped()).
    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t dropped() const { return pending_.dropped(); }
    std::uint8_t  lastApplied() const { return lastApplied_; }

private:
    // One queued level in one word: the host sample above bit 7, the level byte below it. 56 bits of sample is about
    // 24,000 years at 48 kHz, and the packing halves the queue and its sort scratch against an unpacked
    // {std::uint64_t, std::uint8_t}, which every instance holds for its life.
    struct Entry {
        std::uint64_t word;
        std::uint64_t sample() const { return word >> 8; }
        std::uint8_t  level() const { return std::uint8_t(word & 0xFF); }
    };
    static Entry make(std::uint64_t sample, std::uint8_t level) {
        return Entry{(sample << 8) | std::uint64_t(level)};
    }

    // Sorts the queue once before a drain, as SmsSyncRole's pumpUntil does. Stable, so entries sharing a sample keep
    // their enqueue order and the last one queued is the one on the pins.
    void sortPending();

    rp::FixedQueue<Entry>  pending_{kCapacity};
    rp::SortScratch<Entry> sortScratch_{kCapacity};
    bool                   needsSort_ = false;
    std::uint64_t          grid_ = 0;
    std::span<PortAccess>  log_{};
    std::size_t            recorded_ = 0;
    std::uint64_t          accepted_ = 0;
    std::uint8_t           lastApplied_ = 0x60;  // idle: TR and TH released (a pad's pull-ups)
};

}  // namespace rp::blastem

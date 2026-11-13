#pragma once

#include <cstddef>
#include <cstdint>

#include "system/InputCapacity.hpp"
#include "util/FixedQueue.hpp"

namespace rp::blastem {

// Controller port 2 as the host device (design section 3). A queue of (master clock, level byte) entries; each access
// applies everything due at or before its own clock and then answers. The level byte's bits are the pins: bit 5 (TR)
// carries genmddj's counter bit 0, bit 6 (TH) its bit 1, and the rest idle high, which is what a disconnected pad
// leaves. `pushCoreBytes` is the only door in, so an idle port is a port with an empty queue.
//
// A LEVEL, not a byte stream: smsggdj/genmddj hold a 2-bit counter on two lines and the ROM samples it once per frame,
// so an entry means "drive the pins with this value from this clock on, and hold it", and several entries coming due
// between two of the ROM's polls collapse to the last one. That is the protocol's own tolerance (it counts
// `(current - last) & 3`), not a shortcut.
class PortDevice {
public:
    // Sized to what one block's doors can deliver, as SmsSyncRole's is: the bytes input's 1 KiB, or a message the
    // midi input carries (72 KiB) through pushCoreBytes. A level is one byte.
    static constexpr std::size_t kCapacity = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;

    // [control/block] Queue one level at the absolute master clock it goes on at. A later call can carry an earlier
    // clock (the two doors of one block are not ordered against each other), so the queue sorts before it drains.
    void enqueue(std::uint64_t mclk, std::uint8_t level);
    void clear() {
        pending_.clear();
        needsSort_ = false;
    }
    // [device] Applies every entry due at `mclk` and returns the LEVEL BYTE in force: bits 5 and 6 are the host's TR
    // and TH, and nothing else is the host's to decide (io_data_read's tail mixes in the ROM's latch and pulls the
    // rest high).
    std::uint8_t level(std::uint64_t mclk);
    // [device] Whether anything is queued (a test's read of whether a block drained its schedule).
    bool empty() const { return pending_.empty(); }

    std::uint64_t accepted() const { return accepted_; }
    std::uint64_t dropped() const { return pending_.dropped(); }
    std::uint8_t  lastApplied() const { return lastApplied_; }

private:
    struct Entry {
        std::uint64_t mclk;
        std::uint8_t  level;
    };

    rp::FixedQueue<Entry>  pending_{kCapacity};
    rp::SortScratch<Entry> sortScratch_{kCapacity};
    bool                   needsSort_ = false;
    std::uint64_t          accepted_ = 0;
    std::uint8_t           lastApplied_ = 0x60;  // idle: TR and TH released (a pad's pull-ups)
};

}  // namespace rp::blastem

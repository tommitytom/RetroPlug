// The fixed storage a core's ingress queues are built from (docs/modular-graph-phase-c4-design.md section 11):
// FixedQueue's refusal and its compaction when the storage is full but a prefix was consumed, FixedByteRing's wrap,
// and SortScratch's stability and its bound. These are the branches a block relies on and a core's cases reach only
// indirectly.

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "CountingAllocator.hpp"
#include "system/InputCapacity.hpp"
#include "util/FixedQueue.hpp"

TEST_CASE("FixedQueue pushes, pops, refuses at its bound and counts the refusals", "[graph][fixed-queue]") {
    rp::FixedQueue<int> queue(3);
    CHECK(queue.capacity() == 3);
    CHECK(queue.empty());
    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK(queue.push(3));
    CHECK(queue.size() == 3);
    CHECK(queue.front() == 1);
    CHECK(queue.back() == 3);
    CHECK_FALSE(queue.push(4));
    CHECK(queue.size() == 3);
    CHECK(queue.dropped() == 1);
    CHECK(queue.front() == 1);

    queue.pop();
    CHECK(queue.size() == 2);
    CHECK(queue.front() == 2);
    queue.pop();
    queue.pop();
    CHECK(queue.empty());
    CHECK(queue.dropped() == 1);

    // The counter is cumulative, not reset by a drain.
    CHECK(queue.push(5));
    queue.clear();
    CHECK(queue.empty());
    CHECK(queue.dropped() == 1);
}

TEST_CASE("FixedQueue compacts a consumed prefix before a push, keeping the live order", "[graph][fixed-queue]") {
    rp::FixedQueue<int> queue(3);
    queue.push(10);
    queue.push(20);
    queue.push(30);
    CHECK_FALSE(queue.push(40));  // full with nothing consumed: refused
    queue.pop();                   // 10 goes; the buffer still holds [10, 20, 30] with head at 1

    // The push needs the room the consumed prefix occupies, so the queue erases it first. The order of what is live
    // must be unchanged, and the new entry lands behind it.
    CHECK(queue.push(40));
    CHECK(queue.dropped() == 1);
    CHECK(queue.size() == 3);
    CHECK(queue.front() == 20);
    CHECK(queue[0] == 20);
    CHECK(queue[1] == 30);
    CHECK(queue[2] == 40);

    // begin()/end() are the live range, head included, after the compaction as before it.
    std::vector<int> live(queue.begin(), queue.end());
    CHECK(live == std::vector<int>{20, 30, 40});
}

TEST_CASE("FixedQueue pop on an empty queue is a no-op, and the queue still takes an entry", "[graph][fixed-queue]") {
    rp::FixedQueue<int> queue(2);
    queue.pop();  // moving the head past the end would make size() wrap round
    CHECK(queue.empty());
    CHECK(queue.size() == 0);
    CHECK(queue.push(1));
    CHECK(queue.size() == 1);
    CHECK(queue.front() == 1);
    queue.pop();
    CHECK(queue.empty());
}

TEST_CASE("FixedQueue is not copyable, so a copy cannot silently lose its reservation", "[graph][fixed-queue]") {
    // vector's copy allocates size(), not capacity(), so a copied queue would reallocate on its next push.
    STATIC_REQUIRE(!std::is_copy_constructible_v<rp::FixedQueue<int>>);
    STATIC_REQUIRE(!std::is_copy_assignable_v<rp::FixedQueue<int>>);
    STATIC_REQUIRE(std::is_move_constructible_v<rp::FixedQueue<int>>);
}

TEST_CASE("FixedByteRing wraps and delivers in arrival order over more than its capacity", "[graph][fixed-byte-ring]") {
    rp::FixedByteRing ring(4);
    CHECK(ring.capacity() == 4);
    CHECK(ring.empty());
    for (std::uint8_t b = 1; b <= 4; ++b) CHECK(ring.push(b));
    CHECK_FALSE(ring.push(5));
    CHECK(ring.dropped() == 1);
    CHECK(ring.size() == 4);
    CHECK(ring.front() == 1);

    ring.pop();
    ring.pop();                    // two slots are free at the front; the next pushes wrap past the end
    CHECK(ring.push(5));
    CHECK(ring.push(6));
    CHECK(ring.dropped() == 1);
    CHECK(ring.size() == 4);
    CHECK(ring.front() == 3);

    std::vector<std::uint8_t> got;
    while (!ring.empty()) {
        got.push_back(ring.front());
        ring.pop();
    }
    CHECK(got == std::vector<std::uint8_t>{3, 4, 5, 6});
    CHECK(ring.dropped() == 1);
}

TEST_CASE("FixedByteRing reserve re-lays a wrapped queue out, keeping its bytes in order", "[graph][fixed-byte-ring]") {
    rp::FixedByteRing ring(4);
    for (std::uint8_t b = 1; b <= 4; ++b) CHECK(ring.push(b));
    ring.pop();
    ring.pop();
    CHECK(ring.push(5));
    CHECK(ring.push(6));  // head is 2 and the queue has wrapped: 3 and 4 sit after the new bytes
    ring.reserve(8);
    CHECK(ring.capacity() == 8);
    CHECK(ring.size() == 4);
    std::vector<std::uint8_t> got;
    while (!ring.empty()) {
        got.push_back(ring.front());
        ring.pop();
    }
    CHECK(got == std::vector<std::uint8_t>{3, 4, 5, 6});
}

namespace {

// An entry that counts the moves and comparisons a sort makes of it, so a case can bound the sort's work rather than
// its wall clock: the same element and key a role's queue holds, one counter the Less functor reads.
struct CountedEntry {
    std::uint32_t key = 0;
    std::uint32_t seq = 0;

    static std::size_t moves;
    static std::size_t comparisons;

    CountedEntry() = default;
    CountedEntry(std::uint32_t k, std::uint32_t s) : key(k), seq(s) {}
    CountedEntry(const CountedEntry& other) : key(other.key), seq(other.seq) { ++moves; }
    CountedEntry(CountedEntry&& other) noexcept : key(other.key), seq(other.seq) { ++moves; }
    CountedEntry& operator=(const CountedEntry& other) {
        ++moves;
        key = other.key;
        seq = other.seq;
        return *this;
    }
    CountedEntry& operator=(CountedEntry&& other) noexcept {
        ++moves;
        key = other.key;
        seq = other.seq;
        return *this;
    }
};

std::size_t CountedEntry::moves = 0;
std::size_t CountedEntry::comparisons = 0;

}  // namespace

TEST_CASE("SortScratch keeps equal keys in their original order", "[graph][fixed-queue]") {
    struct Entry {
        int key;
        int seq;
    };
    std::vector<Entry> entries{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    rp::SortScratch<Entry> scratch(entries.size());
    scratch.sort(entries.data(), entries.data() + entries.size(),
                 [](const Entry& a, const Entry& b) { return a.key < b.key; });

    std::vector<int> keys;
    std::vector<int> seqs;
    for (const auto& e : entries) {
        keys.push_back(e.key);
        seqs.push_back(e.seq);
    }
    CHECK(keys == std::vector<int>{1, 1, 2, 2, 2});
    CHECK(seqs == std::vector<int>{1, 3, 0, 2, 4});  // equal keys did not swap: the sort is stable

    // Trivial shapes, and a range shorter than the scratch's capacity, which is how a role calls it.
    std::vector<Entry> one{{7, 7}};
    scratch.sort(one.data(), one.data(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    scratch.sort(one.data(), one.data() + 1, [](const Entry& a, const Entry& b) { return a.key < b.key; });
    CHECK(one.front().seq == 7);
}

TEST_CASE("SortScratch bounds a full reversed queue to a merge's moves, and allocates nothing",
          "[graph][fixed-queue]") {
    // The worst shape a block can hand the sort: a Master System's level queue full (the midi door's longest message
    // and the bytes door's block, which is what SmsSyncRole sizes it to) and in decreasing offset order, which is a
    // producer allowed to stamp offsets backwards. The insertion sort this replaced moved n * n / 2 entries there,
    // about 2.8 G on the audio thread; a bottom-up merge moves each entry once a pass, about 1.3 M. The bound is the
    // sort's own work, counted by the entries, not a wall clock.
    constexpr std::size_t kCount = std::size_t(rp::kMidiInput.bytes) + rp::kBytesInput.bytes;
    std::size_t passes = 0;
    for (std::size_t width = 1; width < kCount; width *= 2) ++passes;

    std::vector<CountedEntry> entries;
    entries.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) entries.emplace_back(static_cast<std::uint32_t>(kCount - i), 0);
    rp::SortScratch<CountedEntry> scratch(kCount);  // sized with a role's queue, so the sort allocates nothing
    const auto less = [](const CountedEntry& a, const CountedEntry& b) {
        ++CountedEntry::comparisons;
        return a.key < b.key;
    };

    CountedEntry::moves = 0;
    CountedEntry::comparisons = 0;
    rp::test::beginCountingAllocations();
    scratch.sort(entries.data(), entries.data() + entries.size(), less);
    const auto counted = rp::test::endCountingAllocations();
    INFO("sort: " << counted.allocations << " allocations, " << counted.frees << " frees, "
                  << CountedEntry::moves << " moves, " << CountedEntry::comparisons << " comparisons");
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    // Each pass moves every entry once, and an odd pass count moves the sorted run back out of the scratch.
    CHECK(CountedEntry::moves <= kCount * (passes + 1));

    bool ordered = true;
    for (std::size_t i = 1; i < entries.size(); ++i) ordered = ordered && entries[i - 1].key <= entries[i].key;
    CHECK(ordered);
    CHECK(entries.front().key == 1);
    CHECK(entries.back().key == kCount);
}

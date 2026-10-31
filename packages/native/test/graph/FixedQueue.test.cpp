// The fixed storage a core's ingress queues are built from (docs/modular-graph-phase-c4-design.md section 4):
// FixedQueue's refusal and its compaction when the storage is full but a prefix was consumed, FixedByteRing's wrap,
// and stableSortEntries's stability. These are the branches a block relies on and a core's cases reach only indirectly.

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("stableSortEntries keeps equal keys in their original order", "[graph][fixed-queue]") {
    struct Entry {
        int key;
        int seq;
    };
    std::vector<Entry> entries{{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    rp::stableSortEntries(entries.data(), entries.data() + entries.size(),
                          [](const Entry& a, const Entry& b) { return a.key < b.key; });

    std::vector<int> keys;
    std::vector<int> seqs;
    for (const auto& e : entries) {
        keys.push_back(e.key);
        seqs.push_back(e.seq);
    }
    CHECK(keys == std::vector<int>{1, 1, 2, 2, 2});
    CHECK(seqs == std::vector<int>{1, 3, 0, 2, 4});  // equal keys did not swap: the sort is stable
}

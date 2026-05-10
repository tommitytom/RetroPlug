#include <catch2/catch_test_macros.hpp>

#include "transport/EventQueue.hpp"

// EventQueue is structurally identical to CommandQueue; these tests cover the
// EventQueue-specific payload shape and basic correctness. The deeper
// concurrency / wraparound tests live in CommandQueueTests since the ring
// implementation is shared in spirit (any divergence would be a regression
// to fix in both).

TEST_CASE("EventQueue is empty on construction", "[EventQueue]") {
    EventQueue q;
    Event out;
    REQUIRE_FALSE(q.tryPop(out));
}

TEST_CASE("EventQueue carries SystemReleased payload", "[EventQueue]") {
    EventQueue q;
    // Sentinel pointer; we don't dereference, just confirm round-trip.
    auto* sentinel = reinterpret_cast<SystemBase*>(0xCAFEBABE);
    REQUIRE(q.tryPush(Event::makeSystemReleased(sentinel)));

    Event out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Event::Kind::SystemReleased);
    REQUIRE(out.payload.systemReleased.system == sentinel);
}

TEST_CASE("EventQueue preserves FIFO order", "[EventQueue]") {
    EventQueue q;
    for (uintptr_t i = 0; i < 16; ++i)
        REQUIRE(q.tryPush(Event::makeSystemReleased(reinterpret_cast<SystemBase*>(i + 1))));

    for (uintptr_t i = 0; i < 16; ++i) {
        Event out;
        REQUIRE(q.tryPop(out));
        REQUIRE(out.payload.systemReleased.system == reinterpret_cast<SystemBase*>(i + 1));
    }
}

TEST_CASE("EventQueue rejects pushes when full", "[EventQueue]") {
    EventQueue q;
    // Bounded ring loses one slot as a sentinel.
    constexpr std::size_t maxFill = EventQueue::kCapacity - 1;

    std::size_t pushed = 0;
    while (q.tryPush(Event::makeSystemReleased(nullptr)))
        ++pushed;

    REQUIRE(pushed == maxFill);
}

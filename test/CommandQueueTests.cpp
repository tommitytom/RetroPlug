#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "transport/CommandQueue.hpp"

TEST_CASE("CommandQueue is empty on construction", "[CommandQueue]") {
    CommandQueue q;
    Command out;
    REQUIRE_FALSE(q.tryPop(out));
}

TEST_CASE("CommandQueue round-trips a single command", "[CommandQueue]") {
    CommandQueue q;
    REQUIRE(q.tryPush(Command::makeButtonPress(7, static_cast<std::uint8_t>(GameboyButton::A), true)));

    Command out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::ButtonPress);
    REQUIRE(out.payload.buttonPress.systemId == 7);
    REQUIRE(out.payload.buttonPress.button == static_cast<std::uint8_t>(GameboyButton::A));
    REQUIRE(out.payload.buttonPress.down == true);

    REQUIRE_FALSE(q.tryPop(out));
}

TEST_CASE("CommandQueue preserves FIFO order", "[CommandQueue]") {
    CommandQueue q;
    for (uint32_t i = 0; i < 10; ++i)
        REQUIRE(q.tryPush(Command::makeButtonPress(i, static_cast<std::uint8_t>(GameboyButton::B), i % 2 == 0)));

    for (uint32_t i = 0; i < 10; ++i) {
        Command out;
        REQUIRE(q.tryPop(out));
        REQUIRE(out.payload.buttonPress.systemId == i);
        REQUIRE(out.payload.buttonPress.down == (i % 2 == 0));
    }
}

TEST_CASE("CommandQueue rejects pushes when full", "[CommandQueue]") {
    CommandQueue q;
    // Bounded ring uses one slot as a sentinel, so capacity = kCapacity - 1.
    constexpr std::size_t maxFill = CommandQueue::kCapacity - 1;

    std::size_t pushed = 0;
    while (q.tryPush(Command::makeButtonPress(0, static_cast<std::uint8_t>(GameboyButton::A), true)))
        ++pushed;

    REQUIRE(pushed == maxFill);

    // After draining one, push should succeed again.
    Command out;
    REQUIRE(q.tryPop(out));
    REQUIRE(q.tryPush(Command::makeButtonPress(0, static_cast<std::uint8_t>(GameboyButton::A), true)));
}

TEST_CASE("CommandQueue wraps around correctly", "[CommandQueue]") {
    CommandQueue q;
    // Push and pop more than capacity to exercise the index-wrap path.
    constexpr uint32_t total = CommandQueue::kCapacity * 3;
    for (uint32_t i = 0; i < total; ++i) {
        REQUIRE(q.tryPush(Command::makeButtonPress(i, static_cast<std::uint8_t>(GameboyButton::A), true)));
        Command out;
        REQUIRE(q.tryPop(out));
        REQUIRE(out.payload.buttonPress.systemId == i);
    }
}

TEST_CASE("CommandQueue carries LoadRom payload", "[CommandQueue]") {
    CommandQueue q;
    // We don't actually dereference the pointer in this test — just confirm
    // the variant payload round-trips intact.
    auto* sentinel = reinterpret_cast<SystemBase*>(0xDEADBEEF);
    REQUIRE(q.tryPush(Command::makeLoadRom(sentinel)));

    Command out;
    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::LoadRom);
    REQUIRE(out.payload.loadRom.newSystem == sentinel);
}

TEST_CASE("CommandQueue makers cover the per-system + project-wide menu actions", "[CommandQueue]") {
    CommandQueue q;

    // Project-wide layout change.
    REQUIRE(q.tryPush(Command::makeSetLayout(SystemLayout::Grid)));
    // Per-system actions.
    REQUIRE(q.tryPush(Command::makeResetSystem(11)));
    REQUIRE(q.tryPush(Command::makeNewSram(12)));
    REQUIRE(q.tryPush(Command::makeSetFastBoot(13, false)));
    REQUIRE(q.tryPush(Command::makeSetModel(14, SameBoyModel::DmgB)));
    REQUIRE(q.tryPush(Command::makeSetReloadOnRomChange(15, true)));

    Command out;

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::SetLayout);
    REQUIRE(out.payload.setLayout.layout == SystemLayout::Grid);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::ResetSystem);
    REQUIRE(out.payload.resetSystem.id == 11);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::NewSram);
    REQUIRE(out.payload.newSram.id == 12);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::SetFastBoot);
    REQUIRE(out.payload.setFastBoot.id == 13);
    REQUIRE(out.payload.setFastBoot.enabled == false);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::SetModel);
    REQUIRE(out.payload.setModel.id == 14);
    REQUIRE(out.payload.setModel.model == SameBoyModel::DmgB);

    REQUIRE(q.tryPop(out));
    REQUIRE(out.kind == Command::Kind::SetReloadOnRomChange);
    REQUIRE(out.payload.setReloadOnRomChange.id == 15);
    REQUIRE(out.payload.setReloadOnRomChange.enabled == true);

    REQUIRE_FALSE(q.tryPop(out));
}

TEST_CASE("CommandQueue is safe across one producer and one consumer", "[CommandQueue][threading]") {
    CommandQueue q;
    constexpr uint32_t kMessages = 10'000;

    std::atomic<bool> startGate{false};
    std::vector<uint32_t> received;
    received.reserve(kMessages);

    std::thread consumer([&]() {
        while (!startGate.load(std::memory_order_acquire)) { /* spin */ }
        Command out;
        while (received.size() < kMessages) {
            if (q.tryPop(out)) {
                received.push_back(out.payload.buttonPress.systemId);
            }
        }
    });

    std::thread producer([&]() {
        startGate.store(true, std::memory_order_release);
        for (uint32_t i = 0; i < kMessages; ++i) {
            // tryPush may transiently fail when the queue is full; spin
            // until the consumer drains. This is the realistic UI→DSP shape:
            // backpressure rather than drop on full.
            while (!q.tryPush(Command::makeButtonPress(i, static_cast<std::uint8_t>(GameboyButton::A), false))) {
                /* spin */
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(received.size() == kMessages);
    for (uint32_t i = 0; i < kMessages; ++i)
        REQUIRE(received[i] == i);
}

// The Mesen shared pieces this fork patches for the block thread, held on their own rather than through a console:
//
// - BaseControlDevice::ClearState leaves a state EMPTY (its buffer kept), which four devices read as their contract.
//   A controller hub appends each port's state to what a clear leaves, so a cleared state that was 64 zero bytes turned
//   every hub's composite state into a 64-byte prefix that SetRawState then read as port lengths, and the barcode
//   readers take "no state" to mean the tape is not playing, so a cleared-but-non-empty state made every input poll
//   arm a stream it had no data for (a length_error out of the Epoch Barcode Battler's InitBarcodeStream).
// - NotificationManager iterates a copy of its listener list, reused per thread and per nesting depth, so a
//   notification allocates nothing once its depth has been reached. These hold what that copy must keep true: a
//   listener destroyed while the notification runs is not called after it, and a listener that sends a notification of
//   its own does not disturb the one being iterated.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NES/Input/BarcodeBattlerReader.h"
#include "NES/Input/FourScore.h"
#include "Shared/NotificationManager.h"

namespace {

// Records each notification it is called for, under a name, so a run's exact sequence can be compared.
struct NamedListener : INotificationListener {
    std::string                        name;
    std::vector<std::string>*          seen = nullptr;
    std::function<void()>              on = {};

    void ProcessNotification(ConsoleNotificationType, void*) override {
        seen->push_back(name);
        if (on) on();
    }
};

std::shared_ptr<NamedListener> listener(std::string name, std::vector<std::string>* seen) {
    auto l = std::make_shared<NamedListener>();
    l->name = std::move(name);
    l->seen = seen;
    return l;
}

}  // namespace

TEST_CASE("a cleared controller device's state is still empty, and a hub's composite state round-trips",
          "[engine-graph][mesen][control-state]") {
    SECTION("the Epoch Barcode Battler") {
        // No emulator: ClearState, SetStateFromInput and OnAfterSetState only read the state (an armed stream would
        // read the clock, which is why this stays unarmed, as an empty state is meant to).
        BarcodeBattlerReader barcode(nullptr);
        barcode.ClearState();
        CHECK(barcode.GetRawState().State.empty());
        CHECK_NOTHROW(barcode.SetStateFromInput());
        // What an input poll does after it sets a device's state: with an empty state this must arm nothing. With 64
        // zero bytes it armed the stream, and InitBarcodeStream's fixed padding threw std::length_error from the poll.
        CHECK_NOTHROW(barcode.OnAfterSetState());
        CHECK(barcode.GetRawState().State.empty());
    }

    SECTION("a Four Score's polled state is one length byte per empty port, not a cleared prefix") {
        // Four NES controllers with no keys mapped, so a poll reads no key and needs no emulator.
        ControllerConfig ports[4] = {};
        for (ControllerConfig& port : ports) port.Type = ControllerType::NesController;
        auto hub = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);

        hub->SetStateFromInput();  // what BaseControlManager::UpdateInputState does before OnAfterSetState
        // ControllerHub builds its composite state by appending [length][bytes] per port to what ClearState left: four
        // empty ports are four zero length bytes. A cleared state of 64 zero bytes would sit in front of them, and
        // SetRawState reads lengths from index 0, so every port would parse as empty and the round trip below would
        // lose a pressed button.
        CHECK(hub->GetRawState().State == std::vector<std::uint8_t>(4, 0));

        hub->ClearState();
        CHECK(hub->GetRawState().State.empty());
    }

    SECTION("a hub's composite state round-trips through another hub") {
        ControllerConfig ports[4] = {};
        for (ControllerConfig& port : ports) port.Type = ControllerType::NesController;
        auto from = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);
        auto to = std::make_unique<FourScore>(nullptr, ControllerType::FourScore, 0, ports);

        // Port 0's first button down, then a hub refresh, which is what Serializer/rewind restore into a hub.
        from->GetController(0)->SetRawState(ControlDeviceState{std::vector<std::uint8_t>{0x01}});
        from->RefreshHubState();

        ControlDeviceState saved = from->GetRawState();
        REQUIRE(saved.State.size() == 5);  // port 0 is one byte, ports 1-3 are empty: [1, 1, 0, 0, 0]
        CHECK(saved.State[0] == 1);
        CHECK(saved.State[1] == 0x01);
        CHECK(saved.State[2] == 0);

        to->SetRawState(saved);
        ControlDeviceState back = to->GetRawState();
        CHECK(!(back != saved));
        CHECK(to->GetTextState() == from->GetTextState());
        CHECK(to->GetController(0)->IsPressed(0));  // the pressed button reached the child, not just the hub's copy
    }
}

TEST_CASE("a notification's copy of the listener list survives a nested send and an unregister",
          "[engine-graph][mesen][notification]") {
    NotificationManager manager;
    std::vector<std::string> seen;

    auto first = listener("first", &seen);
    auto second = listener("second", &seen);
    auto third = listener("third", &seen);

    // The outer run destroys `second` (the manager and the copy it iterates hold only weak_ptrs, so this handle is its
    // only owner) and sends a notification of its own. The nested run must see its own listener set with its own
    // buffer, and the outer run must keep iterating the copy it captured: `second` is dead by the time the outer loop
    // reaches it, so it is skipped rather than called.
    first->on = [&] {
        if (!second) return;  // only the outer run does this, not the nested one that follows
        second.reset();
        manager.SendNotification(ConsoleNotificationType::ConfigChanged);
    };

    manager.RegisterNotificationListener(first);
    manager.RegisterNotificationListener(second);
    manager.RegisterNotificationListener(third);

    manager.SendNotification(ConsoleNotificationType::PpuFrameDone);
    const std::vector<std::string> expected{"first", "first", "third", "third"};
    CHECK(seen == expected);  // first + third nested, then first + third in the outer run (second already dead)

    // The next run sees the same set, with `second` gone for good.
    seen.clear();
    manager.SendNotification(ConsoleNotificationType::PpuFrameDone);
    CHECK(seen == std::vector<std::string>{"first", "third"});
}

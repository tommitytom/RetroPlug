#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "system/InputTypes.hpp"
#include "system/SystemTypes.hpp"

// SPSC command queue: UI thread → DSP thread.
//
// Hand-rolled bounded ring of POD Command records; no allocation on either
// thread once constructed. Commands are small tagged-union structs so a
// variant-of-trivially-copyable types isn't needed.
//
// Step 2 inhabitants: ButtonPressCommand (keyboard → emulator buttons).
// Future steps add LoadRom, SetSetting, ResetSystem, etc.

struct ButtonPressCommand {
    SystemId      systemId;
    GameboyButton button;
    bool          down;
};

struct Command {
    enum class Kind : std::uint8_t {
        None        = 0,
        ButtonPress = 1,
    };

    Kind kind = Kind::None;
    union Payload {
        ButtonPressCommand buttonPress;
        Payload() : buttonPress{} {}
    } payload;

    Command() = default;

    static Command makeButtonPress(SystemId id, GameboyButton b, bool down) {
        Command c;
        c.kind = Kind::ButtonPress;
        c.payload.buttonPress = ButtonPressCommand{id, b, down};
        return c;
    }
};

// Power-of-two bounded SPSC ring. Single producer (UI thread), single
// consumer (DSP thread). Lock-free on both sides; tryPush returns false when
// full so the UI can drop or coalesce as it sees fit. 1024 entries handles
// chord-style key spam comfortably without ever touching the heap.
class CommandQueue {
public:
    static constexpr std::size_t kCapacity = 1024;
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "kCapacity must be a power of two");

    CommandQueue() = default;
    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    bool tryPush(const Command& c) {
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & (kCapacity - 1);
        if (next == readIdx.load(std::memory_order_acquire))
            return false; // full
        slots[w] = c;
        writeIdx.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(Command& out) {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        if (r == writeIdx.load(std::memory_order_acquire))
            return false; // empty
        out = slots[r];
        readIdx.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> writeIdx{0};
    alignas(64) std::atomic<std::size_t> readIdx{0};
    Command slots[kCapacity]{};
};

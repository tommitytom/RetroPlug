#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "system/InputTypes.hpp"
#include "system/SystemTypes.hpp"

class SystemBase;

// SPSC command queue: UI thread → DSP thread.
//
// Hand-rolled bounded ring of POD Command records; no allocation on either
// thread once constructed. Commands are small tagged-union structs so a
// variant-of-trivially-copyable types isn't needed.
//
// Audio-thread invariant: the DSP must never allocate, free, or block when
// processing commands. `LoadRom` therefore carries a fully-constructed
// `SystemBase*` (the UI thread did `make_unique` + onActivate then released
// the unique_ptr); the DSP just swaps it into the project. The displaced
// system, if any, is shipped back through the EventQueue for the UI to
// `delete`.

struct ButtonPressCommand {
    SystemId      systemId;
    GameboyButton button;
    bool          down;
};

struct LoadRomCommand {
    // Ownership transferred to DSP — DSP must either install via
    // Project::swapSystem (which doesn't alloc/free) or, on failure, ship it
    // back through the EventQueue for the UI thread to delete. Replaces
    // slot 0 if the project is empty; otherwise replaces the focused
    // system (or, if none focused, slot 0 fallback).
    SystemBase* newSystem;
};

// Append a new system to the project. UI built it (full make_unique +
// onActivate); DSP adopts the raw pointer with no allocation.
struct AddSystemCommand {
    SystemBase* newSystem;
};

// Remove the system identified by `id`. The displaced pointer is shipped
// back through the EventQueue (`SystemReleased`) for off-thread delete.
struct RemoveSystemCommand {
    SystemId id;
};

// Replace one specific system by id (used by "Replace ROM" on a focused
// tile). Same ownership rules as LoadRom; the displaced ptr returns via
// the EventQueue.
struct ReplaceSystemCommand {
    SystemId    id;
    SystemBase* newSystem;
};

// Move a system into a serial-link group (0 = standalone). The DSP
// rebuilds link-group membership on receipt; UI gets a ConfigChanged so
// the menu chrome updates.
struct SetLinkGroupCommand {
    SystemId     id;
    std::uint8_t groupId;
};

struct Command {
    enum class Kind : std::uint8_t {
        None          = 0,
        ButtonPress   = 1,
        LoadRom       = 2,
        AddSystem     = 3,
        RemoveSystem  = 4,
        ReplaceSystem = 5,
        SetLinkGroup  = 6,
    };

    Kind kind = Kind::None;
    union Payload {
        ButtonPressCommand   buttonPress;
        LoadRomCommand       loadRom;
        AddSystemCommand     addSystem;
        RemoveSystemCommand  removeSystem;
        ReplaceSystemCommand replaceSystem;
        SetLinkGroupCommand  setLinkGroup;
        Payload() : buttonPress{} {}
    } payload;

    Command() = default;

    static Command makeButtonPress(SystemId id, GameboyButton b, bool down) {
        Command c;
        c.kind = Kind::ButtonPress;
        c.payload.buttonPress = ButtonPressCommand{id, b, down};
        return c;
    }

    static Command makeLoadRom(SystemBase* newSystem) {
        Command c;
        c.kind = Kind::LoadRom;
        c.payload.loadRom = LoadRomCommand{newSystem};
        return c;
    }

    static Command makeAddSystem(SystemBase* newSystem) {
        Command c;
        c.kind = Kind::AddSystem;
        c.payload.addSystem = AddSystemCommand{newSystem};
        return c;
    }

    static Command makeRemoveSystem(SystemId id) {
        Command c;
        c.kind = Kind::RemoveSystem;
        c.payload.removeSystem = RemoveSystemCommand{id};
        return c;
    }

    static Command makeReplaceSystem(SystemId id, SystemBase* newSystem) {
        Command c;
        c.kind = Kind::ReplaceSystem;
        c.payload.replaceSystem = ReplaceSystemCommand{id, newSystem};
        return c;
    }

    static Command makeSetLinkGroup(SystemId id, std::uint8_t groupId) {
        Command c;
        c.kind = Kind::SetLinkGroup;
        c.payload.setLinkGroup = SetLinkGroupCommand{id, groupId};
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

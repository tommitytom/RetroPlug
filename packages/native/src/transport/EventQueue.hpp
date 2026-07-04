#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class SystemBase;

// SPSC event queue: DSP thread → UI thread.
//
// Mirrors CommandQueue's hand-rolled bounded ring shape. The DSP pushes
// events at the top of run() (or wherever it makes sense in a non-allocating
// realtime context) and the UI drains them in uiIdle.
//
// `SystemReleased` ships ownership of an unwanted `SystemBase*` back to the
// UI for off-thread `delete`. `ConfigChanged` is a payload-less "the project
// *structure* changed (system added/removed/model/link)" signal — the UI
// re-fetches its system list + focus but keeps the settings it owns.
// `ProjectLoaded` is the payload-less "the whole project was replaced" signal
// (setState / LoadProject) — the UI does a full re-seed including settings,
// because a load is the only non-UI source of a settings change.

struct SystemReleasedEvent {
    // TODO: std::unique_ptr?
    SystemBase* system; // ownership; UI deletes
};

struct Event {
    enum class Kind : std::uint8_t {
        None            = 0,
        SystemReleased  = 1,
        ConfigChanged   = 2,
        ProjectLoaded   = 3,
    };

    Kind kind = Kind::None;
    union Payload {
        SystemReleasedEvent systemReleased;
        Payload() : systemReleased{nullptr} {}
    } payload;

    Event() = default;

    static Event makeSystemReleased(SystemBase* sys) {
        Event e;
        e.kind = Kind::SystemReleased;
        e.payload.systemReleased = SystemReleasedEvent{sys};
        return e;
    }

    static Event makeConfigChanged() {
        Event e;
        e.kind = Kind::ConfigChanged;
        return e;
    }

    static Event makeProjectLoaded() {
        Event e;
        e.kind = Kind::ProjectLoaded;
        return e;
    }
};

class EventQueue {
public:
    static constexpr std::size_t kCapacity = 256;
    static_assert((kCapacity & (kCapacity - 1)) == 0,
                  "kCapacity must be a power of two");

    EventQueue() = default;
    EventQueue(const EventQueue&)            = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    bool tryPush(const Event& e) {
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t next = (w + 1) & (kCapacity - 1);
        if (next == readIdx.load(std::memory_order_acquire))
            return false;
        slots[w] = e;
        writeIdx.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(Event& out) {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        if (r == writeIdx.load(std::memory_order_acquire))
            return false;
        out = slots[r];
        readIdx.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<std::size_t> writeIdx{0};
    alignas(64) std::atomic<std::size_t> readIdx{0};
    Event slots[kCapacity]{};
};

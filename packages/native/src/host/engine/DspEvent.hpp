#pragma once

class SystemBase;  // pointer-only — keeps DspEvent trivially copyable

// An audio-thread → control-thread event for the greenfield host, carried by an
// SpscRing<DspEvent, N>. The audio thread OWNS the Project's cores while it runs, so a system
// removed or displaced by a lifecycle command can't be freed there (delete is non-RT). Instead the
// audio thread releases ownership as a raw pointer through this ring; the control thread drains it
// (drainReleased) and `delete`s each off the audio thread. Mirrors production's
// Event::SystemReleased → PluginUI::drainEvents. POD / trivially copyable.
struct DspEvent {
    enum class Kind : unsigned char { None = 0, SystemReleased = 1 };

    Kind kind = Kind::None;
    union {
        struct { SystemBase* sys; } released;  // ownership handed to the control thread; it deletes
    };

    DspEvent() : kind(Kind::None), released{nullptr} {}
};

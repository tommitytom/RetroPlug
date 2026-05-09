#pragma once

// SPSC command queue: UI thread → DSP thread.
// Empty in Step 1 — the type exists so subsequent steps (keyboard input at
// Step 2, ROM picker at Step 3) can fill it without churning the surrounding
// architecture. Concrete implementation (likely a moodycamel::ReaderWriterQueue
// or a hand-rolled bounded SPSC) lands when the first inhabitant is added.
//
// The intended payload is a std::variant of command structs (AddSystem,
// RemoveSystem, SetSetting, ButtonPress, ...).

class CommandQueue {
    // Reserved: do not use yet.
};

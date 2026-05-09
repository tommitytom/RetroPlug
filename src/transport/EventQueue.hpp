#pragma once

// SPSC event queue: DSP thread → UI thread.
// Empty in Step 1 — the type exists so subsequent steps can fill it without
// churning the surrounding architecture. Concrete implementation lands when
// the first inhabitant is added (likely SystemReady / SystemErrored /
// ConfigChanged at Step 3).

class EventQueue {
    // Reserved: do not use yet.
};

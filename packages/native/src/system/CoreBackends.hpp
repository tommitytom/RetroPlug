#pragma once

class SystemFactory;

// Register the built-in core backends (sameboy + mesen) on `factory` — the one build path, keyed by the
// `core` value (Mesen serves both NES and GBA, dispatching internally on platform). Each host calls this
// once while composing its own backend graph. (The Engine pre-reserves its Project so the audio thread's
// adopt/swap never reallocates.)
void registerCoreBackends(SystemFactory& factory);

#pragma once

#include <cstdint>

// A core's per-channel audio tap: the capability a split routing arms (a NES's Pulse | TND | Expansion pins, or its
// five APU channels, exported one stream each). `mode` is the backend's own numbering, the one its config's
// channelExportMode carries: 0 is the mixed output, and the modes above it are the streams the core can capture.
//
// One capability object rather than a virtual per caller, as IDebugTarget is for the debugger: a caller asks
// SystemBase::channelTap() and branches on null, so the engine can arm a routing without naming the console. The
// system owns it; the engine asks only a core it has built, which has activated.
//
// setExportMode runs between two blocks on the block thread (the plan's swap trailer), and may also be called on the
// control thread while the engine is quiescent. An arm must neither allocate nor restart the core: a routing flip must
// not reset a playing game. exportMode is the mode the core was last given (a NES's construct-time value before any
// arm), and is what a caller records so leaving a split mode puts the core back as it found it.
class ChannelTap {
public:
    virtual ~ChannelTap() = default;

    virtual std::uint32_t exportMode() const = 0;
    virtual void          setExportMode(std::uint32_t mode) = 0;
};

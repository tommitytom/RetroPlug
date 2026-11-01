#pragma once

// The surface device link rp.launchpad.surface-in / -out bind through rp.host.devices/1, over a LaunchpadLink: the
// device's MIDI in both directions, its connected state and the pushes that went nowhere. Opening and pairing the
// ports, the farewell the device needs before a host lets it go, and writing the queued LED traffic out on the main
// loop all stay the host's (LaunchpadHost and its pump). Header-only, as it is a few forwards over the link; the
// standalone is the only host that owns a Launchpad, and the only caller.

#include <cstddef>
#include <cstdint>

#include "host/devices/DeviceLinks.hpp"
#include "host/launchpad/LaunchpadLink.hpp"

namespace retroplug {

class LaunchpadSurfaceLink final : public rp::host::SurfaceLink {
public:
    explicit LaunchpadSurfaceLink(LaunchpadLink& link) : link_(link) {}

    // [audio] The messages the device sent since the last drain, copied straight out of the link's fixed slots: the
    // block thread allocates nothing and frees nothing, and a message the caller has no room for stays queued, so a
    // short answer means the ring is empty.
    std::size_t   drainInput(rp::host::DeviceMessage* out, std::size_t capacity) override {
        return link_.drainInput(out, capacity);
    }
    void          pushOutput(const std::uint8_t* data, std::size_t size) override { link_.pushOutput(data, size); }
    bool          connected() const override { return link_.isConnected(); }
    std::uint64_t dropped() const override { return link_.messagesDropped(); }

private:
    LaunchpadLink& link_;
};

}  // namespace retroplug

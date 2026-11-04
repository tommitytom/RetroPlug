#pragma once

// The surface device link rp.launchpad.surface-in / -out bind through rp.host.devices/2, over a LaunchpadLink: the
// device's MIDI in both directions, its connected state and the pushes that went nowhere. Opening and pairing the
// ports, the farewell the device needs before a host lets it go, and writing the queued LED traffic out on the main
// loop all stay the host's (LaunchpadHost and its pump). Header-only, as it is a few forwards over the link; the
// standalone is the only host that owns a Launchpad, and the only caller.

#include <cstddef>
#include <cstdint>

#include "launchpad/SurfaceLink.hpp"
#include "host/launchpad/LaunchpadLink.hpp"

namespace retroplug {

class LaunchpadSurfaceLink final : public rp::launchpad::SurfaceLink {
public:
    explicit LaunchpadSurfaceLink(LaunchpadLink& link) : link_(link) {}

    // [audio] The messages the device sent since the last drain, copied straight out of the link's fixed slots: the
    // block thread allocates nothing and frees nothing, and a message the caller has no room for stays queued, so a
    // short answer means the ring is empty.
    std::size_t   drainInput(rp::launchpad::DeviceMessage* out, std::size_t capacity) override {
        return link_.drainInput(out, capacity);
    }
    void          pushOutput(const std::uint8_t* data, std::size_t size) override { link_.pushOutput(data, size); }
    bool          connected() const override { return link_.isConnected(); }
    // The link's two outbound counts (messagesDropped and messagesDetached): a push the device was not there for went
    // nowhere just as a full ring's did, and a node reads one number for "the device never got this". The inbound loss
    // (messagesInboundDropped) is what the device sent that the audio side missed, not a frame it never got, so the
    // handle does not answer it.
    std::uint64_t dropped() const override { return link_.messagesDropped() + link_.messagesDetached(); }

private:
    LaunchpadLink& link_;
};

}  // namespace retroplug

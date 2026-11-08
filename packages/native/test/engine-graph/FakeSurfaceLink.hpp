#pragma once

// The fake control-surface link, shared by the cases that drive a node over rp.host.devices/2: it scripts what the
// device sent, records what a node handed it, and counts the nodes holding it (through the hooks it is installed with,
// TestDevices.hpp). A message it records lives in its own slot, so a case that counts allocations reserves the room it
// needs before counting. A push it takes nowhere is counted as ControllerLink counts one: the device was not there for
// it.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "launchpad/SurfaceLink.hpp"

namespace rp::host::test {

using rp::launchpad::DeviceMessage;
using rp::launchpad::kMaxDeviceMessage;

// One message in storage the caller owns, as a test scripts the device's input.
inline DeviceMessage deviceMessage(const std::vector<std::uint8_t>& bytes) {
    DeviceMessage m;
    m.size = static_cast<std::uint16_t>(bytes.size());
    std::copy(bytes.begin(), bytes.end(), m.data);
    return m;
}

class FakeSurfaceLink final : public rp::launchpad::SurfaceLink {
public:
    struct Out {
        std::uint16_t size = 0;
        std::uint8_t  data[kMaxDeviceMessage] = {};
    };

    void reserve(std::size_t in, std::size_t out) {
        input.reserve(in);
        outputs.reserve(out);
    }

    std::size_t drainInput(DeviceMessage* out, std::size_t capacity) override {
        std::size_t n = 0;
        while (next < input.size() && n < capacity) out[n++] = input[next++];
        return n;
    }
    void pushOutput(const std::uint8_t* data, std::size_t size) override {
        if (!attached) {  // the device is not there to take it: counted apart, as ControllerLink counts it
            ++detached;
            return;
        }
        Out m;
        m.size = static_cast<std::uint16_t>(std::min(size, kMaxDeviceMessage));
        std::memcpy(m.data, data, m.size);
        outputs.push_back(m);
    }
    std::uint32_t generation() const override { return attached ? claim : 0; }
    // The pushes that went nowhere: this fake has no send ring, so a link that is down is the only way to lose one.
    std::uint64_t dropped() const override { return detached; }
    void acquire() { ++acquires; }
    void release() { ++releases; }

    bool                       attached = true;
    std::uint32_t              claim = 1;  // the claim's number: what the node reports as `connected`
    std::uint64_t              detached = 0;  // pushed while there was no device
    int                        acquires = 0;
    int                        releases = 0;
    std::vector<DeviceMessage> input;    // scripted: what the device sent, drained oldest first
    std::size_t                next = 0;  // how much of it a node has drained
    std::vector<Out>           outputs;
};

}  // namespace rp::host::test

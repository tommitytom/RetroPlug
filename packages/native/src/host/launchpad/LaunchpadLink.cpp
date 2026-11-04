#include "host/launchpad/LaunchpadLink.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <utility>

namespace retroplug {

LaunchpadLink::LaunchpadLink(PortFactory factory)
    : factory_(std::move(factory)), inRing_(std::make_unique<rp::launchpad::DeviceMessage[]>(kInCapacity)) {}

LaunchpadLink::~LaunchpadLink() { disconnect(); }

bool LaunchpadLink::connect(const std::string& inName, const std::string& outName) {
    disconnect();  // idempotent; also releases a previously claimed pair before we claim another
    if (!factory_) {
        error_ = "no MIDI backend";
        return false;
    }
    try {
        // The receiver runs on the backend's callback thread. `this` outlives the port (the port is reset
        // before the link is destroyed, and resetting it cancels the callback), so capturing it is safe.
        port_ = factory_(inName, outName, [this](const std::uint8_t* data, std::size_t n) {
            pushInputRing(data, n);
        });
    } catch (const std::exception& e) {
        error_ = e.what();
        port_.reset();
        return false;
    }
    if (!port_) {
        error_ = "MIDI port unavailable";
        return false;
    }
    error_.clear();
    connected_.store(true, std::memory_order_release);
    return true;
}

void LaunchpadLink::disconnect() {
    if (!port_) {
        connected_.store(false, std::memory_order_release);
        return;
    }
    // Stop the audio thread queueing first, so nothing is still arriving while we say goodbye.
    connected_.store(false, std::memory_order_release);
    pump();  // flush whatever the last block queued, so the surface is not left half-painted
    if (!farewell_.empty()) port_->send(farewell_.data(), farewell_.size());
    port_.reset();  // cancels the callback: no receiver runs after this returns

    // Drop anything still queued in either direction - it belongs to a device we no longer hold.
    inHead_.store(inTail_.load(std::memory_order_acquire), std::memory_order_release);
    OutMessage discard;
    while (outRing_.tryPop(discard)) {}
}

void LaunchpadLink::pushInputRing(const std::uint8_t* data, std::size_t n) {
    if (!data || n == 0) return;
    if (n > kMaxOutMessage) {  // wider than any message the protocol produces: count it, never truncate it
        inboundDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const std::size_t t    = inTail_.load(std::memory_order_relaxed);
    const std::size_t next = (t + 1) % kInCapacity;
    if (next == inHead_.load(std::memory_order_acquire)) {  // full -> drop
        inboundDropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    inRing_[t].size = static_cast<std::uint16_t>(n);
    std::memcpy(inRing_[t].data, data, n);
    inTail_.store(next, std::memory_order_release);
}

std::size_t LaunchpadLink::drainInput(rp::launchpad::DeviceMessage* out, std::size_t capacity) {
    if (!out) return 0;
    std::size_t       h = inHead_.load(std::memory_order_relaxed);
    const std::size_t t = inTail_.load(std::memory_order_acquire);
    std::size_t       n = 0;
    while (h != t && n < capacity) {
        out[n++] = inRing_[h];
        h        = (h + 1) % kInCapacity;
    }
    inHead_.store(h, std::memory_order_release);
    return n;
}

void LaunchpadLink::pushOutput(const std::uint8_t* data, std::size_t n) {
    if (!data || n == 0) return;  // nothing to send: not a push that went nowhere
    if (!connected_.load(std::memory_order_acquire)) {  // the device is not there to take it
        detached_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (n > kMaxOutMessage) {  // longer than any message the protocol can produce - a bug upstream, not a cap
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    OutMessage m;
    m.len = static_cast<std::uint16_t>(n);
    std::copy(data, data + n, m.data);
    if (!outRing_.tryPush(m)) dropped_.fetch_add(1, std::memory_order_relaxed);
}

void LaunchpadLink::pump() {
    OutMessage m;
    while (outRing_.tryPop(m)) {
        if (!port_ || m.len == 0) continue;  // still drained, so a reconnect starts clean
        port_->send(m.data, m.len);
        sent_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace retroplug

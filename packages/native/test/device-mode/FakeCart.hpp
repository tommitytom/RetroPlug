#pragma once

// A cart link that records what it is handed (CartRecord, DeviceModeEntries.hpp), built twice into
// retroplug-device-mode-test as PlanNodes.hpp's nodes are: in device mode by DeviceModeNodes.cpp, where it derives the
// device-mode build's rp::n8::CartLink (rp_devmode's), which is the one that build's rp.n8.nes casts its acquire to,
// and on the desktop by DevicePlan.test.cpp. Portable: no allocation, no lock.

#include <cstddef>
#include <cstdint>

#include "DeviceModeEntries.hpp"
#include "n8/CartLink.hpp"

namespace rp::graph::devtest {

class FakeCart final : public rp::n8::CartLink {
public:
    CartRecord* record = nullptr;

    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush, double) override {
        if (!record) return false;
        if (!record->connected) {
            ++record->refused;
            return false;
        }
        if (record->count < CartRecord::kPushes) {
            CartPush& p = record->pushes[record->count];
            p.frame = frame;
            p.size = static_cast<std::uint32_t>(size);
            p.flush = flush;
            for (std::size_t i = 0; i < size && i < sizeof p.bytes; ++i) p.bytes[i] = data[i];
        }
        ++record->count;
        return true;
    }
    bool          connected() const override { return record && record->connected; }
    std::uint64_t dropped() const override { return record ? record->refused : 0; }
    void          setExpansionVolume(int value) override {
        if (record) record->volume = value;
    }
};

}  // namespace rp::graph::devtest

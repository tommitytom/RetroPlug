// The device-mode half of retroplug-device-mode-test: built with RP_DEVICE and with neither exceptions nor RTTI
// (cmake/tests.cmake), beside the device-mode builds of CoreNodes.cpp and DspNodes.cpp. It holds the echo node's
// package (echo, pair, shaper, bare), the fault node's and the plan cases' (PlanNodes.hpp), a fake cart link
// (FakeCart.hpp) and the box's own cart link (pico/n8-host/BoxCartLink.cpp, compiled into these objects too); the test
// reaches them, and the node packages, only through their C entries.

#include <cstdint>
#include <new>

#include "BoxCartLink.hpp"
#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"
#include "FakeCart.hpp"
#include "PlanNodes.hpp"

#ifndef RP_DEVICE
#error "DeviceModeNodes.cpp is the device-mode half: it is built with RP_DEVICE"
#endif

namespace rp::graph::devtest {
namespace {

const rp_node_type* const kFaultTypes[] = {rp::node_type<Fault>()};
const rp_package kFaultPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.fault","version":"1.0.0"})", 1, kFaultTypes};

const rp_node_type* const kTypes[] = {rp::node_type<Echo>(), rp::node_type<Pair>(), rp::node_type<Shaper>(),
                                      rp::node_type<Bare>()};
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 4, kTypes};

const rp_node_type* const kPlanTypes[] = {rp::node_type<Source>(), rp::node_type<Digest>(), rp::node_type<Lockstep>()};
const rp_package kPlanPackage{RP_ABI_VERSION, R"({"id":"rp.devtest.plan","version":"1.0.0"})", 3, kPlanTypes};

FakeCart gCart;

// The box's link, remade for each test, with the record its writer fills.
rp::box::BoxCartLink::Storage gBoxRing;
alignas(rp::box::BoxCartLink) unsigned char gBoxMemory[sizeof(rp::box::BoxCartLink)];
rp::box::BoxCartLink* gBox = nullptr;
BoxRecord*            gBoxRecord = nullptr;

bool boxFifo(void*, const std::uint8_t* data, std::size_t size, std::uint32_t messages, std::uint32_t firstUs) {
    BoxRecord& r = *gBoxRecord;
    if (r.writeCount < BoxRecord::kWrites) {
        BoxWrite& w = r.writes[r.writeCount];
        w.size = static_cast<std::uint32_t>(size);
        w.messages = messages;
        w.firstUs = firstUs;
        for (std::size_t i = 0; i < size && i < sizeof w.bytes; ++i) w.bytes[i] = data[i];
    }
    ++r.writeCount;
    return !r.fifoFails;
}
bool boxVolume(void*, int value, bool) {
    BoxRecord& r = *gBoxRecord;
    if (r.volumeCount < 8) r.volumes[r.volumeCount] = value;
    ++r.volumeCount;
    return true;
}
std::uint32_t boxNow(void*) { return gBoxRecord->now ? gBoxRecord->now(gBoxRecord->clockCtx) : 0; }
bool          boxArrival(void*, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us) {
    return gBoxRecord->arrival && gBoxRecord->arrival(gBoxRecord->clockCtx, frame, data, size, us);
}

}  // namespace
}  // namespace rp::graph::devtest

extern "C" const rp_package* rp_devtest_package_device_mode(void) { return &rp::graph::devtest::kPackage; }
extern "C" const rp_package* rp_devtest_fault_package_device_mode(void) { return &rp::graph::devtest::kFaultPackage; }
extern "C" const rp_package* rp_devtest_plan_package_device_mode(void) { return &rp::graph::devtest::kPlanPackage; }
extern "C" int rp_devtest_plan_live_device_mode(void) { return rp::graph::devtest::gLive; }
extern "C" void* rp_devtest_cart_device_mode(CartRecord* record) {
    rp::graph::devtest::gCart.record = record;
    return static_cast<rp::n8::CartLink*>(&rp::graph::devtest::gCart);
}

extern "C" void* rp_devtest_box_link_device_mode(BoxRecord* record) {
    using namespace rp::graph::devtest;
    gBoxRecord = record;
    gBoxRing = rp::box::BoxCartLink::Storage{};
    gBox = new (gBoxMemory) rp::box::BoxCartLink(gBoxRing);  // trivially destructible state: nothing to end first
    gBox->setClock({nullptr, &boxNow, &boxArrival});
    return static_cast<rp::n8::CartLink*>(gBox);
}
extern "C" void rp_devtest_box_connect_device_mode(bool connected) {
    rp::graph::devtest::gBox->setConnected(connected);
}
extern "C" void rp_devtest_box_drain_device_mode(std::uint32_t nowMs) {
    using namespace rp::graph::devtest;
    gBox->drain({nullptr, &boxFifo, &boxVolume}, nowMs);
}
extern "C" void rp_devtest_box_counts_device_mode(BoxCounts* out) {
    const auto& c = rp::graph::devtest::gBox->counts();
    *out = BoxCounts{c.received,  c.made,   c.notReady, c.full,   c.released,
                     c.forwarded, c.failed, c.sysex,    c.writes, c.maxBatch,
                     c.fromPlanInput(), rp::graph::devtest::gBox->expansionVolume(),
                     rp::graph::devtest::gBox->holders()};
}
extern "C" void rp_devtest_box_acquired_device_mode(void*) { rp::graph::devtest::gBox->acquire(); }
extern "C" void rp_devtest_box_released_device_mode(void*) { rp::graph::devtest::gBox->release(); }

// The device-mode half of retroplug-device-mode-test: built with RP_DEVICE and with neither exceptions nor RTTI
// (cmake/tests.cmake), beside the device-mode builds of CoreNodes.cpp and DspNodes.cpp. It holds the echo node's
// package (echo, pair, shaper); the test reaches it, and the two node packages, only through their C entries.

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"

#ifndef RP_DEVICE
#error "DeviceModeNodes.cpp is the device-mode half: it is built with RP_DEVICE"
#endif

namespace rp::graph::devtest {
namespace {

const rp_node_type* const kTypes[] = {rp::node_type<Echo>(), rp::node_type<Pair>(), rp::node_type<Shaper>()};
const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.devtest","version":"1.0.0"})", 3, kTypes};

}  // namespace
}  // namespace rp::graph::devtest

extern "C" const rp_package* rp_devtest_package_device_mode(void) { return &rp::graph::devtest::kPackage; }

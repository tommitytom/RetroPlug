// The device-mode half of retroplug-device-mode-test: built with RP_DEVICE and with neither exceptions nor RTTI
// (cmake/tests.cmake), beside the device-mode builds of CoreNodes.cpp and DspNodes.cpp. It holds the echo node's
// package (echo, pair, shaper, bare), the fault node's and the plan cases' (PlanNodes.hpp); the test reaches them, and
// the node packages, only through their C entries.

#include <cstdint>

#include "DeviceModeEntries.hpp"
#include "EchoNode.hpp"
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

}  // namespace
}  // namespace rp::graph::devtest

extern "C" const rp_package* rp_devtest_package_device_mode(void) { return &rp::graph::devtest::kPackage; }
extern "C" const rp_package* rp_devtest_fault_package_device_mode(void) { return &rp::graph::devtest::kFaultPackage; }
extern "C" const rp_package* rp_devtest_plan_package_device_mode(void) { return &rp::graph::devtest::kPlanPackage; }
extern "C" int rp_devtest_plan_live_device_mode(void) { return rp::graph::devtest::gLive; }

#pragma once

// Installing a test's device links as a host does (rp.host.devices/2): a cart link under "n8" as an rp::n8::CartLink, a
// surface link under "launchpad" as an rp::launchpad::SurfaceLink, with the fake's own acquire() and release() as the
// hooks when it counts them (rp::host::linkHooks). On an Engine (Engine::installLink) or on a rig's own registry, each
// install required to succeed; and taking them back (Engine::clearLink).

#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "host/devices/DeviceRegistry.hpp"
#include "host/engine/Engine.hpp"
#include "launchpad/SurfaceLink.hpp"
#include "n8/CartLink.hpp"

namespace rp::host::test {

// The widest push a test's fake cart records, in a slot of its own: a cart message has no bound of the link's, and the
// widest a case pushes is a SysEx far below this.
inline constexpr std::size_t kCartPushBytes = 560;

template <class Link>
void installCart(Engine& e, Link& link) {
    REQUIRE(e.installLink<rp::n8::CartLink>(rp::n8::kDeviceName, rp::n8::kCartLinkInterface, link));
}
template <class Link>
void installSurface(Engine& e, Link& link) {
    REQUIRE(e.installLink<rp::launchpad::SurfaceLink>(rp::launchpad::kDeviceName,
                                                      rp::launchpad::kSurfaceLinkInterface, link));
}
template <class Link>
void clearCart(Engine& e, Link& link) {
    e.clearLink<rp::n8::CartLink>(link);
}
template <class Link>
void clearSurface(Engine& e, Link& link) {
    e.clearLink<rp::launchpad::SurfaceLink>(link);
}

// The same on a registry a rig keeps itself (NodeRig, ProtocolNodes.test.cpp's), with the hooks Engine::installLink
// would give.
template <class Interface, class Link>
void installOn(DeviceRegistry& registry, const char* name, const char* interfaceId, Link& link) {
    const LinkHooks hooks = linkHooks<Interface, Link>();
    REQUIRE(registry.install(name, interfaceId, static_cast<Interface*>(&link), hooks.acquire, hooks.release));
}

}  // namespace rp::host::test

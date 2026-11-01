#pragma once

// The device links an engine's host installed, by the name a node asks for (rp.host.devices/1). An engine holds what
// its host gave it; a node holds a handle for the life of the plan it runs in. [control]: a host installs before the
// first graph, a node acquires in its activate and releases in its destroy, and both of those run on the control
// thread (a plan's reclaim included). A reactivate runs deactivate and activate again on one instance
// (GraphHost::reactivate, for a sample-rate change or a block growth), so a node that already holds a handle keeps it
// and does not acquire a second: the registry forwards each acquire a node makes, and the balance is the node's to
// keep. The links outlive every plan whose nodes hold a handle, so a node may use one in its destroy.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "host/devices/DeviceLinks.hpp"

namespace rp::host {

class DeviceRegistry {
public:
    // [control] The links this host owns. Null installs nothing: that name then answers with a handle that is never
    // connected, as does every name this host has no link for, so a node needs no null check and a document plans the
    // same wherever it runs (design section 3).
    void install(CartLink* cart, SurfaceLink* surface) {
        cart_    = cart;
        surface_ = surface;
    }
    // [control] The host taking its links back, before the objects they are members of go. A node of a plan that is
    // still alive must have been destroyed already (Engine::clearGraph), or its release reaches nothing: the identity
    // check in release keeps that harmless, but a host tears its plans down first so a node may use its handle there.
    void clear() { install(nullptr, nullptr); }

    // [control] A handle for the device `name`, never null. `name` is a node's own string, so a C node's word for the
    // device has to be a link this host installed to get it.
    CartLink* acquireCart(std::string_view name) {
        CartLink* link = cart_ != nullptr && name == kCartDeviceName ? cart_ : &noneCart_;
        link->acquire();
        return link;
    }
    SurfaceLink* acquireSurface(std::string_view name) {
        SurfaceLink* link = surface_ != nullptr && name == kSurfaceDeviceName ? surface_ : &noneSurface_;
        link->acquire();
        return link;
    }
    // [control] A node is done with a handle it acquired here. The link is told only while it is still the installed
    // one: a host clears its links before the objects they wrap go, and the plans the engine frees afterwards hand
    // their handles back to a registry that no longer names them, so a release never reaches a freed link.
    void release(CartLink* link) {
        if (link != nullptr && link == cart_) link->release();
    }
    void release(SurfaceLink* link) {
        if (link != nullptr && link == surface_) link->release();
    }

private:
    // What a host that owns no cart answers with: never connected, taking every push nowhere and counting it (a node
    // reads that count as its device's drops), and an expansion volume it has nowhere to send.
    class NoCart final : public CartLink {
    public:
        void push(std::uint32_t, const std::uint8_t*, std::size_t size, bool, double) override {
            if (size != 0) dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        bool          connected() const override { return false; }
        std::uint64_t dropped() const override { return dropped_.load(std::memory_order_relaxed); }
        void          setExpansionVolume(int) override {}

    private:
        std::atomic<std::uint64_t> dropped_{0};
    };

    // And the same for a surface: nothing queued, nothing received.
    class NoSurface final : public SurfaceLink {
    public:
        std::size_t drainInput(DeviceMessage*, std::size_t) override { return 0; }
        void        pushOutput(const std::uint8_t*, std::size_t) override {}
        bool        connected() const override { return false; }
    };

    CartLink*    cart_    = nullptr;
    SurfaceLink* surface_ = nullptr;
    NoCart       noneCart_;
    NoSurface    noneSurface_;
};

}  // namespace rp::host

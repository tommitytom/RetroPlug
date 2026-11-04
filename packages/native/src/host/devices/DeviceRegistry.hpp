#pragma once

// The device links an engine's host installed, by name and interface id (rp.host.devices/2, graph/rp_device.h). An
// engine holds what its host gave it; a node holds a link for the life of the plan it runs in. [control]: a host
// installs before the first graph, a node acquires in its activate and releases in its destroy, and both of those run
// on the control thread (a plan's reclaim included). A reactivate runs deactivate and activate again on one instance
// (GraphHost::reactivate, for a sample-rate change or a block growth), so a node that already holds a link keeps it and
// does not acquire a second: the registry forwards each acquire a node makes, and the balance is the node's to keep.
// The links outlive every plan whose nodes hold one, so a node may use one in its destroy (graph/rp_device.h says the
// rule once: a host frees a link only after every plan that could hold it is destroyed).
//
// The engine knows no link's interface: a link is the pointer a host installed, handed to the node that names it, and
// the interface between the two is the device's own (rp::n8::CartLink, rp::launchpad::SurfaceLink).

#include <cstddef>
#include <cstring>

#include "graph/rp_device.h"

namespace rp::host {

class DeviceRegistry {
public:
    // The links one host installs. A fixed table, so an acquire is a scan that allocates nothing and the registry needs
    // nothing but the control-thread rule: a host owns a handful of devices (the standalone two, a cart and a surface),
    // and eight leaves room for a pack or two more without the table ever being what a host runs out of first.
    static constexpr std::size_t kMaxLinks = 8;
    // The longest name and interface id an entry keeps (the NUL not included): copied in, so a host's strings need not
    // outlive the install. The bounds graph/rp_device.h tells a pack.
    static constexpr std::size_t kMaxName = RP_DEVICE_NAME_MAX;
    static constexpr std::size_t kMaxInterfaceId = RP_DEVICE_INTERFACE_ID_MAX;
    // What the registry forwards a node's acquire and release to: the link's own (a cart link counts the nodes holding
    // it, to put the device back to its own state at the last release). Null forwards nothing.
    using Hook = void (*)(void* link);

    DeviceRegistry() : api_{this, &acquireFn, &releaseFn} {}
    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    // [control] Install `link` under `name` with the interface `interfaceId`. False, installing nothing, for a null
    // link, a name or id empty or past its bound, a link or a name and id already installed, and a full table: a host
    // installs each of its links once and clears it before it installs another in its place.
    [[nodiscard]] bool install(const char* name, const char* interfaceId, void* link, Hook acquire = nullptr,
                               Hook release = nullptr) {
        if (link == nullptr || !fits(name, kMaxName) || !fits(interfaceId, kMaxInterfaceId)) return false;
        Entry* vacant = nullptr;
        for (Entry& e : entries_) {
            if (e.link == nullptr) {
                if (vacant == nullptr) vacant = &e;
                continue;
            }
            if (e.link == link || (std::strcmp(e.name, name) == 0 && std::strcmp(e.interfaceId, interfaceId) == 0))
                return false;
        }
        if (vacant == nullptr) return false;
        // memcpy rather than strcpy, which MSVC's /W4 /WX refuses: the lengths are known and bounded above.
        std::memcpy(vacant->name, name, std::strlen(name) + 1);
        std::memcpy(vacant->interfaceId, interfaceId, std::strlen(interfaceId) + 1);
        vacant->link = link;
        vacant->acquire = acquire;
        vacant->release = release;
        return true;
    }
    // [control] The host taking `link` back: a lookup after this finds nothing, and a release reaches nothing. It frees
    // nothing and makes freeing no safer: a node of a plan still alive holds the link and uses it in its destroy, so a
    // host tears its plans down first (Engine::clearGraph) and frees the link after the clear. Whether `link` was
    // installed: false for one that is not (a null pointer, one already cleared, or one installed as another interface
    // than the caller computed, which leaves the entry installed).
    bool clear(void* link) {
        if (link == nullptr) return false;
        for (Entry& e : entries_)
            if (e.link == link) {
                e = Entry{};
                return true;
            }
        return false;
    }

    // [control] The link installed under `name` with `interfaceId`, its acquire hook told, or null: a name this host
    // has no link for, or a link of another interface. `name` and the id are a node's own strings, so a C node's word
    // for the device has to be one this host installed to get it.
    void* acquire(const char* name, const char* interfaceId) {
        if (name == nullptr || interfaceId == nullptr) return nullptr;
        for (Entry& e : entries_) {
            if (e.link == nullptr || std::strcmp(e.name, name) != 0 || std::strcmp(e.interfaceId, interfaceId) != 0)
                continue;
            if (e.acquire) e.acquire(e.link);
            return e.link;
        }
        return nullptr;
    }
    // [control] A node is done with a link it acquired here. The link is told only while it is still installed: the
    // identity check guards a release that arrives out of order, after the host cleared the link, and is no licence to
    // free a link a plan still holds.
    void release(void* link) {
        if (link == nullptr) return;
        for (Entry& e : entries_)
            if (e.link == link) {
                if (e.release) e.release(e.link);
                return;
            }
    }

    // The links installed now.
    std::size_t size() const {
        std::size_t n = 0;
        for (const Entry& e : entries_) n += e.link != nullptr ? 1 : 0;
        return n;
    }

    // What a node reads the registry through, rp.host.devices/2.
    const rp_host_devices* api() const { return &api_; }

private:
    struct Entry {
        char  name[kMaxName + 1] = {};
        char  interfaceId[kMaxInterfaceId + 1] = {};
        void* link = nullptr;  // null: the entry is free
        Hook  acquire = nullptr;
        Hook  release = nullptr;
    };

    static bool fits(const char* s, std::size_t max) {
        if (s == nullptr || s[0] == '\0') return false;
        std::size_t n = 0;
        while (s[n] != '\0')
            if (++n > max) return false;
        return true;
    }
    static void* acquireFn(void* ctx, const char* name, const char* interfaceId) {
        return static_cast<DeviceRegistry*>(ctx)->acquire(name, interfaceId);
    }
    static void releaseFn(void* ctx, void* link) { static_cast<DeviceRegistry*>(ctx)->release(link); }

    Entry           entries_[kMaxLinks];
    rp_host_devices api_;
};

// The hooks a C++ link implementing `Interface` is installed with: its own acquire() and release(), reached through the
// `Interface*` it was installed as, each null for a link that has no such member. What Engine::installLink installs a
// C++ link with, and a test's rig the same.
struct LinkHooks {
    DeviceRegistry::Hook acquire = nullptr;
    DeviceRegistry::Hook release = nullptr;
};
template <class Interface, class Link>
LinkHooks linkHooks() {
    LinkHooks hooks;
    if constexpr (requires(Link& l) { l.acquire(); })
        hooks.acquire = [](void* p) { static_cast<Link*>(static_cast<Interface*>(p))->acquire(); };
    if constexpr (requires(Link& l) { l.release(); })
        hooks.release = [](void* p) { static_cast<Link*>(static_cast<Interface*>(p))->release(); };
    return hooks;
}

}  // namespace rp::host

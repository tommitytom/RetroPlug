#pragma once

// A rig of named MIDI ports for the transport's tests (ported from feature/launchpad's Controller.test.cpp): a fake
// backend records everything written and hands back each input's receiver, so the whole lifecycle - claim, both rings,
// the close bytes, the loose ports a scan uses - runs with no rtmidi, no MIDI system and nothing plugged in. What it
// saw is kept OUTSIDE the ports, so it survives their destruction: the close bytes are written moments before the
// output is dropped, and a test reads them afterwards.

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "graph/rp_provider.h"
#include "launchpad/desktop/MidiPort.hpp"

namespace rp::launchpad::test {

using Bytes = std::vector<std::uint8_t>;

struct Rig {
    std::map<std::string, std::vector<Bytes>>  written;    // output name -> what went out, in order
    std::map<std::string, IMidiPort::Receiver> listening;  // currently-open inputs
    std::set<std::string>                      refuse;     // ports that will not open
    std::vector<std::string>                   events;     // "open in:X" / "close out:Y", in order

    /** Play the device: deliver `msg` on input `name`. */
    void deliver(const std::string& name, const Bytes& msg) {
        auto it = listening.find(name);
        if (it != listening.end() && it->second) it->second(msg.data(), msg.size());
    }
};

struct FakePort final : IMidiPort {
    FakePort(Rig& rig, bool input, std::string name, Receiver r) : rig_(rig), input_(input), name_(std::move(name)) {
        rig_.events.push_back(std::string(input_ ? "open in:" : "open out:") + name_);
        if (input_) rig_.listening[name_] = std::move(r);
    }
    ~FakePort() override {
        rig_.events.push_back(std::string(input_ ? "close in:" : "close out:") + name_);
        if (input_) rig_.listening.erase(name_);
    }
    void send(const std::uint8_t* data, std::size_t n) override {
        if (!input_) rig_.written[name_].emplace_back(data, data + n);
    }
    Rig&        rig_;
    bool        input_;
    std::string name_;
};

inline MidiPortOpener openerFor(Rig& rig) {
    return [&rig](bool input, const std::string& name, IMidiPort::Receiver r) -> std::unique_ptr<IMidiPort> {
        if (rig.refuse.count(name)) throw std::runtime_error("MIDI port not found: " + name);
        return std::make_unique<FakePort>(rig, input, name, std::move(r));
    };
}

// A Launchpad's exitToLiveMode, and a second device's: the bytes a host must replay or strand the user's hardware.
inline const Bytes kCloseBytes{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x0E, 0x00, 0xF7};
inline const Bytes kOtherClose{0xF0, 0x01, 0xF7};

// A fake rp_device_host: install and clear record what a provider installed, reserve_midi and suspend_hardware_inputs
// are recorded in order, and restored counts the facet's rendezvous end (the product's own calls,
// docs/modular-graph-phase-f3-design.md section 3). Nothing throws.
struct FakeHost {
    rp_device_host table{};
    std::string    configDir;
    void*          link  = nullptr;
    std::string    name, interfaceId;
    int            installs = 0;
    int            clears = 0;
    std::vector<std::string> reserved;    // "in|out", in order
    std::vector<bool>        suspended;   // the scan's held edge
    int                      restoredCalls = 0;  // the control plane said it had restored its selection

    explicit FakeHost(std::string dir = "") : configDir(std::move(dir)) {
        table = rp_device_host{RP_PROVIDER_ABI_VERSION, this, configDir.c_str(), &installOn, &clearOn, &reserveOn,
                               &suspendOn, &restoredOn};
    }
    static bool installOn(void* ctx, const char* n, const char* id, void* l, void (*)(void*), void (*)(void*)) {
        auto* self   = static_cast<FakeHost*>(ctx);
        self->link   = l;
        self->name   = n ? n : "";
        self->interfaceId = id ? id : "";
        self->installs++;
        return true;
    }
    static void clearOn(void* ctx, void* l) {
        auto* self = static_cast<FakeHost*>(ctx);
        if (self->link == l) self->link = nullptr;
        self->clears++;
    }
    static bool reserveOn(void* ctx, const char* in, const char* out) {
        static_cast<FakeHost*>(ctx)->reserved.push_back(std::string(in ? in : "") + "|" + std::string(out ? out : ""));
        return true;
    }
    static void suspendOn(void* ctx, bool suspended) { static_cast<FakeHost*>(ctx)->suspended.push_back(suspended); }
    static void restoredOn(void* ctx) { static_cast<FakeHost*>(ctx)->restoredCalls++; }
};

}  // namespace rp::launchpad::test

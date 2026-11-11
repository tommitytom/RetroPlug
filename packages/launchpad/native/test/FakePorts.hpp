#pragma once

// A rig of named MIDI ports for the transport's tests (ported from feature/launchpad's Controller.test.cpp): the midi
// package's fake backend (midi/FakeMidiBackend.hpp) plus what is particular to a CONTROL SURFACE - a device that answers
// the Universal Device Inquiry and its layout query. It records everything written and hands back each input's delivery
// path, so the whole lifecycle - claim, both rings, the close bytes, the loose ports a scan uses - runs with no rtmidi,
// no MIDI system and nothing plugged in. What it saw is kept OUTSIDE the ports, so it survives their destruction: the
// close bytes are written moments before the output is dropped, and a test reads them afterwards.
//
// This is the pack's test support and the headless host's scripted device (packages/native/src/main.cpp): the shape
// retroplug-launchpad-harness has, a header a host, and only a host, compiles. The device model is an `onSend` hook over
// the fake's outputs, so a second surface is a change here and in TS, never in the fake.

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "graph/rp_provider.h"
#include "launchpad/desktop/LaunchpadRpc.hpp"  // PortLister / LaunchpadPortsDto
#include "midi/FakeMidiBackend.hpp"

namespace rp::launchpad::test {

using Bytes = std::vector<std::uint8_t>;
using rp::midi::FakeMidiBackend;

// A Pro MK3's answers, as tools/fixtures/fake-launchpad.c plays them on the ALSA sequencer: the Universal Device Inquiry
// (what a scan sends) answered with a Novation reply carrying the Pro's family bytes, and the "which layout?" query
// answered in the Pro's own header (which is what tells it from the Mini MK3, whose inquiry reply is the same).
inline const Bytes kInquiry{0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
inline const Bytes kInquiryReply{0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0x00, 0x00,
                                 0x00, 0x01, 0x02, 0x03, 0xF7};
inline const Bytes kLayoutQuery{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0xF7};
inline const Bytes kLayoutReply{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0x00, 0x00, 0xF7};

/** The fake's machine, plus the answers a Launchpad gives: an output that is a DEVICE answers the inquiry and the layout
 *  query on the input its device listens on, and any other listed output is an interface's, which answers nothing. */
struct Rig : FakeMidiBackend {
    // The outputs that are a DEVICE (they answer an inquiry); any other listed output is an interface's, and answers
    // nothing - the rig a TRS-attached Launchpad is on, where a scan has to keep looking.
    std::set<std::string> answering;
    // The input a listed OUTPUT's device answers on, when the two names differ (a device that names its in and out
    // differently). Absent means the same name, which is the ALSA fake's shape.
    std::map<std::string, std::string> answersOn;

    Rig() {
        onSend = [this](FakeMidiBackend&, const std::string& name, const std::uint8_t* data, std::size_t n) {
            if (answering.count(name) == 0) return;  // an interface, not a device
            const Bytes  query(data, data + n);
            const Bytes* reply = query == kInquiry ? &kInquiryReply : query == kLayoutQuery ? &kLayoutReply : nullptr;
            if (!reply) return;
            const auto at = answersOn.find(name);
            deliver(at != answersOn.end() ? at->second : name, *reply);
        };
    }
};

/** The port list a product's lister answers from `rig` (LaunchpadRpc's PortLister). */
inline PortLister listerFor(Rig& rig) {
    return [&rig] { return LaunchpadPortsDto{rig.inputs, rig.outputs}; };
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
    void*          link = nullptr;
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

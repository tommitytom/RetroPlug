#pragma once

// A rig of named MIDI ports for the transport's tests (ported from feature/launchpad's Controller.test.cpp): a fake
// backend records everything written and hands back each input's receiver, so the whole lifecycle - claim, both rings,
// the close bytes, the loose ports a scan uses - runs with no rtmidi, no MIDI system and nothing plugged in. What it
// saw is kept OUTSIDE the ports, so it survives their destruction: the close bytes are written moments before the
// output is dropped, and a test reads them afterwards.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "graph/rp_provider.h"
#include "launchpad/desktop/LaunchpadRpc.hpp"  // PortLister / LaunchpadPortsDto
#include "launchpad/desktop/MidiPort.hpp"

namespace rp::launchpad::test {

using Bytes = std::vector<std::uint8_t>;

struct Rig {
    std::map<std::string, std::vector<Bytes>>  written;    // output name -> what went out, in order
    std::map<std::string, IMidiPort::Receiver> listening;  // currently-open inputs
    std::set<std::string>                      refuse;     // ports that will not open
    std::vector<std::string>                   events;     // "open in:X" / "close out:Y", in order
    // What the product's LISTER reports: the machine's ports, as a scan and a claim see them. A test (or the headless
    // host) sets these to play a device being plugged in, swapped for another or unplugged; opening a port no list names
    // still works (the opener has no list), so a claim of a vanished port is the lister's refusal, as on a real machine.
    std::vector<std::string>                   inputs, outputs;
    // The outputs that are a DEVICE (they answer an inquiry); any other listed output is an interface's, and answers
    // nothing - the rig a TRS-attached Launchpad is on, where a scan has to keep looking.
    std::set<std::string>                      answering;
    // The input a listed OUTPUT's device answers on, when the two names differ (a device that names its in and out
    // differently). Absent means the same name, which is the ALSA fake's shape.
    std::map<std::string, std::string>         answersOn;
    // How long a write to an OUTPUT blocks in the port, in milliseconds: a WinMM bulk SysEx waits for the bytes to leave
    // the wire (~170 ms), which is why output leaves on the writer thread. 0 for every ordinary case.
    int                                        sendDelayMs = 0;
    // Writes in flight, so a case can wait until the writer is INSIDE the port before it times a send() against it.
    std::atomic<int>                           sendsStarted{0};
    std::atomic<int>                           sendsFinished{0};

    /** Play the device: deliver `msg` on input `name`. */
    void deliver(const std::string& name, const Bytes& msg) {
        auto it = listening.find(name);
        if (it != listening.end() && it->second) it->second(msg.data(), msg.size());
    }
};

struct FakePort : IMidiPort {
    FakePort(Rig& rig, bool input, std::string name, Receiver r) : rig_(rig), input_(input), name_(std::move(name)) {
        rig_.events.push_back(std::string(input_ ? "open in:" : "open out:") + name_);
        if (input_) rig_.listening[name_] = std::move(r);
    }
    ~FakePort() override {
        rig_.events.push_back(std::string(input_ ? "close in:" : "close out:") + name_);
        if (input_) rig_.listening.erase(name_);
    }
    void send(const std::uint8_t* data, std::size_t n) override {
        if (input_) return;
        rig_.sendsStarted.fetch_add(1, std::memory_order_relaxed);
        if (rig_.sendDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(rig_.sendDelayMs));
        rig_.written[name_].emplace_back(data, data + n);
        rig_.sendsFinished.fetch_add(1, std::memory_order_relaxed);
    }
    Rig&        rig_;
    bool        input_;
    std::string name_;
};

/** The port list a product's lister answers from `rig` (LaunchpadRpc's PortLister). */
inline PortLister listerFor(Rig& rig) {
    return [&rig] { return LaunchpadPortsDto{rig.inputs, rig.outputs}; };
}

// A Pro MK3's answers, as tools/fixtures/fake-launchpad.c plays them on the ALSA sequencer: the Universal Device Inquiry
// (what a scan sends) answered with a Novation reply carrying the Pro's family bytes, and the "which layout?" query
// answered in the Pro's own header (which is what tells it from the Mini MK3, whose inquiry reply is the same).
inline const Bytes kInquiry{0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
inline const Bytes kInquiryReply{0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x13, 0x01, 0x00, 0x00,
                                 0x00, 0x01, 0x02, 0x03, 0xF7};
inline const Bytes kLayoutQuery{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0xF7};
inline const Bytes kLayoutReply{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0E, 0x00, 0x00, 0x00, 0xF7};

/** A fake Pro MK3: FakePort that also ANSWERS, as the device does, on ITS OWN input (`Rig::answersOn`, or the input of
 *  the same name). A scan listens on every input and credits the pair the answer arrived on, so answering on whichever
 *  input happened to open first would credit a pair the device is not on - a rig whose other port sorts first would pass
 *  a scan that a real machine fails. */
struct ScriptedPort final : FakePort {
    using FakePort::FakePort;
    void send(const std::uint8_t* data, std::size_t n) override {
        FakePort::send(data, n);
        if (rig_.answering.count(name_) == 0) return;  // an interface, not a device
        const Bytes query(data, data + n);
        const Bytes* reply = query == kInquiry ? &kInquiryReply : query == kLayoutQuery ? &kLayoutReply : nullptr;
        if (!reply) return;
        IMidiPort::Receiver hear;
        const auto          at = rig_.answersOn.find(name_);
        const auto          it = rig_.listening.find(at != rig_.answersOn.end() ? at->second : name_);
        if (it != rig_.listening.end()) hear = it->second;
        if (hear) hear(reply->data(), reply->size());
    }
};

inline MidiPortOpener openerFor(Rig& rig) {
    return [&rig](bool input, const std::string& name, IMidiPort::Receiver r) -> std::unique_ptr<IMidiPort> {
        if (rig.refuse.count(name)) throw std::runtime_error("MIDI port not found: " + name);
        return std::make_unique<FakePort>(rig, input, name, std::move(r));
    };
}

/** openerFor for a rig whose OUTPUTS are a Pro MK3: every output answers the inquiry and the layout query (see
 *  ScriptedPort). The headless host's facet serves this, so a test drives the same facet the standalone does over a
 *  scripted device instead of rtmidi's ports. */
inline MidiPortOpener scriptedOpenerFor(Rig& rig) {
    return [&rig](bool input, const std::string& name, IMidiPort::Receiver r) -> std::unique_ptr<IMidiPort> {
        if (rig.refuse.count(name)) throw std::runtime_error("MIDI port not found: " + name);
        if (input) return std::make_unique<FakePort>(rig, true, name, std::move(r));
        return std::make_unique<ScriptedPort>(rig, false, name, std::move(r));
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

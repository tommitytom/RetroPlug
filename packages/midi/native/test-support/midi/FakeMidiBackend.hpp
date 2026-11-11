#pragma once

// A MIDI system a test scripts, filling the same interface RtMidi does (docs/modular-graph-phase-f5b-design.md section
// 4.6). It is what the Launchpad pack's tests and the headless host's scripted Pro MK3 are built on, so it carries what
// a device model needs and not only a recorder: the ports a machine lists, the ones that refuse, an event log, what
// every output was sent (kept outside the port, so it survives the port's destruction), the receiver of every open
// input, a deliver() that plays one, a send hook a scripted device answers through, a per-send delay with the counters
// a writer-thread case waits on, and a delivery mode: inline on the caller's thread, or on a thread of its own, which
// is what the stream's cases and the ThreadSanitizer leg need.
//
// Header-only test support under native/test-support, an include root the test binaries and the headless host add and
// no product does - the shape the n8 pack's FakeN8.hpp has, "a header a host, and only a host, compiles". It is not
// final: the Launchpad pack's rig derives from it to add the one thing a control surface does that a plain recorder
// does not (answer a Device Inquiry), through the `onSend` hook.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "midi/MidiBackend.hpp"

namespace rp::midi {

class FakeMidiBackend;

/** What one port did, kept OUTSIDE the port object: the close bytes are written moments before an output is dropped,
 *  and a test reads them afterwards. A send that reaches a record whose port the stream has closed is a send the stream
 *  let go of too early - the output-set swap's failure, counted rather than only crashing. */
struct FakePortRecord {
    std::string        name;
    bool               input  = false;
    bool               closed = false;
    std::vector<std::vector<std::uint8_t>> received;  // what an output was sent, in order
    std::size_t        sendsAfterClose = 0;
};

/** One port of the fake. `send` copies its record FIRST, so a write that began before a swap counts itself against a
 *  record the fake still owns even if the port object is gone - which is what a swap that freed the port under a send
 *  does, and what the case that must fail without the epoch handshake measures. */
class FakePort final : public MidiPort {
public:
    FakePort(FakeMidiBackend& backend, std::shared_ptr<FakePortRecord> rec, Receiver receiver);
    ~FakePort() override;
    void send(const std::uint8_t* data, std::size_t n) override;
    /** Play one message into this input: on the caller's thread, or queued to this port's own when the backend asked
     *  for threaded delivery. */
    void deliver(const std::uint8_t* data, std::size_t n);

private:
    void drain();

    FakeMidiBackend&                      backend_;
    std::shared_ptr<FakePortRecord>       record_;
    Receiver                              receiver_;
    std::mutex                            queueMutex_;
    std::condition_variable               queueWake_;
    std::deque<std::vector<std::uint8_t>> queue_;
    bool                                  stop_ = false;
    // LAST: joined by ~FakePort() before any member it reads is destroyed.
    std::thread                           worker_;
};

class FakeMidiBackend : public MidiBackend {
public:
    using Bytes = std::vector<std::uint8_t>;

    // --- what the machine has, and what it did ------------------------------------------------------------------

    // The ports the machine LISTS. Independent of what opens: opening a port no list names still works, so a claim of a
    // vanished port is the lister's refusal, as on a real machine.
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::set<std::string>    refuse;   // ports whose open throws
    std::vector<std::string> events;   // "open in:X", "close out:Y", "virtual in:C" / "virtual out:C", in order
    std::map<std::string, std::vector<Bytes>> written;    // output name -> what went out, in order
    std::map<std::string, Receiver>           listening;  // currently-open inputs
    std::vector<std::string>                  listed;     // "in" / "out" per listing call
    // Whether an output is EXCLUSIVE, as a WinMM one is: opening a name the stream still holds throws. It is the
    // failure a re-pick has to avoid by letting the port go first, and it is off for every ordinary case. Inputs are
    // exclusive on Windows too, so the flag covers both directions.
    bool exclusivePorts = false;
    // A send hook, called by every output port once the bytes are recorded: a scripted device answers an inquiry
    // through it (the Launchpad pack's Pro MK3), and a case can watch a write arrive.
    std::function<void(FakeMidiBackend&, const std::string&, const std::uint8_t*, std::size_t)> onSend;
    // How long a write to an OUTPUT blocks in the port, in milliseconds: a WinMM bulk SysEx waits for the bytes to
    // leave the wire (~170 ms), which is why the Launchpad's output leaves on a writer thread. 0 for every ordinary
    // case.
    int sendDelayMs = 0;
    // Writes in flight, so a case can wait until the writer is INSIDE the port before it times a send() against it.
    std::atomic<int> sendsStarted{0};
    std::atomic<int> sendsFinished{0};

    // --- the fake's own answers ---------------------------------------------------------------------------------

    // The name this system opens its ports under, as a real backend's own client name is (the stream filters by it).
    std::string clientNameValue = "RetroPlug";
    bool usableFlag    = true;   // false: the system refuses to be used at all
    bool virtualPorts  = true;   // false: the Windows shape, no virtual ports
    bool threadedDelivery = false;  // true: an input's receiver runs on the port's own thread, as RtMidi's does

    std::string              id() const override { return "fake"; }
    std::string              clientName() const override { return clientNameValue; }
    /** A second fake listing the same machine under another client name. Its records and ports are its own (a renamed
     *  stream opens its own ports), and its HOOKS are its own too: `onSend` is not carried over, so a session that
     *  renamed its backend talks to no scripted device - a case that drives a device model (the Launchpad's Pro MK3)
     *  keeps driving the instance it made, and a renamed one answers nothing. */
    std::unique_ptr<MidiBackend> withClientName(std::string clientName) override {
        auto next           = std::make_unique<FakeMidiBackend>();
        next->inputs        = inputs;
        next->outputs       = outputs;
        next->refuse        = refuse;
        next->clientNameValue = std::move(clientName);
        next->usableFlag      = usableFlag;
        next->virtualPorts    = virtualPorts;
        next->exclusivePorts  = exclusivePorts;
        return next;
    }
    bool                     usable() override { return usableFlag; }
    std::vector<std::string> listInputs() override {
        listed.push_back("in");
        return inputs;
    }
    std::vector<std::string> listOutputs() override {
        listed.push_back("out");
        return outputs;
    }
    bool supportsVirtual() const override { return virtualPorts; }

    std::unique_ptr<MidiPort> openInput(const std::string& name, Receiver receiver) override {
        return open(true, name, std::move(receiver));
    }
    std::unique_ptr<MidiPort> openOutput(const std::string& name) override { return open(false, name, nullptr); }

    std::unique_ptr<MidiPort> openVirtualInput(Receiver receiver) override {
        return openVirtual(true, clientNameValue, std::move(receiver));
    }
    std::unique_ptr<MidiPort> openVirtualOutput() override { return openVirtual(false, clientNameValue, nullptr); }

    /** Play the device: deliver `msg` on input `name`, as that port's callback thread would. Called under the fake's
     *  own lock, so an input the control thread closes at that moment cannot be destroyed between the lookup and the
     *  call (the port is alive until its own destructor takes the same lock, which waits for this delivery). An INLINE
     *  receiver therefore runs under that lock: a receiver that sends synchronously on the same fake (a thru) would
     *  deadlock on its non-recursive mutex, so a case that needs one delivers through a threaded port
     *  (`threadedDelivery`, which is also the shape RtMidi has). */
    void deliver(const std::string& name, const Bytes& msg) {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto                  it = deliverers_.find(name);
        if (it != deliverers_.end()) it->second->deliver(msg.data(), msg.size());
    }

    /** Outputs sent to `name` so far (the record survives the port, so a close's goodbye is readable afterwards). */
    std::vector<Bytes> sentTo(const std::string& name) const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<Bytes>          out;
        for (const auto& r : records_)
            if (!r->input && r->name == name) out.insert(out.end(), r->received.begin(), r->received.end());
        return out;
    }
    /** Every send any output recorded, across every port that ever opened, so a lost send shows as a short count. */
    std::size_t totalReceived() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t                 n = 0;
        for (const auto& r : records_)
            if (!r->input) n += r->received.size();
        return n;
    }
    /** Sends that reached a port after the stream closed it: zero, whichever thread swapped the selection. */
    std::size_t sendsAfterClose() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::size_t                 n = 0;
        for (const auto& r : records_) n += r->sendsAfterClose;
        return n;
    }
    /** Records made so far (one per open port, virtual included): how a case waits for a scan or a swap to land. */
    std::size_t records() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return records_.size();
    }

    // --- the ports' own calls (public so the header-only ports can make them) -------------------------------------

    void pushEvent(const std::string& event) {
        std::lock_guard<std::mutex> lk(mutex_);
        events.push_back(event);
    }

    /** One output's send: recorded against its record, then handed to the hook. */
    void sendThrough(const std::shared_ptr<FakePortRecord>& rec, const std::uint8_t* data, std::size_t n) {
        if (!data || n == 0) return;
        sendsStarted.fetch_add(1, std::memory_order_relaxed);
        if (sendDelayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sendDelayMs));
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (rec->input) {
                // an input's send is a no-op
            } else if (rec->closed) {
                rec->sendsAfterClose++;  // the port this write belongs to was let go while the write was in flight
            } else {
                rec->received.emplace_back(data, data + n);
                written[rec->name].emplace_back(data, data + n);
            }
        }
        if (onSend) onSend(*this, rec->name, data, n);
        sendsFinished.fetch_add(1, std::memory_order_relaxed);
    }

    /** A port went away: say so once, and stop routing deliveries to it. The machine's open-input map is written under
     *  the same lock registerInput writes it under, so a test reading it while a port closes sees either state, not
     *  half of one. */
    void closePort(const std::shared_ptr<FakePortRecord>& rec) {
        std::lock_guard<std::mutex> lk(mutex_);
        rec->closed = true;
        deliverers_.erase(rec->name);
        if (rec->input) listening.erase(rec->name);
        events.push_back(std::string("close ") + (rec->input ? "in:" : "out:") + rec->name);
    }

private:
    std::unique_ptr<MidiPort> open(bool input, const std::string& name, Receiver receiver) {
        if (refuse.count(name) || !usableFlag) throw std::runtime_error("MIDI port not found: " + name);
        if (exclusivePorts) {
            std::lock_guard<std::mutex> lk(mutex_);
            for (const auto& r : records_)
                if (r->name == name && r->input == input && !r->closed)
                    throw std::runtime_error("MIDI port is already in use: " + name);
        }
        auto rec = std::make_shared<FakePortRecord>();
        rec->name  = name;
        rec->input = input;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            records_.push_back(rec);
        }
        pushEvent(std::string("open ") + (input ? "in:" : "out:") + name);
        auto port = std::make_unique<FakePort>(*this, rec, std::move(receiver));
        if (input) registerInput(name, *port, /*machinePort=*/true);
        return port;
    }

    std::unique_ptr<MidiPort> openVirtual(bool input, const std::string& client, Receiver receiver) {
        if (!virtualPorts || !usableFlag) throw std::runtime_error("virtual ports are not supported");
        // A virtual port is a port with no machine name behind it: "<client> In" / "<client> Out", as RtMidi publishes
        // it, recorded like any other so a case sees what the stream opened and logged.
        const std::string name = client + (input ? " In" : " Out");
        pushEvent(std::string("virtual ") + (input ? "in:" : "out:") + client);
        auto rec = std::make_shared<FakePortRecord>();
        rec->name  = name;
        rec->input = input;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            records_.push_back(rec);
        }
        auto port = std::make_unique<FakePort>(*this, rec, std::move(receiver));
        if (input) registerInput(name, *port, /*machinePort=*/false);
        return port;
    }

    /** An input that opened: where a delivery goes. The receiver is the port's own delivery path, so a threaded port
     *  queues and an inline one calls on the caller's thread. `listening` is the MACHINE's open inputs (what a case
     * reads as "is it open"): a virtual port the stream publishes is not one the machine lists, so it is left out of
     * it. */
    void registerInput(const std::string& name, FakePort& port, bool machinePort);

    friend class FakePort;

    mutable std::mutex                       mutex_;
    std::vector<std::shared_ptr<FakePortRecord>> records_;
    std::map<std::string, FakePort*>         deliverers_;  // open inputs, for threaded delivery
};

// --- the port's definitions, after the backend it calls ---------------------------------------------------------

inline void FakeMidiBackend::registerInput(const std::string& name, FakePort& port, bool machinePort) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (machinePort) listening[name] = [&port](const std::uint8_t* data, std::size_t n) { port.deliver(data, n); };
    deliverers_[name] = &port;
}

inline FakePort::FakePort(FakeMidiBackend& backend, std::shared_ptr<FakePortRecord> rec, Receiver receiver)
    : backend_(backend), record_(std::move(rec)), receiver_(std::move(receiver)) {
    if (backend_.threadedDelivery) worker_ = std::thread([this] { drain(); });
}

inline FakePort::~FakePort() {
    if (worker_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(queueMutex_);
            stop_ = true;
        }
        queueWake_.notify_one();
        worker_.join();  // no receiver call is running when this returns
    }
    // Unrouted under the fake's lock, so a delivery already in flight finishes before this returns and no later one can
    // look the port up; only then does the receiver a delivery would call go away (the MidiPort contract: no receiver
    // call runs once the destructor has returned).
    backend_.closePort(record_);
    receiver_ = nullptr;
}

inline void FakePort::send(const std::uint8_t* data, std::size_t n) {
    auto rec = record_;  // a copy: alive whatever happens to this object (see the class comment)
    backend_.sendThrough(rec, data, n);
}

inline void FakePort::deliver(const std::uint8_t* data, std::size_t n) {
    if (!record_->input || !data || n == 0) return;
    if (!worker_.joinable()) {
        if (receiver_) receiver_(data, n);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        queue_.emplace_back(data, data + n);
    }
    queueWake_.notify_one();
}

inline void FakePort::drain() {
    for (;;) {
        std::vector<std::uint8_t> bytes;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            queueWake_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) return;  // stop_ and nothing left
            bytes = std::move(queue_.front());
            queue_.pop_front();
        }
        if (receiver_) receiver_(bytes.data(), bytes.size());
    }
}

}  // namespace rp::midi

#include "launchpad/desktop/ControllerLink.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <utility>

#include "launchpad/desktop/MidiPortFilter.hpp"

namespace rp::launchpad {

ControllerLink::ControllerLink(MidiPortOpener opener, bool writerThread)
    : opener_(std::move(opener)), useWriter_(writerThread) {}

ControllerLink::~ControllerLink() {
    disconnect();
}

std::string ControllerLink::lastError() const {
    return error_;
}

bool ControllerLink::connect(const std::string& inName, const std::string& outName,
                             std::vector<std::uint8_t> closeBytes) {
    disconnect();  // idempotent; also releases a previously claimed pair (with its close bytes) first
    if (!opener_) {
        error_ = "no MIDI backend";
        return false;
    }
    // The test-only allow list (MidiPortFilter.hpp). Checked before anything opens, and named as the claim's own error, so
    // a filtered process cannot take a port it was not given - whatever opener the product injected - and says which.
    if (!midiPortAllowed(outName)) {
        error_ = "MIDI port not allowed: " + outName;
        return false;
    }
    if (!midiPortAllowed(inName)) {
        error_ = "MIDI port not allowed: " + inName;
        return false;
    }
    // The claim's number is fixed BEFORE the input opens, and the receiver is bound to it, so the first message the new
    // device sends is already tagged as this claim's - not the previous one's, and not "nothing" because the number had
    // not been published yet.
    if (++nextGen_ == 0) nextGen_ = 1;  // 0 means "nothing held"
    const std::uint32_t gen = nextGen_;
    try {
        // Output first, so the input's callback can never run against a half-built link. The receiver runs on the
        // backend's callback thread; `this` outlives the port (in_ is reset before the link is destroyed, and resetting
        // it cancels the callback), so capturing it is safe.
        out_ = opener_(false, outName, nullptr);
        in_  = opener_(true, inName,
                       [this, gen](const std::uint8_t* data, std::size_t n) { pushInputRing(data, n, gen); });
    } catch (const std::exception& e) {
        error_ = e.what();
        in_.reset();
        out_.reset();
        return false;
    }
    if (!in_ || !out_) {
        error_ = "MIDI port unavailable";
        in_.reset();
        out_.reset();
        return false;
    }
    error_.clear();
    sent_.store(0, std::memory_order_relaxed);  // "since connect": a new device starts its own count
    dropped_.store(0, std::memory_order_relaxed);
    closeBytes_ = std::move(closeBytes);
    inName_     = inName;
    outName_    = outName;
    generation_.store(gen, std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    startWriter();
    return true;
}

void ControllerLink::disconnect() {
    // The control thread's path: a swap in launchpadClaim, a release, and the provider's stop all come through here. It
    // waits for the writer to finish what it is writing (a WinMM bulk SysEx blocks ~170 ms) and then writes the ring's
    // flush and the goodbye itself, so under WinMM a claim or a release can hold the caller for the queued writes, bounded
    // by the send ring's 32 slots and the control queue's 16. The goodbye cannot be left to a detached writer: a swap must
    // have the old pair's ports CLOSED before the new pair opens.
    // Stop the audio thread queueing first, so nothing is still arriving while we say goodbye.
    connected_.store(false, std::memory_order_release);
    stopWriter();  // from here this thread is the send ring's and the control queue's only consumer
    if (out_) {
        {
            std::lock_guard<std::mutex> seat(popMutex_);  // the ring's consumer, now that the writer has stopped
            std::lock_guard<std::mutex> lk(sendMutex_);
            // Flush what the last block queued, so the device is not left half-painted.
            OutMessage m;
            while (outRing_.tryPop(m)) {
                if (m.gen != generation_.load(std::memory_order_acquire) || m.len == 0) continue;
                out_->send(m.data, m.len);
                sent_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        // Then the UI's own messages, in the order they were queued behind the session's, and then the goodbye.
        writeControl();
        {
            std::lock_guard<std::mutex> lk(sendMutex_);
            if (!closeBytes_.empty()) out_->send(closeBytes_.data(), closeBytes_.size());
        }
    }
    {
        std::lock_guard<std::mutex> lk(ctlMutex_);
        control_.clear();  // nothing from this claim survives it
    }
    in_.reset();  // cancels the callback: no receiver runs after this returns
    out_.reset();
    closeBytes_.clear();
    inName_.clear();
    outName_.clear();
    // The input ring is NOT rewound here: the audio thread is its only consumer and may be mid-drain. What is left in it
    // carries this claim's number, which no later claim will share, so drainInput drops it.
}

void ControllerLink::pushInputRing(const std::uint8_t* data, std::size_t n, std::uint32_t gen) {
    if (!data || n == 0) return;
    if (n > kMaxDeviceMessage) {  // wider than the node-facing slot: dropped and counted, never truncated
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const std::size_t t    = inTail_.load(std::memory_order_relaxed);
    const std::size_t next = (t + 1) % kInCapacity;
    if (next == inHead_.load(std::memory_order_acquire)) {  // full -> drop
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    InSlot& slot = inRing_[t];
    std::memcpy(slot.msg.data, data, n);
    slot.msg.size = static_cast<std::uint16_t>(n);
    slot.gen      = gen;
    inTail_.store(next, std::memory_order_release);
}

std::size_t ControllerLink::drainInput(DeviceMessage* out, std::size_t capacity) {
    if (!out || capacity == 0) return 0;
    const std::uint32_t live = generation();
    // Nothing claimed right now: answer nothing and leave the ring alone. A connect publishes its number only after both
    // ports are open, so a drain that ran between the open and the publish would otherwise consume (and discard) the new
    // claim's first messages.
    if (live == 0) return 0;
    std::size_t         h    = inHead_.load(std::memory_order_relaxed);
    const std::size_t   t    = inTail_.load(std::memory_order_acquire);
    std::size_t         n    = 0;
    while (h != t && n < capacity) {
        InSlot& slot = inRing_[h];
        // An earlier claim's message is skipped here, in place: nothing on another thread drains this ring, so a
        // disconnect never has to rewind it (and could not safely).
        if (slot.gen == live) out[n++] = slot.msg;
        h = (h + 1) % kInCapacity;
    }
    inHead_.store(h, std::memory_order_release);
    return n;
}

void ControllerLink::pushOutput(const std::uint8_t* data, std::size_t size) {
    const std::uint32_t gen = generation();
    if (gen == 0 || !data || size == 0) {
        // A paint sent while nothing is attached went nowhere: counted, so the session repaints when a device appears.
        if (data && size != 0) dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (size > kMaxDeviceMessage) {  // longer than any message a controller app can produce - a bug upstream
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    OutMessage m;
    m.gen = gen;
    m.len = static_cast<std::uint16_t>(size);
    std::memcpy(m.data, data, size);
    if (!outRing_.tryPush(m)) dropped_.fetch_add(1, std::memory_order_relaxed);
}

void ControllerLink::writeQueued() {
    // The send ring has ONE consumer (the writer thread, or disconnect()'s flush), and this is the seat: a ring that two
    // threads popped would tear. Each write takes sendMutex_ on its own, so a write in flight does not hold the seat.
    std::lock_guard<std::mutex> seat(popMutex_);
    OutMessage                  m;
    while (outRing_.tryPop(m)) {
        if (m.len == 0) continue;
        std::lock_guard<std::mutex> lk(sendMutex_);
        // Produced for an earlier claim (queued just before a swap): not this device's to hear. Compared with the
        // claim's PUBLISHED number, not generation(), which already reads 0 while disconnect() is flushing this claim's
        // last frames.
        if (!out_ || m.gen != generation_.load(std::memory_order_acquire)) continue;
        out_->send(m.data, m.len);
        sent_.fetch_add(1, std::memory_order_relaxed);
    }
}

void ControllerLink::writeControl() {
    // The UI's messages, which send() queued. The batch is taken under ctlMutex_ and written outside it, so a UI send
    // never waits for a write: it waits for a swap at most, which is a pointer's work.
    std::vector<ControlMessage> batch;
    {
        std::lock_guard<std::mutex> lk(ctlMutex_);
        batch.swap(control_);
    }
    for (const ControlMessage& m : batch) {
        if (m.bytes.empty()) continue;
        std::lock_guard<std::mutex> lk(sendMutex_);
        if (!out_ || m.gen != generation_.load(std::memory_order_acquire)) continue;  // an earlier claim's message
        out_->send(m.bytes.data(), m.bytes.size());
        sent_.fetch_add(1, std::memory_order_relaxed);
    }
}

void ControllerLink::pump() {
    if (!useWriter_) {
        writeQueued();
        writeControl();
        return;
    }
    wakeWriter();
}

void ControllerLink::wakeWriter() {
    {
        std::lock_guard<std::mutex> lk(wakeMutex_);
        wakeFlag_ = true;
    }
    wake_.notify_one();
}

bool ControllerLink::send(const std::uint8_t* data, std::size_t n) {
    if (!isConnected() || !data || n == 0 || n > kMaxDeviceMessage) return false;
    if (!useWriter_) {
        // No writer thread: this thread is the writer (the unit tests' shape), so write in order here and say what
        // happened - a case can then assert on the device's own record without racing a thread.
        writeQueued();
        writeControl();
        std::lock_guard<std::mutex> lk(sendMutex_);
        if (!out_) return false;
        out_->send(data, n);
        sent_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    // The provider's shape: hand it to the writer and return, so the UI thread never writes the port and never waits for
    // the session's queue to drain (a WinMM bulk SysEx blocks ~170 ms) or for a write in flight.
    {
        std::lock_guard<std::mutex> lk(ctlMutex_);
        if (control_.size() >= kControlCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        control_.push_back(ControlMessage{generation(), std::vector<std::uint8_t>(data, data + n)});
    }
    wakeWriter();
    return true;
}

void ControllerLink::startWriter() {
    if (!useWriter_ || writer_.joinable()) return;
    writerRun_.store(true, std::memory_order_release);
    writer_ = std::thread([this] {
        while (writerRun_.load(std::memory_order_acquire)) {
            {
                // Woken by pump() once a frame; the timeout only bounds the wait if the frame loop stalls.
                std::unique_lock<std::mutex> lk(wakeMutex_);
                wake_.wait_for(lk, std::chrono::milliseconds(50),
                               [this] { return wakeFlag_ || !writerRun_.load(std::memory_order_acquire); });
                wakeFlag_ = false;
            }
            writeQueued();
            writeControl();
        }
    });
}

void ControllerLink::stopWriter() {
    if (!writer_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(wakeMutex_);
        writerRun_.store(false, std::memory_order_release);
    }
    wake_.notify_one();
    writer_.join();
}

}  // namespace rp::launchpad

#include "midi/PortWriter.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace rp::midi {

PortWriter::PortWriter(std::unique_ptr<MidiPort> port, std::string name, Options options, LogSink log)
    : port_(std::move(port)),
      name_(std::move(name)),
      options_(options),
      log_(std::move(log)),
      ring_(options.ringBytes < kHeader * 2 ? kHeader * 2 : options.ringBytes),
      scratch_(ring_.size()) {
    // Only a writer with a port it can send to gets a thread: a null port takes no push (push() says so) and has
    // nothing to flush, and a thread started for one would be left joinable by stop()'s early return, whose ~thread
    // terminates the process.
    if (port_ && options_.mode == OutputMode::Threaded) worker_ = std::thread([this] { run(); });
}

PortWriter::~PortWriter() { stop(); }

bool PortWriter::push(const std::uint8_t* data, std::size_t n) {
    if (!port_ || !data || n == 0) return false;
    if (options_.mode == OutputMode::Inline) {
        // The caller's thread is the one that waits, which is the point of the mode: "the push returned" is "it is on
        // the wire".
        write(data, n);
        return true;
    }
    const std::size_t need = kHeader + n;
    if (need > ring_.size()) {  // bigger than any ring there is: it could never fit, so it is dropped whole
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const std::uint64_t w = write_.load(std::memory_order_relaxed);
    const std::uint64_t r = read_.load(std::memory_order_acquire);
    if (need > ring_.size() - (w - r)) {  // full: drop the NEWEST whole message, never part of one
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const auto length = static_cast<std::uint32_t>(n);
    copyIn(reinterpret_cast<const std::uint8_t*>(&length), kHeader, w);
    copyIn(data, n, w + kHeader);
    write_.store(w + need, std::memory_order_release);
    // The wake (the header comment has the whole shape). `exchange` rather than `load` is what bounds the syscalls:
    // only the push that finds the writer still asleep notifies it, so a block of messages costs at most one futex
    // wake, where a load that sees a writer awake-but-not-yet-running would notify once per message. The clear is the
    // wake's own compare value, so a notify that arrives before the writer's wait is not spent (see the header).
    wake_.fetch_add(1, std::memory_order_seq_cst);
    if (sleeping_.exchange(0, std::memory_order_seq_cst)) sleeping_.notify_one();
    return true;
}

void PortWriter::stop() {
    if (!port_) return;  // already stopped
    // The deadline is taken HERE, before the writer is told to stop: the send already inside the port cannot be
    // interrupted, and one that starts just before the writer sees the flag runs to completion too, so the bound has to
    // be measured from the stop to hold for both (see kFlushDeadline). Taken by the writer when it notices the flag, it
    // would leave the second send outside the bound.
    flushDeadline_ = std::chrono::steady_clock::now() + kFlushDeadline;
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        // The clear is what the writer's wait compares against, so it is not enough to notify: a writer that has not
        // reached the kernel yet must find the flag already cleared (the header comment).
        sleeping_.store(0, std::memory_order_seq_cst);
        sleeping_.notify_all();
        worker_.join();  // the writer flushes to flushDeadline_ and returns
    }
    port_.reset();  // closed now, and no push can reach it: the stream unpublishes and waits sends out before stopping
}

void PortWriter::run() {
    bool                                  flushing = false;
    std::chrono::steady_clock::time_point deadline{};
    for (;;) {
        // The counter snapshot is taken FIRST, before this pass looks at the ring or at the stop: a push that the ring
        // check below misses has bumped the counter after this load, so either the re-read further down sees the bump
        // or the push's exchange found the flag set and cleared it, which is the wake (see the header). Taken after the
        // ring check it does not cover the push that lands between the two, and the writer then sleeps with that record
        // queued - a Note Off at a burst's end, held until the next message or a stop.
        const std::uint32_t seen = wake_.load(std::memory_order_seq_cst);
        if (stopping_.load(std::memory_order_acquire) && !flushing) {
            flushing = true;
            deadline = flushDeadline_;
        }
        if (queued()) {
            if (flushing && std::chrono::steady_clock::now() >= deadline) {
                // The rest is dropped whole and counted, so a stuck note the flush gave up on leaves a trace.
                abandon();
                return;
            }
            sendOne();
            continue;
        }
        if (flushing) return;  // the flush has nothing left to send
        // Nothing to do: publish that this thread is going to sleep, then look once more, so a push that lands after
        // the snapshot above is not missed, and wait for the flag to be cleared.
        sleeping_.store(1, std::memory_order_seq_cst);
        if (wake_.load(std::memory_order_seq_cst) == seen && !stopping_.load(std::memory_order_acquire))
            sleeping_.wait(1, std::memory_order_seq_cst);
        sleeping_.store(0, std::memory_order_seq_cst);
    }
}

bool PortWriter::queued() const { return read_.load(std::memory_order_acquire) != write_.load(std::memory_order_acquire); }

void PortWriter::sendOne() {
    const std::uint64_t r      = read_.load(std::memory_order_relaxed);
    std::uint32_t       length = 0;
    copyOut(reinterpret_cast<std::uint8_t*>(&length), kHeader, r);
    copyOut(scratch_.data(), length, r + kHeader);
    read_.store(r + kHeader + length, std::memory_order_release);
    write(scratch_.data(), length);
}

void PortWriter::abandon() {
    // The flush ran out of time with records still queued: they are dropped whole, as a full ring drops the message it
    // would have refused, and counted the same way, so the Note Off the flush gave up on is not the one lost message
    // nothing reports. The producer is done (no push may follow stop()) and the walk reads only what it left behind.
    std::uint64_t       r = read_.load(std::memory_order_relaxed);
    const std::uint64_t w = write_.load(std::memory_order_relaxed);
    std::uint64_t       n = 0;
    while (r < w) {
        std::uint32_t length = 0;
        copyOut(reinterpret_cast<std::uint8_t*>(&length), kHeader, r);
        r += kHeader + length;
        ++n;
    }
    dropped_.fetch_add(n, std::memory_order_relaxed);
}

void PortWriter::copyIn(const std::uint8_t* from, std::size_t n, std::uint64_t at) {
    const std::size_t size = ring_.size();
    const std::size_t pos  = static_cast<std::size_t>(at % size);
    const std::size_t head = n < size - pos ? n : size - pos;
    std::memcpy(ring_.data() + pos, from, head);
    if (head < n) std::memcpy(ring_.data(), from + head, n - head);
}

void PortWriter::copyOut(std::uint8_t* to, std::size_t n, std::uint64_t at) const {
    const std::size_t size = ring_.size();
    const std::size_t pos  = static_cast<std::size_t>(at % size);
    const std::size_t head = n < size - pos ? n : size - pos;
    std::memcpy(to, ring_.data() + pos, head);
    if (head < n) std::memcpy(to + head, ring_.data(), n - head);
}

void PortWriter::write(const std::uint8_t* data, std::size_t n) {
    if (options_.log) {
        // The port is in the line: a Threaded stream has one writer per output, and two ports sending the same bytes is
        // not something a dump should hide (an Inline stream keeps its one line, which covers every destination).
        std::string line = "MIDI out (" + name_ + "): ";
        char        hex[4];
        for (std::size_t i = 0; i < n; ++i) {
            std::snprintf(hex, sizeof(hex), "%02X ", data[i]);
            line += hex;
        }
        log_(line);
    }
    port_->send(data, n);
}

}  // namespace rp::midi

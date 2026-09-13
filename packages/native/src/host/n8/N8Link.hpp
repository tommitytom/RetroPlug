#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "host/n8/Edio.hpp"  // ISerialPort, Edio
#include "transport/SpscRing.hpp"

namespace retroplug {

// Host-side link to a physical Everdrive N8 Pro for the standalone/plugin: the audio thread hands MIDI bytes
// (with intra-block sample offsets) to a lock-free ring; a dedicated serial thread drains it on a timed
// schedule and writes them to the cart FIFO via Edio. USB I/O never touches the audio thread, and a small
// constant lookahead turns block-quantized arrival into accurate relative on-wire timing. This is the
// standalone/plugin twin of the CLI's runN8Bridge (same Edio + scheduler), fed from an SpscRing instead of
// a live MIDI port. Lifecycle modeled on AudioDriverRpcService (run-flag + join-in-dtor, thread member last).
class N8Link {
public:
    // Serial-port opener the caller supplies - the standalone passes a WjwwoodSerialPort factory, a unit
    // test passes one returning a FakeSerialPort. Keeping it injected means N8Link depends only on
    // ISerialPort (not the serial lib), so it's testable hardware-free. May throw if the port can't be
    // opened; connect() catches it. (Port enumeration for the picker is the caller's job via listSerialPorts.)
    using PortFactory = std::function<std::unique_ptr<ISerialPort>(const std::string&)>;

    // Ran on the freshly handshaken Edio inside connect(), before the serial thread starts - so it is the
    // only user of the port and needs no locking. The one-shot "the link just came up" seam: N8Host hangs the
    // expansion-audio master volume off it (N8Host::applyExpVol). A throw is caught and ignored: a config
    // write must never fail a working MIDI link.
    using OnConnected = std::function<void(Edio&)>;

    explicit N8Link(PortFactory factory);
    ~N8Link();
    N8Link(const N8Link&)            = delete;
    N8Link& operator=(const N8Link&) = delete;

    // --- Control (UI/main thread) ---
    // Set once by the owner before any connect (read-only afterwards, so the SD worker's resume-connect can
    // run it too).
    void          setOnConnected(OnConnected fn) { onConnected_ = std::move(fn); }
    // Run `fn` on the serial thread's Edio at its next turn. The UI thread must NOT touch the port while the
    // link is up (the serial thread owns it), and tearing the link down to reach the device would drop
    // whatever MIDI was in flight - a dropped note-off is a stuck note on real hardware. Replaces any call
    // still pending (these are "make the device match the current setting", so only the last one matters).
    // A no-op while disconnected: the connect path applies the current config through OnConnected anyway.
    void          postControl(std::function<void(Edio&)> fn);
    bool          connect(const std::string& port);  // open + handshake + spawn the serial thread
    void          disconnect();                       // stop + join the serial thread, close the port
    bool          isConnected() const { return connected_.load(std::memory_order_acquire); }
    std::string   portName() const;
    std::uint64_t bytesForwarded() const { return bytesForwarded_.load(std::memory_order_relaxed); }
    std::string   lastError() const;
    void          setLookaheadMs(int ms) { lookaheadNs_.store(std::int64_t(ms) * 1'000'000, std::memory_order_relaxed); }
    int           lookaheadMs() const { return static_cast<int>(lookaheadNs_.load(std::memory_order_relaxed) / 1'000'000); }

    // --- Audio thread ---
    // Forward MIDI/bytes with their intra-block sample offset. Lock-free, never blocks; drops on ring-full;
    // a no-op when not connected. Splits a >8-byte push into multiple chunks (rare - MIDI is <=4).
    void push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate);

private:
    struct TimedChunk {
        std::int64_t targetNs = 0;  // steady_clock ns at which to release this chunk
        std::uint8_t len      = 0;
        std::uint8_t data[8]  = {0};
    };

    void serialLoop();
    void setError(const std::string& msg);

    PortFactory                factory_;
    OnConnected                onConnected_;  // set before the first connect; never mutated afterwards
    SpscRing<TimedChunk, 1024> ring_;  // audio producer -> serial consumer
    std::atomic<std::int64_t>  lookaheadNs_{0};
    std::atomic<bool>          connected_{false};
    std::atomic<bool>          running_{false};
    std::atomic<std::uint64_t> bytesForwarded_{0};

    mutable std::mutex meta_;      // guards portName_ + error_ (set on connect/disconnect/error, read by status)
    std::string        portName_;
    std::string        error_;

    std::mutex             control_;  // guards pendingControl_ (posted by the UI thread, taken by the serial one)
    std::function<void(Edio&)> pendingControl_;

    std::unique_ptr<ISerialPort> serialPort_;  // owned; read by the serial thread
    std::unique_ptr<Edio>        edio_;
    std::thread                  thread_;  // LAST: joined (in disconnect) before serialPort_/edio_ are torn down
};

}  // namespace retroplug

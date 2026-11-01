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
#include "transport/N8ReliableLink.hpp"
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
    // Ran on the serial thread at the top of every turn, while the link is up: where a setting whose value lives in an
    // atomic (N8Host's expansion volume) reaches the device without the caller taking a lock or building a
    // std::function. Set once before any connect, like OnConnected, so it is never mutated while the thread reads it;
    // a throw is caught and ignored, so a config write never takes the MIDI link down.
    using OnControl = std::function<void(Edio&)>;

    explicit N8Link(PortFactory factory);
    ~N8Link();
    N8Link(const N8Link&)            = delete;
    N8Link& operator=(const N8Link&) = delete;

    // --- Control (UI/main thread) ---
    // Set once by the owner before any connect (read-only afterwards, so the SD worker's resume-connect can
    // run it too).
    void          setOnConnected(OnConnected fn) { onConnected_ = std::move(fn); }
    void          setOnControl(OnControl fn) { onControl_ = std::move(fn); }
    bool          connect(const std::string& port);  // open + handshake + spawn the serial thread
    void          disconnect();                       // stop + join the serial thread, close the port
    bool          isConnected() const { return connected_.load(std::memory_order_acquire); }
    std::string   portName() const;
    std::uint64_t bytesForwarded() const { return bytesForwarded_.load(std::memory_order_relaxed); }
    // Pushes that lost their bytes: the link was down, or the ring was full. Written on the audio thread, so it is the
    // device link's (rp.host.devices/1) dropped count and the UI's, read relaxed.
    std::uint64_t pushesDropped() const { return pushesDropped_.load(std::memory_order_relaxed); }
    std::string   lastError() const;
    void          setLookaheadMs(int ms) { lookaheadNs_.store(std::int64_t(ms) * 1'000'000, std::memory_order_relaxed); }
    int           lookaheadMs() const { return static_cast<int>(lookaheadNs_.load(std::memory_order_relaxed) / 1'000'000); }

    // BlipToaster's reliable link (transport/N8ReliableLink.hpp). On connect the serial thread sends a sync (F4) and
    // listens ~100 ms for the ROM's ack on the cart's USB back-channel: a ROM that answers gets framed MIDI and
    // re-sends of whatever an NTSC DMC-DMA halt loses - the same sender, and so the same recovery, as the plugin's
    // emulated N8; one that does not (any other MIDI ROM, an older BlipToaster) gets raw MIDI as before.
    bool          reliable() const { return reliable_.load(std::memory_order_acquire); }
    std::uint64_t linkResends() const { return linkResends_.load(std::memory_order_relaxed); }
    std::uint64_t linkOutstanding() const { return linkOutstanding_.load(std::memory_order_relaxed); }
    std::int64_t  linkRttMinNs() const { return linkRttMinNs_.load(std::memory_order_relaxed); }
    // TEST ONLY: drop this many per mille of the framed bytes on their way out, in 1-3 byte bursts - what an NTSC
    // DMC-DMA halt does at the cart - so the recovery can be exercised on a PAL console (which loses nothing).
    void          setTestLossPermille(int p) { testLossPermille_.store(p, std::memory_order_relaxed); }
    // TEST / MEASUREMENT: set before connect. setLinkAllowed(false) skips the probe - raw MIDI even to a ROM with the
    // link. setWriteLog gets every FIFO write as it goes out (serial thread; the bytes, CLOCK_MONOTONIC ns taken just
    // before the write, and whether it is a link re-send) - what retroplug-n8-hwtest `timing` times latency from.
    using WriteLog = std::function<void(const std::uint8_t* data, std::size_t n, std::int64_t ns, bool resend)>;
    void          setLinkAllowed(bool on) { linkAllowed_ = on; }
    void          setWriteLog(WriteLog log) { writeLog_ = std::move(log); }

    // --- Audio thread ---
    // Forward MIDI/bytes with their intra-block sample offset. Lock-free, never blocks; drops on ring-full;
    // a no-op when not connected. Splits a >8-byte push into multiple chunks (rare - MIDI is <=4). False when any of
    // the bytes were lost (the link is down, or a chunk found the ring full), which is what a device link's own drop
    // count is made of: pushesDropped() also counts the callers that are not that node.
    bool push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate);

private:
    struct TimedChunk {
        std::int64_t targetNs = 0;  // steady_clock ns at which to release this chunk
        std::uint8_t len      = 0;
        std::uint8_t data[8]  = {0};
    };

    void serialLoop();
    void probeLink();                                    // serial thread: sync + listen for an ack
    void serviceLink();                                  // serial thread: read acks, send re-sends
    void writeFramed(std::vector<std::uint8_t>& out, const std::vector<int>& seqs, std::int64_t now, bool resend);
    void setError(const std::string& msg);

    PortFactory                factory_;
    OnConnected                onConnected_;  // set before the first connect; never mutated afterwards
    OnControl                  onControl_;    // likewise
    SpscRing<TimedChunk, 1024> ring_;  // audio producer -> serial consumer
    std::atomic<std::int64_t>  lookaheadNs_{0};
    std::atomic<bool>          connected_{false};
    std::atomic<bool>          running_{false};
    std::atomic<std::uint64_t> bytesForwarded_{0};
    std::atomic<std::uint64_t> pushesDropped_{0};

    mutable std::mutex meta_;      // guards portName_ + error_ (set on connect/disconnect/error, read by status)
    std::string        portName_;
    std::string        error_;

    // The reliable link: owned by the serial thread (null = raw); the atomics are its stats for other threads.
    std::unique_ptr<rp::N8ReliableSender> rel_;
    std::atomic<bool>          reliable_{false};
    std::atomic<std::uint64_t> linkResends_{0};
    std::atomic<std::uint64_t> linkOutstanding_{0};
    std::atomic<std::int64_t>  linkRttMinNs_{-1};
    std::atomic<int>           testLossPermille_{0};
    bool                       linkAllowed_ = true;   // set before connect (see setLinkAllowed)
    WriteLog                   writeLog_;
    std::uint32_t              lossRng_ = 0x2545F491u;

    std::unique_ptr<ISerialPort> serialPort_;  // owned; read by the serial thread
    std::unique_ptr<Edio>        edio_;
    std::thread                  thread_;  // LAST: joined (in disconnect) before serialPort_/edio_ are torn down
};

}  // namespace retroplug

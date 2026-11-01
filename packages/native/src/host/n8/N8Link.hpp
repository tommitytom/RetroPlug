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
    // atomic (N8Host's expansion volume and its config-block probe) reaches the device without the caller taking a lock
    // or building a std::function. Set once before any connect, like OnConnected, so it is never mutated while the
    // thread reads it. A throw is caught and ignored, so a config write never takes the MIDI link down; the next turn
    // is then a second later, not the next 200 us, so a port that keeps refusing a write cannot take the link's turns
    // from the MIDI stream.
    using OnControl = std::function<void(Edio&)>;

    // What a paused job tells the link once it has run: resume streaming, or stop it (a ROM boot: the old stream is
    // stale, and a running game has no menu to come back to). Stop closes the port, as a disconnect does.
    enum class AfterJob { Resume, Stop };
    // A job the serial thread runs with streaming paused (docs/modular-graph-phase-c5-design.md section 6). The pause
    // keeps the port, the reliable link's sequence state and the expansion volume: everything already pushed goes out
    // first, each chunk at its own release time, and the reliable link waits until the ROM has acknowledged every frame
    // (bounded), so a note-off already on its way is not lost; the job then has the port's Edio to itself, and
    // streaming picks up where it was. `run` gets the link's own Edio, or null when the link went down before the job
    // could run (it then only says so); `done` runs once streaming has resumed or stopped. Each runs exactly once, on
    // the serial thread, so a job's completion is posted after the link is back. A job checks stopRequested() between
    // its steps, and those are its only stop points: a disconnect() (or the owner's destructor) that lands inside a
    // step waits for it: a 512-byte block of a file read (about half a second on the real cart), a battery file's whole
    // write once it has begun (under a second), an upload's 8 KB chunk, up to the Edio timeout (2 s, raised to 10 s
    // while the menu installs an uploaded ROM) for a cart that stops answering, plus the drain's bounded wait for acks.
    // A throw that escapes `run` is the port's (a job reports its own failures and returns): it is recorded
    // (lastError), a job that never ran is told the link went down, and the link stops streaming as a failed write
    // stops it, so a completion that had already been decided is still posted.
    struct PausedJob {
        std::function<AfterJob(Edio*)> run;
        std::function<void()>          done;
    };

    explicit N8Link(PortFactory factory);
    ~N8Link();
    N8Link(const N8Link&)            = delete;
    N8Link& operator=(const N8Link&) = delete;

    // --- Control (UI/main thread) ---
    // Set once by the owner before any connect; connect() runs it on the caller's thread, while the link is the only
    // thing on the port.
    void          setOnConnected(OnConnected fn) { onConnected_ = std::move(fn); }
    void          setOnControl(OnControl fn) { onControl_ = std::move(fn); }
    // Hand the serial thread a job to run with streaming paused (see PausedJob). False, and nothing runs, while the
    // link is down or another job is pending. From the moment the thread takes it until it is done, isConnected()
    // reads false and push() drops (and counts) what it is given.
    bool          runPaused(PausedJob job);
    // The serial thread has a job to run, or is running one: the flag clears just before the job's `done`, which is
    // what keeps a completion `done` posts out of the link's own busy answer.
    bool          jobPending() const { return jobPending_.load(std::memory_order_acquire); }
    // [any] A disconnect has been asked for: a paused job checks this between its steps and stops early.
    bool          stopRequested() const { return !running_.load(std::memory_order_acquire); }
    // Chunks a stream that stopped after a job (AfterJob::Stop) found in its ring and threw away: pushes that raced the
    // pause's start, and so reached the ring after its drain. Counted in pushesDropped() too.
    std::uint64_t chunksDiscarded() const { return chunksDiscarded_.load(std::memory_order_relaxed); }
    bool          connect(const std::string& port);  // open + handshake + spawn the serial thread
    // Stop + join the serial thread, close the port. With a paused job running the join waits for the job's current
    // step to end (see PausedJob): a caller on a UI thread (Settings > N8's Connect and port rows) should not offer it
    // while a job holds the cart, as N8Host refuses its config writes then.
    void          disconnect();
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
    void streamLoop();                                   // serial thread: the probe, then stream until stopped
    void writeChunk(const TimedChunk& chunk);            // serial thread: one chunk out, framed when reliable
    void runJob(TimedChunk& chunk, bool& have);          // serial thread: pause, run the pending job, resume
    void drainForJob(TimedChunk& chunk, bool& have);     // serial thread: what was pushed before the pause goes out
    void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
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
    std::atomic<std::uint64_t> chunksDiscarded_{0};
    // The earliest moment OnControl runs again after a throw (see the loop in streamLoop): serial thread only.
    std::int64_t controlRetryAtNs_ = 0;

    // The paused job (runPaused): handed over under jobMutex_, which also orders a submission against the serial
    // thread's exit, so a job taken is always run (with null, if the link went down first). jobPending_ is the flag the
    // serial thread polls; it clears just before the job's `done` runs, so a completion `done` posts is never refused
    // busy by the link (the owner's own slot is what holds a second job off until then).
    std::mutex                 jobMutex_;
    PausedJob                  job_;
    bool                       acceptingJobs_ = false;  // under jobMutex_: the serial thread is running and will poll
    std::atomic<bool>          jobPending_{false};

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

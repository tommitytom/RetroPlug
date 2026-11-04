#pragma once

#include <atomic>
#include <bitset>
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
    // Ran on the serial thread each turn while the link is up, once the reliable link's acks are read and its re-sends
    // written: where a setting whose value lives in an atomic (N8Host's expansion volume and its config-block probe)
    // reaches the device without the caller taking a lock or building a std::function. Set once before any connect,
    // like OnConnected, so it is never mutated while the thread reads it. `backChannelQuiet` is false while the cart
    // may be about to acknowledge an outstanding frame: anything that reads a reply off the port must wait for a quiet
    // turn, since an ack landing inside that read is taken for the reply's first bytes and the reply's tail is then
    // parsed as link traffic. A turn is quiet with nothing outstanding, or once the cart has said nothing for the
    // pause's ack bound (250 ms) since the oldest outstanding frame first went out: a cart at its menu acknowledges
    // nothing, and would otherwise never give a read a turn. What it does not cover is an ack for no outstanding frame:
    // on a lossy link the ROM acknowledges each copy of a frame the host sent twice, and the second copy's ack can come
    // after the first has emptied the window. A read that meets one misreads a few bytes (an Auto that sees no
    // expansion mapper writes nothing; a probe that sees no menu refuses the job) and leaves the reply's last bytes in
    // the input, which the link parses as an ack only if they spell one. A throw is caught and ignored, so a config
    // write never takes the MIDI link down; the callback then waits a second, not the next 200 us turn, so a port that
    // keeps refusing a write cannot take the link's turns from the MIDI stream (a caller that cannot wait that out, a
    // submission's probe, asks for a quiet turn of its own through wakeControl).
    using OnControl = std::function<void(Edio&, bool backChannelQuiet)>;

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
    // (lastError), a job that never started is told the link went down (one whose `run` threw did run, and is not run
    // again), and the link stops streaming as a failed write stops it, so a completion that had already been decided is
    // still posted - and a job that had decided to stop closes the port as it would have.
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
    // [any] Run OnControl at the next quiet turn, whatever the wait a failed config write armed: the wake stands
    // through turns that are not quiet, and is spent by the first that is. A submission's menu probe is why this
    // exists: it is bounded by its own deadline, and a volume write that keeps failing must not be what it waits
    // behind.
    void          wakeControl() { controlWanted_.store(true, std::memory_order_release); }
    // [any] Whether the serial thread's last turn was quiet for a read (see OnControl): what tells a probe that timed
    // out on a link that kept acknowledging from one whose read never answered. The last turn's answer, not the probe's
    // whole second: a quiet turn with the probe pending always applies it, since runningMapper stores its answer before
    // the generation is checked.
    bool          backChannelQuiet() const { return backChannelQuiet_.load(std::memory_order_relaxed); }
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
    // The notes a cart holds, one bit a note (channel * 128 + note), as rp::n8::CartNotes.
    using NoteSet = std::bitset<16 * 128>;
    // [control] Send each of `notes` a note-off (0x8n, note, 0) from the serial thread: after every chunk pushed before
    // this call has gone out, so a note-on still waiting for its release time cannot follow its own release, framed
    // when the link is reliable, never during a job's pause (the next streaming turn writes them), and on a disconnect
    // before the port closes. Dropped when no serial thread runs (the link is down), when a chunk write throws (the
    // stream stops there, and a port that just failed a write cannot carry them; a reconnect does not replay them), and
    // when an upload's boot stops the stream (the port is closed, and the ROM it booted holds none). A port that throws
    // inside a job stops the stream with the port still open, so the stream's end still tries them. How a cart node
    // leaving the plan releases what it held: its destroy runs on the control thread, which may not push into the audio
    // thread's ring.
    void          releaseNotes(const NoteSet& notes);
    bool          connect(const std::string& port);  // open + handshake + spawn the serial thread
    // Stop + join the serial thread, close the port. With a paused job running the join waits for the job's current
    // step to end (see PausedJob): a caller on a UI thread (Settings > N8's Connect and port rows) should not offer it
    // while a job holds the cart, as N8Host refuses its config writes then.
    void          disconnect();
    bool          isConnected() const { return connected_.load(std::memory_order_acquire); }
    std::string   portName() const;
    std::uint64_t bytesForwarded() const { return bytesForwarded_.load(std::memory_order_relaxed); }
    // Pushes that lost their bytes: the link was down or a job held it, the ring was full, or a stopped stream threw
    // a chunk away (chunksDiscarded). Written on the audio thread and by the serial thread's stopStreaming, read
    // relaxed; a device link's own count is made of the `false` push() answers instead, and N8CartLink's dropped()
    // adds the chunks a stopped stream discarded.
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
    // Forward MIDI/bytes with their intra-block sample offset. Lock-free, never blocks; drops on ring-full, and drops
    // everything while the link is down or a job holds it (both counted, never waited on). Splits a >8-byte push into
    // multiple chunks (rare - MIDI is <=4). False when any of the bytes were lost (the link is down, a job holds it, or
    // a chunk found the ring full): what a node keeps its own books on, where pushesDropped() is also every other
    // caller's.
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
    void writeReleases(bool stopping);                   // serial thread: releaseNotes' note-offs, once they may go
    void runJob(TimedChunk& chunk, bool& have);          // serial thread: pause, run the pending job, resume
    void drainForJob(TimedChunk& chunk, bool& have);     // serial thread: what was pushed before the pause goes out
    void stopStreaming();                                // serial thread: stop and close the port (AfterJob::Stop)
    void probeLink();                                    // serial thread: sync + listen for an ack
    void serviceLink();                                  // serial thread: read acks, send re-sends
    bool quietForRead(std::int64_t now) const;           // serial thread: may a reply be read now (see OnControl)
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
    // Every chunk push() put in the ring (written on the audio thread), and every one the serial thread took out and
    // wrote (its own, reset to the first at each connect): a release waits until the second reaches what the first
    // read when it was asked, so it goes out behind everything pushed before it.
    std::atomic<std::uint64_t> chunksPushed_{0};
    std::uint64_t              chunksWritten_ = 0;
    // The earliest moment OnControl runs again after a throw (see the loop in streamLoop), and a wake a caller asked
    // for since the last quiet turn: the first is serial-thread state, the second is stored from any thread.
    std::int64_t               controlRetryAtNs_ = 0;
    std::atomic<bool>          controlWanted_{false};
    std::atomic<bool>          backChannelQuiet_{true};  // the last turn's quietForRead, for other threads

    // The paused job (runPaused): handed over under jobMutex_, which also orders a submission against the serial
    // thread's exit, so a job taken is always run (with null, if the link went down first). jobPending_ is the flag the
    // serial thread polls; it clears just before the job's `done` runs, so a completion `done` posts is never refused
    // busy by the link (the owner's own slot is what holds a second job off until then).
    std::mutex                 jobMutex_;
    PausedJob                  job_;
    bool                       acceptingJobs_ = false;  // under jobMutex_: the serial thread is running and will poll
    std::atomic<bool>          jobPending_{false};
    // The notes releaseNotes was given and the serial thread has not written yet, and the push count they wait behind,
    // both under jobMutex_ (a control thread and the serial thread; the audio thread never takes it). The flag is what
    // the serial thread polls, so a turn with nothing to release takes no lock.
    NoteSet                    releasePending_;
    std::uint64_t              releaseAfter_ = 0;
    std::atomic<bool>          releaseWanted_{false};

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

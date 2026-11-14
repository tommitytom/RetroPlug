#pragma once

// One hardware MIDI output and the thread that writes to it (docs/known-bugs-batch-3-design.md section 6). A port's
// send may BLOCK until the bytes are on the wire: WinMM's SysEx path allocates and sleeps while midiOutUnprepareHeader
// reports MIDIERR_STILLPLAYING, so a 538-byte Launchpad repaint is about 170 ms at DIN rate; ALSA's grows its encode
// buffer, and CoreMIDI makes an OS call. The SDL standalone sends from its audio callback (one `send` per queued
// message, after the block is rendered), so the write has to leave the callback: the callback copies the message into a
// preallocated ring and returns, and a writer thread per hardware output does the blocking.
//
// THREADING. The ring is SPSC: one producer (the audio thread, which is send()'s only caller) and one consumer (this
// object's thread). A push is ALL OR NOTHING - a record is its length header and its bytes, and a record that does not
// fit is dropped whole and counted - so a SysEx is never torn and never interleaved with the message after it. Every
// byte both sides touch is inside the preallocated ring and a scratch buffer of the same size, so neither the push nor
// the writer allocates.
//
// The wake is a C++20 atomic wait (futex on Linux, WakeByAddressSingle on Windows, __ulock_wait on macOS, which libc++
// provides from macOS 11 and this repository pins no older deployment target: the release build's runner is
// macos-15). The writer waits on the FLAG word, not on the counter, and the producer's exchange of that flag for 0 is
// both the question "is a writer asleep?" and the wake itself: a notify the writer never used (it had not reached the
// kernel yet) cannot then be left spent, because the clear is the value its wait compares against, and it has to
// publish the flag again to sleep again. The counter is what says a push landed while the writer was deciding: the
// producer bumps it before it looks at the flag, and the writer publishes the flag and then re-reads the counter, so a
// push from that window is caught by the re-read rather than by a notify (a notify per message would be a syscall per
// message for a writer that is usually awake). The four operations are seq_cst for the reason MidiStream::retire gives
// for its own two: this is the store-buffering (Dekker) shape, the writer writes the flag and then reads the counter
// while the producer bumps the counter and then exchanges the flag, and under acquire/release both store-load pairs may
// be reordered - the writer would then sleep with a record queued, which x86 hides and AArch64 (the handheld and Pi
// builds) does not. In the one total order either the exchange sees the writer's flag and wakes it, or the writer's
// re-read sees the bumped counter. The snapshot that re-read compares against is taken at the TOP of each pass, before
// the writer looks at the ring: one taken after that look does not cover the push that lands in between.
//
// stop() is a push with no record, so the same argument covers it: after it sets `stopping_` it bumps the counter, then
// clears the flag and notifies all. The bump is for a writer that publishes its sleep after the clear: the clear, the
// publish and the re-read are in that order in the total order, so the re-read sees the bump (or a later one) and the
// writer does not wait, and the pass whose snapshot reads the bump synchronizes with it, so it sees `stopping_`. The
// clear is for a writer that published before it: the clear is the first change to the flag after that publish (or a
// push's exchange is, which wakes it the same way), its notify is eligible to unblock the wait, and the wait's load of
// the cleared flag synchronizes with it, so the next pass sees `stopping_` too. The clear alone is not enough: for a
// writer that publishes after it, two stores to the flag order nothing, and the C++ model lets an acquire load of
// `stopping_` miss a release store no synchronization reached, so that writer could wait on 1 for ever and stop()'s
// join never return. No test can show that: x86 never reorders it, and on AArch64 the seq_cst re-read in front of that
// load happens to order it in today's code generation; the model's guarantee is what this relies on, not either.
//
// STOPPING. A port that leaves the selection, or the stream closing, stops its writer with a FLUSH: what a push has
// queued is sent before the port goes, because a dropped Note Off is a stuck note. The flush is bounded by
// kFlushDeadline, which stop() counts from itself; a send that started just before the writer saw the stop runs to
// completion inside the bound and one that started just before the deadline runs on after it, so a close can take up to
// two sends on top (WinMM gives no way to cancel a write in progress). What the deadline leaves in the ring is dropped
// whole and counted, as a message a full ring refused is, so a Note Off the flush gave up on still leaves the trace a
// stuck note needs. Nothing may push after stop() begins: the stream unpublishes the set and waits the send in flight
// out before it stops a writer, which is the order its epoch handshake already keeps for the ports' lifetime.
//
// An Inline writer is the same object with no thread: push() writes to the port on the caller's thread, which is what
// the CLI's facet and the test host want (their caller is the JS thread, and a script that returns from `midiSend` and
// then exits relies on the bytes having gone out). One class, so the ownership, the dropping and the close order are
// the same in both modes.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "midi/MidiBackend.hpp"

namespace rp::midi {

/** How a stream hands a message to its hardware outputs: on the caller's thread (Inline) or on a writer thread of its
 *  own per output (Threaded). The virtual output is inline in both: a DAW or an aconnect route gains no latency. */
enum class OutputMode { Inline, Threaded };

class PortWriter {
public:
    /** The ring's size. A DIN port drains at about 3125 bytes a second, so this is over twenty seconds of the busiest
     *  traffic a control surface makes; a bigger ring would only add latency to a flush. */
    static constexpr std::size_t kDefaultRingBytes = 64 * 1024;
    /** How long stop() keeps sending what is queued, counted from the stop. A Note Off left in the ring is a stuck
     *  note; a port that is wedged must not hold the control thread's close for longer than this, which the two sends
     *  it can still be inside (one already running, one started just before the deadline) add to. */
    static constexpr std::chrono::milliseconds kFlushDeadline{500};

    /** The line sink: the stream's own `emit`, so a writer's log line carries the same prefix and reaches the same
     *  sink an embedder installed. Called from the writer's thread. */
    using LogSink = std::function<void(const std::string&)>;

    struct Options {
        OutputMode  mode      = OutputMode::Threaded;
        bool        log       = false;  // RETROPLUG_MIDI_LOG: dump each message as it is written
        std::size_t ringBytes = kDefaultRingBytes;
    };

    /** Take ownership of an OPEN port and, in Threaded mode, start the writer. `name` is the port's, for the log. */
    PortWriter(std::unique_ptr<MidiPort> port, std::string name, Options options, LogSink log);
    ~PortWriter();
    PortWriter(const PortWriter&)            = delete;
    PortWriter& operator=(const PortWriter&) = delete;

    /** Queue one message. Threaded: all or nothing into the ring, no lock and no allocation. Inline: written to the
     *  port on this thread. False when the message did not go (it was dropped whole because the ring was full, or the
     *  port had already stopped), so a caller may count it; `dropped()` carries the ring's own count. */
    bool push(const std::uint8_t* data, std::size_t n);

    /** Send what is queued (up to kFlushDeadline), join the writer and close the port. No push may come afterwards.
     *  Idempotent, and safe on a writer whose port never opened (it has no thread to join). */
    void stop();

    /** Whole messages dropped because the ring was full, or abandoned when a stop's flush ran out its deadline (a
     *  wedged port), since this writer opened. Read on the control thread, as MidiStream::droppedCount() is, and after
     *  the writer's stop when an abandoned record is to be in it. */
    std::uint64_t      dropped() const { return dropped_.load(std::memory_order_relaxed); }
    const std::string& name() const { return name_; }

private:
    static constexpr std::size_t kHeader = sizeof(std::uint32_t);  // a record is its length, then its bytes

    void run();                                                  // the writer thread, until stop() and an empty ring
    void sendOne();                                              // one queued record: write it and free its space
    void abandon();                                              // a flush's deadline ran out: drop and count the rest
    bool queued() const;
    void copyIn(const std::uint8_t* from, std::size_t n, std::uint64_t at);
    void copyOut(std::uint8_t* to, std::size_t n, std::uint64_t at) const;
    void write(const std::uint8_t* data, std::size_t n);  // one port send, logged when the dump is on

    std::unique_ptr<MidiPort> port_;  // closed by stop()
    std::string               name_;
    Options                   options_;
    LogSink                   log_;

    // The ring, read by the writer and written by the producer. `write_` and `read_` count BYTES since the writer
    // opened (never wrapped), and a byte's place in the buffer is its index modulo the size: a record may straddle the
    // end, which copyIn/copyOut handle in two pieces.
    std::vector<std::uint8_t>  ring_;
    std::vector<std::uint8_t>  scratch_;  // the writer's contiguous copy of one record, so a send never sees two pieces
    std::atomic<std::uint64_t> write_{0};
    std::atomic<std::uint64_t> read_{0};
    std::atomic<std::uint64_t> dropped_{0};

    // The wake (see the header comment). The writer waits on `sleeping_`, which the producer clears to wake it; `wake_`
    // counts pushes (and the stop), so the writer can tell that one landed while it was deciding to sleep. Both are four
    // bytes: libstdc++ then waits on the flag word itself with a futex (its direct path needs a word the size of an
    // int) and MSVC with WaitOnAddress, while libc++ on Apple, whose native wait type is 64-bit, routes a 32-bit word
    // through its contention table and waits there (correct, one indirection more).
    std::atomic<std::uint32_t> wake_{0};
    std::atomic<std::uint32_t> sleeping_{0};  // 1: the writer published that it is going to sleep; 0: it is not
    std::atomic<bool>          stopping_{false};
    // When the flush runs out, taken by stop() before it sets `stopping_` and read by the writer after it sees it.
    std::chrono::steady_clock::time_point flushDeadline_{};

    std::thread worker_;  // Inline: never started
};

}  // namespace rp::midi

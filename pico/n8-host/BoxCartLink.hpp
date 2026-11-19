#pragma once

// The box's cart link (docs/modular-graph-phase-e3-design.md section 5, docs/pico-box-framed-link-design.md):
// rp::n8::CartLink (n8/CartLink.hpp) over the main loop's one Edio, with no thread of its own. rp.n8.nes pushes into a
// fixed ring during the block; after the block the main loop drains the ring to the cart's FIFO in writes of up to 192
// bytes (drain()), so a push never waits on the N8: block and drain run in turn on the main loop (an audio cadence
// would run blocks off it: design 10).
//
// - push() copies the message into the ring and answers false, counted, when no game is running (the box's forwarding
//   state, which its N8 part sets each pass through setConnected(): a game is running and the box forwards to it),
//   when the menu guard has heard the N8 menu answer (below), or when the ring is full.
// - drain() writes whole messages back to back, as many as fit in 192 bytes (a write costs the same ~5 ms whatever it
//   carries, so batching is what lets a burst through), and on a raw link splits only a message longer than that (a
//   SysEx). A message is counted forwarded, or failed, in the write that carries its last byte.
// - BlipToaster's reliable link and the N8 menu guard, the desktop's N8Link's, from the one implementation both run
//   (the n8-wire package's N8ReliableSender and N8MenuGuard), when the Writer reads the cart's back-channel and the
//   Clock has its 64-bit time; without either the link is raw and unguarded, as the box was before them. A connect
//   writes the link's sync (F4 00) at its next drain and listens for the ack across the passes that follow, for 100 ms,
//   while what is pushed waits in the ring: an ack makes the link framed (BlipToaster), silence leaves it raw
//   (setLinkAllowed(false) skips the probe). An ack heard later on a raw link is a ROM that answered the sync after
//   the window (BlipToaster read it late, and its link mode drops unframed messages): the probe runs once more for the
//   connect. Each drain then reads the back-channel (the acks for the sender, the menu's 'k' for the guard), takes
//   the guard's turn, puts the guard's probe at the front of its first write when one is due (a drain starts between
//   two messages), and, unless the guard holds, writes the sender's re-sends and the
//   ring's messages, each framed on a framed link, in writes that never split a frame, and never with more than the
//   window less one outstanding (the rest waits for the next drain, which reads the acks first, so a backlog never
//   makes the sender give its window up). A SysEx longer than a frame carries is dropped there (`unframed`), as the
//   sender drops it. The menu's answer drops the ring and the window (counted not ready) and atMenu() says so until
//   the box disconnects the link to boot the ROM again; two stall probes that hear nothing drop them too, and probe
//   the link again, which a game without the link answers raw.
// - quietForRead() answers whether an Edio read now (the box's CMD_STATUS, its sniff, the volume's read-back) can meet
//   nothing the link is owed on the same USB input: N8Link's rule, and not while the connect's sync waits for its ack.
// - releaseNotes() queues a note-off for each note the same way, behind what was already pushed.
// - setExpansionVolume() records the value (clamped to the register's 0..255, or -1 to leave the register to the
//   cart), and drain() writes it to the cart's config register while connected: at every connect (once the link probe
//   has decided) and on a change, on a turn quietForRead() allows, as the desktop's N8Host does (and a failed write
//   again a second later). Its -1 (N8Host::EXP_VOL_AUTO) writes nothing here: the desktop's rule for it (unity for an
//   expansion mapper, else the N8 OS's value) is for a link no node holds. The bridge plan's rp.n8.nes never leaves it
//   at -1, as it gives its value at its first block.
// - acquire() and release() are the hooks the box installs the link with (rp.host.devices/2), counting the nodes that
//   hold it; the last release puts the volume back to -1, as N8CartLink does.
// - No device jobs: the box offers no rp.host.jobs/1, so rp.n8.nes refuses a job before it reaches the link, and the
//   link's own answer is CartLink's ("not connected").
//
// What it counts is the plan's edge into the cart, which the box's stats read without reaching into the plan: the
// pushes it was handed, and of them the ones rp.n8.nes made itself, the note-offs it owes for notes whose note-off a
// disconnect dropped and pays at the next connected block. The link keeps the node's books on its own answers to tell
// those apart (sounding: a note-on taken, no note-off taken since; owed: a sounding note whose note-off it refused),
// the rules N8Node.cpp's pushMidi keeps, so the box's `filtered` (staged minus what the link was handed from the
// plan's input) stays what the plan dropped. That holds for MIDI pushed through the node's `midi` input, the only input
// the box's plan wires. A message the link took and then dropped (the menu's answer, a re-probe, a disconnect) was
// taken, so the books, as the node's, count it delivered.
//
// Portable (no Pico SDK), so the desktop's tests build it as the box does, and heap-free, so the box links it under the
// device build's trap. One thread: the block and the drain both run on the box's main loop, which also sets the
// connected state.

#include <cstddef>
#include <cstdint>

#include "graph/rp_device_midi_in.h"
#include "n8/CartLink.hpp"
#include "transport/N8MenuGuard.hpp"
#include "transport/N8ReliableLink.hpp"

namespace rp::box {

class BoxCartLink final : public rp::n8::CartLink {
public:
    // Room for one block's MIDI (rp.device.midi-in's queue, which the plan's MIDI policy matches) twice over in bytes,
    // and 512 messages, so the note-offs the node owes after a reconnect (each before the block's own messages, as many
    // as fit) fit beside a full block: the ring is emptied after every block. The messages are not the block's 64
    // twice: a block can bring that many while a reconnect owes far more note-offs than the rest of 128 would take.
    static constexpr std::uint32_t kMessages = 512;
    static constexpr std::uint32_t kBytes = 2 * RP_DEVICE_MIDI_IN_BYTES;
    static_assert(kMessages >= 2 * RP_DEVICE_MIDI_IN_MESSAGES, "a full block twice over");
    // The most one FIFO write carries.
    static constexpr std::uint32_t kBatch = 192;
    // How long a failed volume write waits before it is tried again.
    static constexpr std::uint32_t kVolumeRetryMs = 1000;

    // Where the main loop's writes go, and what it reads: the cart's FIFO (`messages` of the ring's messages end in
    // `data`, 0 for a write that carries only the link's sync, the guard's probe or re-sends, and `firstUs` is when the
    // first of them arrived, or now for a write with none), the cart's expansion-volume register (`again` on a retry
    // of a write that failed), and the cart's back-channel. Each write answers whether it reached the cart: a volume
    // written whose read-back differs is written, not retried (the cart's register is its own business from there;
    // N8Host reads nothing back).
    struct Writer {
        void* ctx;
        bool (*fifo)(void* ctx, const std::uint8_t* data, std::size_t size, std::uint32_t messages,
                     std::uint32_t firstUs);
        bool (*volume)(void* ctx, int value, bool again);
        // The cart's back-channel, without waiting (the box's UsbCdcTransport::read with a 0 timeout: one USB pump, and
        // what it found): up to `size` bytes, 0 when nothing has come. Null: no back-channel, so no framed link and no
        // guard.
        std::size_t (*read)(void* ctx, std::uint8_t* data, std::size_t size);
    };
    // When what the plan pushes arrived: `arrival` answers it for a message the block handed the plan at `frame` with
    // these bytes (MidiStaging::arrival), `now` for one the plan made itself. Microseconds, the low 32 bits.
    // `monotonicUs` is the link's own time, for the sender's and the guard's timeouts: microseconds, all 64 bits,
    // never 0 (the box's time_us_64, read seconds after its boot). Null: no framed link and no guard.
    struct Clock {
        void* ctx;
        std::uint32_t (*now)(void* ctx);
        bool (*arrival)(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us);
        std::uint64_t (*monotonicUs)(void* ctx);
    };

    struct Counts {
        std::uint32_t received = 0;  // pushes, taken or refused
        std::uint32_t made = 0;      // of them, the note-offs rp.n8.nes owed (see above)
        // Pushes refused: no game running, or the menu answered. And what a game that went took with it: the ring at
        // the menu's answer, a re-probe or a disconnect, and the framed window's unacknowledged messages (written, so
        // counted forwarded too).
        std::uint32_t notReady = 0;
        std::uint32_t full = 0;      // pushes refused: the ring full
        std::uint32_t released = 0;  // note-offs releaseNotes queued
        std::uint32_t forwarded = 0; // messages in writes the cart took
        std::uint32_t failed = 0;    // messages in writes that failed
        std::uint32_t sysex = 0;     // of the forwarded, SysEx
        std::uint32_t writes = 0;    // FIFO writes (the link's syncs, the guard's probes and re-sends among them)
        std::uint32_t maxBatch = 0;  // the most messages one write carried
        std::uint32_t resends = 0;   // the framed link's re-send rounds (N8ReliableSender::resends, across connects)
        std::uint32_t resent = 0;    // frames those rounds wrote again
        std::uint32_t unframed = 0;  // messages a framed link could not carry (a SysEx past 40 bytes), dropped

        // What the plan handed the link from its input: its pushes, less the node's own note-offs.
        std::uint32_t fromPlanInput() const { return received - made; }
    };

    // The ring, the books, the framed link's sender and the guard, kept apart from the link in a plain struct the box
    // holds in .bss: a polymorphic object is initialised data (its vtable pointer), and what it held inline would be
    // copied out of flash at boot with it. Every byte of it starts at zero: the sender is raw storage, made in place
    // when a connect's sync is acknowledged (a sender object here would be constant-initialised to its non-zero
    // defaults, about 3.5 KB of .data), and the guard's state is all zeros.
    struct Message {
        std::uint32_t at;  // arrival, microseconds
        std::uint16_t size;
        bool          sysex;
    };
    struct Storage {
        std::uint8_t      bytes[kBytes];
        Message           messages[kMessages];
        std::uint8_t      batch[kBatch];
        rp::n8::CartNotes sounding, owed;
        alignas(rp::N8ReliableSender) unsigned char sender[sizeof(rp::N8ReliableSender)];
        rp::N8MenuGuard   guard;
    };

    constexpr explicit BoxCartLink(Storage& storage) : s_(storage) {}
    void setClock(const Clock& clock) { clock_ = clock; }
    // Whether a connect probes for BlipToaster's reliable link (the default) or streams raw at once, the guard on
    // either way, as N8Link::setLinkAllowed: the box's N8_BOX_LINK=0 build, the raw side of the framed link's latency
    // comparison. Set before a connect.
    void setLinkAllowed(bool on) { linkAllowed_ = on; }

    // rp::n8::CartLink. Connected: a game runs and the guard has not heard the menu.
    bool push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
              double sampleRate) override;
    bool          connected() const override { return connected_ && !s_.guard.atMenu(); }
    std::uint64_t dropped() const override { return std::uint64_t{counts_.notReady} + counts_.full; }
    void          setExpansionVolume(int value) override;
    void          releaseNotes(const rp::n8::CartNotes& notes) override;

    // The box's side. Whether a game is running and the box forwards to it: a connect has the volume written again and
    // the link probed again, and a disconnect drops what is queued and the framed window (counted not ready), as
    // nothing may reach a cart that went away.
    void setConnected(bool connected);
    // The back-channel, the guard's turn, the volume while connected, then the re-sends and the ring, in writes of up
    // to kBatch bytes, until the ring is empty (or the guard holds it, or a framed window is full).
    void drain(const Writer& writer, std::uint32_t nowMs);
    void acquire() { ++holders_; }
    void release();

    // The link is framed: this connect's sync was acknowledged.
    bool framed() const { return sender_ != nullptr; }
    // The guard heard the N8 menu answer its probe: nothing is taken or written, and the box boots the ROM again.
    bool atMenu() const { return s_.guard.atMenu(); }
    // Whether an Edio read now can meet nothing the link is owed (see above). Reads the link's clock.
    bool quietForRead() const;
    // Frames the framed link has written and the cart has not acknowledged (0 on a raw link).
    std::uint32_t outstanding() const { return sender_ ? static_cast<std::uint32_t>(sender_->outstanding()) : 0; }

    const Counts& counts() const { return counts_; }
    int           expansionVolume() const { return volume_; }
    std::uint32_t queued() const { return messageCount_; }
    int           holders() const { return holders_; }

private:
    // The connect's link probe: Pending until its sync is written (at the next drain with a back-channel), Waiting
    // for the ack, Done once framed or raw; Off while disconnected.
    enum class Probe : std::uint8_t { Off, Pending, Waiting, Done };
    // One framed write being filled: its length in s_.batch, the ring's messages it carries (and their SysEx), when the
    // first of them arrived, and the frames it carries, marked sent once it is written.
    struct Out {
        std::uint32_t length = 0, carried = 0, sysex = 0, firstUs = 0, seqCount = 0;
        std::uint8_t  seqs[rp::N8ReliableSender::kWindow] = {};
    };

    bool enqueue(const std::uint8_t* data, std::uint32_t size, std::uint32_t at);
    void book(const std::uint8_t* data, std::size_t size, bool taken);
    void discard();
    std::int64_t  nowNs() const;
    std::uint32_t nowUs() const;
    bool quietAt(std::int64_t now) const;
    bool write(const Writer& writer, const std::uint8_t* data, std::uint32_t size, std::uint32_t messages,
               std::uint32_t firstUs);
    void writeVolume(const Writer& writer, std::uint32_t nowMs);
    void startProbe(const Writer& writer, std::int64_t now);
    void readBackChannel(const Writer& writer, std::int64_t now);
    void drainRaw(const Writer& writer, bool probe);
    void drainFramed(const Writer& writer, std::int64_t now, bool probe);
    void emit(const Writer& writer, Out& out, const std::uint8_t* data, std::size_t size, int seq);
    void flush(const Writer& writer, Out& out);
    void takeFront(std::uint8_t* to);

    Storage& s_;
    Clock    clock_{};
    bool     connected_ = false;

    // The ring (s_.bytes and s_.messages): messages and their bytes, each in a circle of its own.
    std::uint32_t byteHead_ = 0, byteCount_ = 0;
    std::uint32_t messageHead_ = 0, messageCount_ = 0;
    std::uint32_t frontSent_ = 0;  // the bytes of the oldest message already written (a split one)

    int           volume_ = -1;
    bool          volumePending_ = false;
    bool          volumeWaiting_ = false;  // a failed write waits until retryAtMs_
    std::uint32_t retryAtMs_ = 0;
    int           holders_ = 0;

    // The framed link: the sender while framed (in s_.sender), the probe's state, whether its read has seen an ack's
    // F5, and when it gives up listening; whether a connect may probe at all; and whether the last drain had a
    // back-channel (quietForRead's: without one there is nothing a read can meet).
    rp::N8ReliableSender* sender_ = nullptr;
    Probe                 probe_ = Probe::Off;
    bool                  probeSawFrame_ = false;
    std::int64_t          probeUntilNs_ = 0;
    bool                  linkAllowed_ = true;
    bool                  backChannel_ = false;
    bool                  lateProbed_ = false;  // an ack heard on a raw link has had its one fresh probe this connect
    std::int64_t          lastBackNs_ = 0;  // when the back-channel last gave a byte

    Counts counts_{};
};

}  // namespace rp::box

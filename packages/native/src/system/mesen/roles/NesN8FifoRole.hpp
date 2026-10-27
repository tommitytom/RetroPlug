#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "system/mesen/NesEverdriveFifo.hpp"
#include "transport/MidiTypes.hpp"
#include "transport/N8ReliableLink.hpp"

class NesConsole;

// EverDrive N8 Pro FIFO emulator wrapper. Attaches to a NesConsole's memory
// manager so the ROM's reads/writes at $40F0/$40F1 reach the FIFO; sample-offset
// schedules bytes into the FIFO's RX queue so the ROM's `$40F0` polling loop
// sees them. Two feeds share the one queue: host MIDI (`onMidi`, MIDI-framed)
// and a RAW byte stream (`pushBytes`, no framing/cap — e.g. a tracker's own
// sync/locate protocol over the N8 transport).
//
// Always attached when MesenNesSystem activates with a NES ROM — the FIFO is
// benign if the ROM never touches $40F0/$40F1 (most NES homebrew). If non-N8
// NES ROMs become a real concern, gate attachment on an iNES mapper-byte sniffer.
class NesN8FifoRole {
public:
    NesN8FifoRole();
    ~NesN8FifoRole();

    // Register the FIFO with `console`'s memory manager so memory accesses at
    // $40F0/$40F1 are routed through it. Called once, on the audio thread,
    // from MesenNesSystem::onActivate after the ROM has loaded.
    void onAttach(NesConsole& console);

    // The cartridge's clock: CPU cycles and their rate. onAttach takes it from the console; a test without one
    // supplies its own. The FIFO's timing models and the reliable link both run on it.
    void setClock(std::function<std::uint64_t()> cycles, double hz) {
        cycles_  = std::move(cycles);
        clockHz_ = hz;
        fifo_.setCartClock(cycles_, static_cast<std::uint32_t>(clockHz_));
    }

    // Point the emulated SD card at a host directory ("/" on the card). Set at activate from the
    // "mesen" role's `sdRoot`; empty means a scratch dir private to this FIFO (see NesEverdriveFifo).
    void setSdRoot(const std::string& root) { fifo_.setSdRoot(root); }

    // Where a CMD_F_FRD_MEM DMA lands. MesenNesSystem installs one at activate mapping the N8's PI
    // bus onto the core's cartridge memory; without it every DMA reports an Edio error.
    void setCartWriter(std::function<bool(std::uint32_t, const std::uint8_t*, std::size_t)> fn) {
        fifo_.setCartWriter(std::move(fn));
    }

    // Drop the DMA handshake (staged transfer, pending bits, wait deadline). Paired with flushAll
    // wherever the ROM restarts - see NesEverdriveFifo::clearDmaHandshake for why it is separate.
    void clearDmaHandshake() { fifo_.clearDmaHandshake(); }

    // How faithfully the host->NES queue behaves like the cartridge's fifo_a: `depth` 0 = unbounded,
    // `bytesPerSecond` 0 = instant. Both zero is the permissive default. Set at activate from the
    // "mesen" role's `fifo` profile.
    void setFifoProfile(std::uint32_t depth, std::uint32_t bytesPerSecond) {
        fifo_.setFifoProfile(depth, bytesPerSecond);
    }

    // Queue depths + the cumulative dropped-byte count, for a test that needs to see the transport
    // rather than infer it. See NesEverdriveFifo::FifoStats.
    rp::NesEverdriveFifo::FifoStats fifoStats() { return fifo_.stats(); }

    // Take the bytes the ROM has sent host-ward via CMD_USB_WR since the last drain (oldest first).
    // The emulated NES→host back-channel; empty when the ROM has sent nothing.
    std::vector<std::uint8_t> drainUsbTx() { return fifo_.drainTx(); }

    // Audio-thread: QUEUE each event's bytes tagged with the event's intra-block sample offset
    // (ev.frame) instead of delivering immediately. The bytes are released into the FIFO by pumpUntil
    // as the block's sample progress reaches each offset — so host MIDI keeps its timing rather than
    // collapsing to the block start (mirrors SameBoy's serial gate). The ROM polls `$40F1` bit 7
    // (FIFO_MOS_RXF: set = no data); reading `$40F0` pops the next byte. Bytes of one event share its
    // offset; order is preserved (the FIFO is order-sensitive — status byte then data bytes).
    void onMidi(const ::MidiEvent* events, std::uint32_t count);

    // Audio-thread: QUEUE `count` RAW bytes tagged with `offset` (samples from block start), no MIDI
    // framing and no length cap — for a byte protocol carried over the N8 transport (e.g. a tracker's
    // host-sync locate/clock stream). Bytes stay contiguous + ordered; released by pumpUntil alongside
    // any MIDI bytes, so raw + MIDI interleave by offset. `onMidi` is a thin MIDI-framed adapter over this.
    //
    // `flush` empties BOTH queues first (see flushAll) — for a protocol message that is a barrier, like
    // risa's host-sync arm: bytes queued for the position being left must not reach the ROM after it.
    void pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool flush = false);

    // Audio-thread: release every queued byte whose offset has been reached (offset <= sampleOffset)
    // into the FIFO, front-first. Called from MesenNesSystem's step loop with the current intra-block
    // sample offset. The queue is stable-sorted by offset on the first call after any enqueue (all
    // enqueuing happens before the step loop), so bytes released front-first are in true sample order
    // even when two feeds (MIDI + raw) interleave out of enqueue order — subsequent calls are a plain
    // front-drain. With the reliable link on, each message is framed here as it is released, so the
    // sequence numbers follow the order they are released.
    void pumpUntil(std::uint32_t sampleOffset);

    // Audio-thread: at block end, shift still-queued offsets back by `frames` so a byte that didn't
    // fire this block keeps its relative timing next block (already-due bytes clamp to 0).
    void rebase(std::uint32_t frames);

    // BlipToaster's reliable link (transport/N8ReliableLink.hpp): from here on, host MIDI goes out framed, the ROM's
    // acks are read straight off its CMD_USB_WR writes (in emulated time, so drainUsbTx no longer sees them), and a
    // lost frame is re-sent `timing.resendDelayNs` after the ack that reports it - the same recovery a physical N8
    // gets from N8Link. MesenNesSystem turns it on at activate when the ROM's SIG block advertises it.
    void enableLink(rp::N8ReliableSender::Timing timing);
    // Does this iNES image advertise the link? BlipToaster's SIG block (file offset 0x10): "bliptoaster", 3 version
    // bytes, a 4-byte chip tag, then a capability byte - bit 0 is the link, and $FF (older ROMs' padding) is none.
    static bool romAdvertisesLink(const std::vector<std::uint8_t>& rom) {
        static const char kMarker[] = "bliptoaster";
        if (rom.size() < 0x10 + 19) return false;
        for (std::size_t i = 0; i < 11; ++i)
            if (rom[0x10 + i] != static_cast<std::uint8_t>(kMarker[i])) return false;
        const std::uint8_t caps = rom[0x10 + 18];
        return caps != 0xFF && (caps & 0x01);
    }
    bool linkEnabled() const { return link_ != nullptr; }
    const rp::N8ReliableSender* link() const { return link_.get(); }

    // Drop all queued (not-yet-delivered) bytes — on reset, to avoid stale MIDI after a state change. With the link
    // on, its window goes too and the next frame is preceded by a sync: the ROM's sequence may no longer be ours.
    void clear() { pending_.clear(); pendingMsgs_.clear(); needsSort_ = false; if (link_) { link_->reset(); linkDueCycle_ = kNoDue; } }

    // Drop BOTH queues: the not-yet-delivered bytes AND the delivered-but-unread ones sitting in the
    // FIFO's RX queue. `clear()` alone leaves the latter, which the ROM would still read. Used on a
    // barrier message, on core reset, and on savestate load — anywhere the byte stream is discontinuous.
    // The NES→host TX queue is deliberately untouched: this is a barrier in one direction only.
    void flushAll() { clear(); fifo_.clearRx(); }

    // Introspection / tests.
    // Queued raw BYTES; link-mode messages wait unframed in pendingMsgs_ and are not counted (no caller needs them).
    std::size_t pendingCount() const { return pending_.size(); }
    std::size_t fifoRxCount() { return fifo_.rxCount(); }
    // Write a byte to the FIFO as the ROM would (STA $40F0) - an Edio command to the MCU, e.g. a CMD_USB_WR ack.
    void writeFifoByte(std::uint8_t b) { fifo_.WriteRam(0x40F0, b); }
    // Pop the next delivered byte from the FIFO (as the ROM would via $40F0), or -1 if empty.
    int readFifoByte() {
        if (fifo_.ReadRam(0x40F1) == 0x80) return -1;  // bit7 set = no data
        return fifo_.ReadRam(0x40F0);
    }

private:
    struct PendingByte {
        std::uint32_t offset;   // samples from block start
        std::uint8_t  byte;
    };
    std::deque<PendingByte> pending_;
    // Link mode: whole messages wait here unframed, sorted by offset like the bytes, and pumpUntil frames each as it
    // releases it - so the sequence numbers follow release order, not the order the feeds queued them.
    // Invariant: in link mode pending_ stays empty (both feeds go here), so pumpUntil's raw-then-messages drain order
    // never matters. A mid-stream enableLink (bytes already in pending_) would need to revisit that.
    struct PendingMsg {
        std::uint32_t             offset;
        std::vector<std::uint8_t> bytes;
    };
    std::deque<PendingMsg>  pendingMsgs_;
    bool                    needsSort_ = false;  // set on enqueue; pumpUntil stable-sorts by offset once
    rp::NesEverdriveFifo    fifo_;

    // The reliable link (null = raw MIDI). Its clock is the CPU's: cycles_ and clockHz_ come from onAttach.
    static constexpr std::uint64_t kNoDue = ~std::uint64_t{0};
    std::unique_ptr<rp::N8ReliableSender> link_;
    std::function<std::uint64_t()>        cycles_;
    double                                clockHz_      = 0.0;
    std::uint64_t                         linkDueCycle_ = kNoDue;   // next cycle the link has work (poll)
    std::int64_t  nowNs() const;
    void          serviceLink();
};

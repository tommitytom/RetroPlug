#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <vector>

// The host half of BlipToaster's reliable link (bliptoaster src/midi/main.c "The reliable link").
//
// On an NTSC console a DMC-DMA halt that lands on the ROM's `lda $40F0` repeats the read, and the N8 FIFO pops a
// byte per read, so a byte the CPU never sees is simply gone - a dropped drum hit, or a stuck note when it was a
// note-off. The ROM cannot avoid it, so the host frames what it sends and re-sends what does not arrive:
//
//   F4 <seq>            sync: the next frame carries seq (sent before the first frame, and after any reset)
//   F5 <seq> <message> <sum>  a frame: sum = (seq + message bytes) & 0x7F. The ROM reads the whole frame, checks its
//                       structure and sum, and applies it only if it is intact and carries the next seq
//
// and the ROM writes `F5 <next expected>` on the cart's USB back-channel (CMD_USB_WR) whenever its FIFO runs empty
// after anything changed. An ack that names a frame we sent - long enough ago that it should have arrived - means
// that frame was lost: go back and re-send it and everything after it (the ROM skips frames behind a gap, which keeps
// delivery strictly in order, so a note-off can never overtake its note-on). Writes are never repeated by a DMA halt,
// so acks are not lost; a frame destroyed so thoroughly that the ROM saw nothing of it is caught by the timeout.
//
// ONE implementation, two transports: the emulator's cart FIFO (NesN8FifoRole, timed in emulated CPU time) and a
// physical N8 over USB (N8Link, wall-clock time). Which is the point - a user hears the same recovery in the plugin
// as on the cartridge. The transport supplies time and carries the bytes; everything protocol-shaped lives here.
// Not thread-safe: each transport drives its sender from one thread.

namespace rp {

class N8ReliableSender {
public:
    static constexpr std::uint8_t SYNC  = 0xF4;
    static constexpr std::uint8_t FRAME = 0xF5;
    // Outstanding frames before the oldest is given up on (with a re-sync). Under 64, so a 7-bit sequence number
    // always tells an old frame from a new one.
    static constexpr std::size_t kWindow = 48;
    // The longest message a frame may carry: the ROM checks a frame in a 40-byte buffer before applying it
    // (LINK_MSG_MAX in main.c - an N163 wave upload is 37). A longer SysEx is not one the ROM acts on; it is dropped.
    static constexpr std::size_t kMaxMessage = 40;

    struct Timing {
        // An ack can only condemn a frame sent at least this long ago. Emulator: 0 - an ack is written when the FIFO
        // is empty, and everything released before it has been read. Hardware: the host->cart latency plus slack,
        // since an ack can be generated while a frame is still crossing the USB link.
        std::int64_t condemnAfterNs = 0;
        // Wait this long after condemning a frame before re-sending it. Emulator: the hardware round trip, modelled,
        // so a recovered hit lands as late in the plugin as on the cart. Hardware: 0 - the real latency applies.
        std::int64_t resendDelayNs = 0;
        // Nothing heard from the cart for this long while a frame is outstanding: re-send anyway. A safety net only -
        // the ROM answers every burst it reads, damaged or not, so a loss is normally reported by an ack. Measured
        // from the later of the frame going out and the last ack, so a cart that is merely busy is not flooded.
        std::int64_t timeoutNs = 100'000'000;
    };

    // Bytes for the transport. seq >= 0 marks the first byte of a frame, which the transport reports back through
    // onSent() once it is actually on its way (released into the FIFO / written to USB); -1 for anything else.
    using Emit = std::function<void(const std::uint8_t* data, std::size_t n, int seq)>;

    N8ReliableSender() : timing_(Timing{}) {}
    explicit N8ReliableSender(Timing t) : timing_(t) {}
    void setTiming(Timing t) { timing_ = t; }

    // Forget everything in flight and sync again before the next frame - after a transport barrier (a flush, a
    // reset, a savestate load), where the ROM's idea of the sequence and ours may differ.
    void reset() {
        window_.clear();
        synced_ = false;
        status_ = 0;
        msg_.clear();
        inSysex_ = false;
        ackPending_ = false;
        resendAt_ = kNever;
        pendingSync_.clear();
    }

    // Host MIDI in - any run of bytes, possibly several messages, running status, SysEx, real-time. Each complete
    // channel message or SysEx goes out as one frame; real-time bytes (F8-FF) pass through unframed, since the ROM
    // takes them at any point; System Common messages the ROM ignores (F1-F3, F6) are dropped, as are truncated ones.
    void send(const std::uint8_t* data, std::size_t n, const Emit& emit) {
        for (std::size_t i = 0; i < n; ++i) feed(data[i], emit);
    }

    // The frame `seq` has gone out at nowNs.
    void onSent(int seq, std::int64_t nowNs) {
        for (auto& f : window_)
            if (f.seq == seq && f.sentNs < 0) { f.sentNs = nowNs; return; }
    }

    // Back-channel bytes from the cart: the ROM's `F5 <next>` acks (anything else is ignored).
    void onBackChannel(const std::uint8_t* data, std::size_t n, std::int64_t nowNs) {
        if (n) lastAckNs_ = nowNs;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t b = data[i];
            if (ackPending_ && b < 0x80) { onAck(b, nowNs); ackPending_ = false; continue; }
            ackPending_ = (b == FRAME);
        }
    }

    // Re-send whatever is due at nowNs. Returns the next time anything will be due (kNever when nothing is
    // outstanding), so a transport can skip calling until then.
    // `cartBusy`: the transport knows the cart still has unread bytes (the emulator can see its FIFO) - it cannot
    // have answered yet, so the timeout waits.
    std::int64_t poll(std::int64_t nowNs, const Emit& emit, bool cartBusy = false) {
        if (window_.empty()) { resendAt_ = kNever; return kNever; }
        const Frame& oldest = window_.front();
        const bool timedOut = !cartBusy && oldest.sentNs >= 0 && nowNs - lastHeard(oldest) >= timing_.timeoutNs;
        if (nowNs >= resendAt_ || timedOut) {
            resendAt_ = kNever;
            ++resends_;
            if (!pendingSync_.empty()) { emit(pendingSync_.data(), pendingSync_.size(), -1); pendingSync_.clear(); }
            for (auto& f : window_) {
                if (f.sentNs < 0) continue;   // still queued in the transport: it goes out on its own
                f.sentNs = -1;                // re-armed; onSent marks it again when it actually leaves
                emit(f.bytes.data(), f.bytes.size(), f.seq);
            }
        }
        return nextDue();
    }

    std::size_t outstanding() const { return window_.size(); }
    std::uint64_t resends() const { return resends_; }
    std::uint8_t  nextSeq() const { return nextSeq_; }

    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

private:
    struct Frame {
        std::uint8_t              seq;
        std::vector<std::uint8_t> bytes;    // F5 seq message...
        std::int64_t              sentNs;   // -1 = not on its way yet
    };

    // Is `a` before `b` in the 7-bit sequence space (within half the space)?
    static bool before(std::uint8_t a, std::uint8_t b) {
        const std::uint8_t d = static_cast<std::uint8_t>((b - a) & 0x7F);
        return d != 0 && d < 64;
    }

    static std::size_t dataLen(std::uint8_t status) {
        const std::uint8_t k = status & 0xF0;
        return (k == 0xC0 || k == 0xD0) ? 1 : 2;
    }

    void feed(std::uint8_t b, const Emit& emit) {
        if (b >= 0xF8) { emit(&b, 1, -1); return; }   // real-time: unframed, anywhere (the ROM skips or acts on it)
        if (inSysex_) {
            if (b == 0xF7) {
                msg_.push_back(b);
                if (msg_.size() <= kMaxMessage) frame(emit);   // longer: no frame (see kMaxMessage)
                else msg_.clear();
                inSysex_ = false;
                return;
            }
            if (b < 0x80) { msg_.push_back(b); return; }
            inSysex_ = false;   // a status cut the SysEx short: drop it (the ROM would not have applied it either)
            msg_.clear();
        }
        if (b == 0xF0) { status_ = 0; msg_.assign(1, b); inSysex_ = true; return; }
        if (b >= 0xF0) { status_ = 0; msg_.clear(); return; }   // other System Common: the ROM ignores them
        if (b & 0x80) { status_ = b; msg_.assign(1, b); return; }
        if (status_ == 0) return;                       // data with no status: junk
        if (msg_.empty()) msg_.push_back(status_);      // running status: every frame carries its status
        msg_.push_back(b);
        if (msg_.size() == 1 + dataLen(status_)) { frame(emit); }
    }

    void frame(const Emit& emit) {
        if (!synced_) {
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
            synced_ = true;
        }
        if (window_.size() >= kWindow) {
            // The cart has stopped answering (unplugged, crashed, a ROM without the link): give the oldest up and
            // re-point the ROM past them rather than grow without bound.
            window_.clear();
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
        }
        Frame f{ nextSeq_, {}, -1 };
        f.bytes.reserve(3 + msg_.size());
        f.bytes.push_back(FRAME);
        f.bytes.push_back(nextSeq_);
        f.bytes.insert(f.bytes.end(), msg_.begin(), msg_.end());
        unsigned sum = nextSeq_;
        for (auto b : msg_) sum += b;
        f.bytes.push_back(static_cast<std::uint8_t>(sum & 0x7F));   // the ROM checks it before anything applies
        nextSeq_ = static_cast<std::uint8_t>((nextSeq_ + 1) & 0x7F);
        window_.push_back(std::move(f));
        emit(window_.back().bytes.data(), window_.back().bytes.size(), window_.back().seq);
        msg_.clear();
    }

    void onAck(std::uint8_t next, std::int64_t nowNs) {
        while (!window_.empty() && before(window_.front().seq, next)) window_.pop_front();
        if (window_.empty()) { resendAt_ = kNever; return; }
        const Frame& oldest = window_.front();
        if (oldest.seq != next) {
            // The ROM expects something we no longer hold (it rebooted and latched another number, or we reset):
            // point it at our oldest frame and re-send from there.
            const std::uint8_t s[2] = { SYNC, oldest.seq };
            pendingSync_.assign(s, s + 2);
            scheduleResend(nowNs);
            return;
        }
        if (oldest.sentNs >= 0 && nowNs - oldest.sentNs >= timing_.condemnAfterNs) scheduleResend(nowNs);
    }

    std::int64_t lastHeard(const Frame& f) const { return f.sentNs > lastAckNs_ ? f.sentNs : lastAckNs_; }

    void scheduleResend(std::int64_t nowNs) {
        if (resendAt_ == kNever) resendAt_ = nowNs + timing_.resendDelayNs;
    }

    std::int64_t nextDue() const {
        std::int64_t due = resendAt_;
        if (!window_.empty() && window_.front().sentNs >= 0) {
            const std::int64_t t = lastHeard(window_.front()) + timing_.timeoutNs;
            if (t < due) due = t;
        }
        return due;
    }

    Timing                    timing_;
    std::deque<Frame>         window_;
    std::uint8_t              nextSeq_    = 0;
    bool                      synced_     = false;
    std::uint8_t              status_     = 0;
    std::vector<std::uint8_t> msg_;
    bool                      inSysex_    = false;
    bool                      ackPending_ = false;
    std::int64_t              resendAt_   = kNever;
    std::vector<std::uint8_t> pendingSync_;   // a re-sync onAck decided on: poll() sends it ahead of the re-send
    std::uint64_t             resends_    = 0;
    std::int64_t              lastAckNs_  = std::numeric_limits<std::int64_t>::min();
};

}  // namespace rp

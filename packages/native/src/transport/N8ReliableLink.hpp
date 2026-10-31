#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>

#include "util/FixedQueue.hpp"

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
// and the ROM writes `F5 <next expected> <last header read>` on the cart's USB back-channel (CMD_USB_WR) whenever its
// FIFO runs empty after anything changed. An ack that names a frame we sent means it was lost - at once if the ROM has
// read that frame's header or a later one, otherwise once the frame is older than a round trip (on a real cart it may
// still be crossing USB) - so go back and re-send it and everything after it (the ROM skips frames behind a gap, which
// keeps delivery strictly in order, so a note-off can never overtake its note-on). Writes are never repeated by a DMA
// halt, so acks are not lost; the timeout is only a safety net for a cart that stops answering.
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
        // Wait this long after a frame is shown lost before re-sending it. Emulator: the hardware round trip, modelled,
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
        msgLen_ = 0;
        msgTooLong_ = false;
        inSysex_ = false;
        ackState_ = 0;
        resendAt_ = kNever;
        pendingSyncLen_ = 0;
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
    // Each ack is F5 <next> <last>: the seq the ROM expects next, and the seq of the last frame header it read.
    void onBackChannel(const std::uint8_t* data, std::size_t n, std::int64_t nowNs) {
        if (n) lastAckNs_ = nowNs;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t b = data[i];
            if (b == FRAME) { ackState_ = 1; continue; }
            if (b & 0x80) { ackState_ = 0; continue; }
            if (ackState_ == 1) { ackNext_ = b; ackState_ = 2; continue; }
            if (ackState_ == 2) { onAck(ackNext_, b, nowNs); ackState_ = 0; }
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
            if (pendingSyncLen_) { emit(pendingSync_.data(), pendingSyncLen_, -1); pendingSyncLen_ = 0; }
            for (auto& f : window_) {
                if (f.sentNs < 0) continue;   // still queued in the transport: it goes out on its own
                f.sentNs = -1;                // re-armed; onSent marks it again when it actually leaves
                ++framesResent_;
                emit(f.bytes, f.size, f.seq);
            }
        }
        return nextDue();
    }

    std::size_t outstanding() const { return window_.size(); }
    // Round trips (frame out -> the ack that covered it): the fastest, and the latest. -1 until one is measured.
    std::int64_t rttMinNs() const { return rttMinNs_; }
    std::int64_t lastRttNs() const { return lastRttNs_; }
    std::uint64_t resends() const { return resends_; }
    std::uint64_t framesSent() const { return framesSent_; }       // every frame, first sends only
    std::uint64_t framesResent() const { return framesResent_; }   // frames sent again (each re-send of each frame)
    std::uint8_t  nextSeq() const { return nextSeq_; }

    static constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::max();

private:
    // A frame's bytes are inline: a window of 48 frames is a fixed ~3 KiB however many messages a block carries.
    struct Frame {
        std::uint8_t seq;
        std::int64_t sentNs;                    // -1 = not on its way yet
        std::uint32_t size;
        std::uint8_t  bytes[3 + kMaxMessage];   // F5 seq message... sum
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
                msgPush(b);
                if (!msgTooLong_ && msgLen_ <= kMaxMessage) frame(emit);   // longer: no frame (see kMaxMessage)
                msgLen_ = 0;
                msgTooLong_ = false;
                inSysex_ = false;
                return;
            }
            if (b < 0x80) { msgPush(b); return; }
            inSysex_ = false;   // a status cut the SysEx short: drop it (the ROM would not have applied it either)
            msgLen_ = 0;
            msgTooLong_ = false;
        }
        if (b == 0xF0) { status_ = 0; msg_[0] = b; msgLen_ = 1; msgTooLong_ = false; inSysex_ = true; return; }
        // System Common: the ROM ignores them.
        if (b >= 0xF0) { status_ = 0; msgLen_ = 0; msgTooLong_ = false; return; }
        if (b & 0x80) { status_ = b; msg_[0] = b; msgLen_ = 1; return; }
        if (status_ == 0) return;                       // data with no status: junk
        if (msgLen_ == 0) msg_[msgLen_++] = status_;    // running status: every frame carries its status
        msgPush(b);
        if (msgLen_ == 1 + dataLen(status_)) { frame(emit); }
    }

    // Append one byte of the message being collected. Past the longest message a frame may carry the bytes are dropped,
    // which is what a frame would do with them anyway (see kMaxMessage), and the message is marked too long.
    void msgPush(std::uint8_t b) {
        if (msgLen_ < sizeof msg_) msg_[msgLen_++] = b;
        else msgTooLong_ = true;
    }

    void frame(const Emit& emit) {
        if (!synced_) {
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
            synced_ = true;
        }
        if (window_.size() >= kWindow) {
            // The cart has stopped answering (unplugged, crashed, a ROM without the link): give the whole window up
            // and re-point the ROM past it rather than grow without bound.
            window_.clear();
            const std::uint8_t s[2] = { SYNC, nextSeq_ };
            emit(s, 2, -1);
        }
        Frame f{};
        f.seq = nextSeq_;
        f.sentNs = -1;
        f.size = 3 + static_cast<std::uint32_t>(msgLen_);
        f.bytes[0] = FRAME;
        f.bytes[1] = nextSeq_;
        std::memcpy(f.bytes + 2, msg_.data(), msgLen_);
        unsigned sum = nextSeq_;
        for (std::size_t i = 0; i < msgLen_; ++i) sum += msg_[i];
        f.bytes[2 + msgLen_] = static_cast<std::uint8_t>(sum & 0x7F);   // the ROM checks it before anything applies
        ++framesSent_;
        nextSeq_ = static_cast<std::uint8_t>((nextSeq_ + 1) & 0x7F);
        window_.push(f);
        emit(window_.back().bytes, window_.back().size, window_.back().seq);
        msgLen_ = 0;
        msgTooLong_ = false;
    }

    void onAck(std::uint8_t next, std::uint8_t last, std::int64_t nowNs) {
        bool advanced = false;
        while (!window_.empty() && before(window_.front().seq, next)) {
            advanced = true;
            const std::int64_t sent = window_.front().sentNs;
            if (sent >= 0) {
                const std::int64_t rtt = nowNs - sent;
                lastRttNs_ = rtt;
                if (rttMinNs_ < 0 || rtt < rttMinNs_) rttMinNs_ = rtt;
            }
            window_.pop();
        }
        if (window_.empty()) { resendAt_ = kNever; return; }
        if (advanced) resendAt_ = kNever;   // what a pending re-send was for has (partly) arrived: decide afresh
        const Frame& oldest = window_.front();
        if (oldest.seq != next) {
            // The ROM expects something we no longer hold (it rebooted and latched another number, or we reset):
            // point it at our oldest frame and re-send from there.
            const std::uint8_t s[2] = { SYNC, oldest.seq };
            std::memcpy(pendingSync_.data(), s, 2);
            pendingSyncLen_ = 2;
            scheduleResend(nowNs);
            return;
        }
        // The ack names a frame we sent. If the ROM has READ that frame (<last> at or past <next>: its header, a later
        // frame's, or bytes it had to throw away - which it reports as the expected frame), it was lost: re-send now.
        // If not, it is still on its way - on a real cart it may be crossing USB, in the emulator it may land while the
        // ack is being written - and the ack that follows its arrival settles it; the timeout covers a cart that stops
        // answering. (Re-checking on a timer instead re-sent frames that had lost nothing, on a cart and in the
        // emulator alike: the confirming ack comes only once the ROM has processed the frame.)
        if (oldest.sentNs >= 0 && !before(last, next)) scheduleResend(nowNs);
    }

    std::int64_t lastHeard(const Frame& f) const { return f.sentNs > lastAckNs_ ? f.sentNs : lastAckNs_; }

    void scheduleResend(std::int64_t atNs) {
        const std::int64_t t = atNs + timing_.resendDelayNs;
        if (t < resendAt_) resendAt_ = t;
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
    // Fixed storage, sized once here: the sender runs inside a block (the NES role frames a released message as the
    // block's emulation reaches it), so neither the window nor the message being collected may grow there.
    rp::FixedQueue<Frame>     window_{kWindow};
    std::uint8_t              nextSeq_    = 0;
    bool                      synced_     = false;
    std::uint8_t              status_     = 0;
    std::array<std::uint8_t, kMaxMessage + 2> msg_{};   // F0, up to kMaxMessage of payload, F7
    std::size_t               msgLen_     = 0;
    bool                      msgTooLong_ = false;      // a byte past what a frame could carry was dropped
    bool                      inSysex_    = false;
    std::uint8_t              ackState_   = 0;   // 0 idle, 1 after F5, 2 after <next>
    std::uint8_t              ackNext_    = 0;
    std::int64_t              resendAt_   = kNever;
    std::array<std::uint8_t, 2> pendingSync_{};  // a re-sync onAck decided on: poll() sends it ahead of the re-send
    std::size_t               pendingSyncLen_ = 0;
    std::uint64_t             resends_    = 0;
    std::uint64_t             framesSent_   = 0;
    std::uint64_t             framesResent_ = 0;
    std::int64_t              lastAckNs_  = std::numeric_limits<std::int64_t>::min();
    std::int64_t              rttMinNs_   = -1;
    std::int64_t              lastRttNs_  = -1;
};

}  // namespace rp

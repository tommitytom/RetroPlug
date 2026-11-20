#include "BoxCartLink.hpp"

#include <new>
#include <type_traits>

namespace rp::box {

using rp::n8::NoteEvent;
using rp::n8::noteEvent;

// The framed link's sender is made in place in the storage at the connect whose sync was acknowledged, and forgotten,
// never ended, at the next connect, a re-probe or the menu: nothing in it holds anything to give back.
static_assert(std::is_trivially_destructible_v<rp::N8ReliableSender>, "the sender is forgotten, not destroyed");

namespace {

// How long a connect's sync listens for its ack before the link is taken for raw: N8Link::probeLink's 100 ms, which
// covers a busy ROM and the USB round trip with room to spare.
constexpr std::int64_t kLinkProbeNs = 100'000'000;
// How long a cart may say nothing while a frame is outstanding before it is taken for one acknowledging nothing, so a
// read may go ahead of the frame: N8Link's kAckWaitNs.
constexpr std::int64_t kAckWaitNs = 250'000'000;
// The most back-channel reads one drain makes, 64 bytes each: a cart that keeps talking does not hold the pass.
constexpr int kBackReads = 8;
// The link's sync, F4 <seq 0>: a BlipToaster with the link answers it with an ack the moment its FIFO runs empty; any
// other ROM ignores an undefined System Common status.
constexpr std::uint8_t kSync[2] = {rp::N8ReliableSender::SYNC, 0};

}  // namespace

bool BoxCartLink::push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
                       double sampleRate) {
    // The flush flag is the bytes port's barrier, which a FIFO written in order keeps by itself, and the frame and the
    // rate place nothing on a link that writes after the block: the frame only finds when the message arrived.
    (void)flush;
    (void)sampleRate;
    if (size == 0) return true;  // nothing to lose
    ++counts_.received;
    const NoteEvent note = noteEvent(data, size);
    // At the menu the cart reads the FIFO as its commands, so a push there is refused as while no game runs.
    const bool open = connected();
    if (open && note.off && s_.owed.test(note.index)) ++counts_.made;
    bool taken = false;
    if (!open) {
        ++counts_.notReady;
    } else {
        std::uint32_t at = 0;
        if (!clock_.arrival || !clock_.arrival(clock_.ctx, frame, data, size, &at)) at = nowUs();
        taken = size <= kBytes && enqueue(data, static_cast<std::uint32_t>(size), at);
        if (!taken) ++counts_.full;
    }
    book(data, size, taken);
    return taken;
}

// The node's books on its notes, kept from this link's own answers (N8Node.cpp's pushMidi and payOwed).
void BoxCartLink::book(const std::uint8_t* data, std::size_t size, bool taken) {
    const NoteEvent note = noteEvent(data, size);
    if (note.on && taken) {
        s_.sounding.set(note.index);
        s_.owed.reset(note.index);
    } else if (note.off) {
        if (taken) {
            s_.sounding.reset(note.index);
            s_.owed.reset(note.index);
        } else if (s_.sounding.test(note.index)) {
            s_.owed.set(note.index);
        }
    }
}

bool BoxCartLink::enqueue(const std::uint8_t* data, std::uint32_t size, std::uint32_t at) {
    if (messageCount_ == kMessages || size > kBytes - byteCount_) return false;
    std::uint32_t write = (byteHead_ + byteCount_) % kBytes;
    for (std::uint32_t i = 0; i < size; ++i) {
        s_.bytes[write] = data[i];
        write = write + 1 == kBytes ? 0 : write + 1;
    }
    byteCount_ += size;
    s_.messages[(messageHead_ + messageCount_) % kMessages] =
        Message{at, static_cast<std::uint16_t>(size), data[0] == 0xF0};
    ++messageCount_;
    return true;
}

void BoxCartLink::releaseNotes(const rp::n8::CartNotes& notes) {
    const std::uint32_t at = nowUs();
    const bool          open = connected();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes.test(i)) continue;
        s_.sounding.reset(i);
        s_.owed.reset(i);
        const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (i / 128)), static_cast<std::uint8_t>(i % 128),
                                     0};
        if (!open)
            ++counts_.notReady;
        else if (!enqueue(off, sizeof off, at))
            ++counts_.full;
        else
            ++counts_.released;
    }
}

void BoxCartLink::setExpansionVolume(int value) {
    const int clamped = value < 0 ? -1 : (value > 255 ? 255 : value);
    if (clamped == volume_) return;
    volume_ = clamped;
    volumePending_ = true;
    volumeWaiting_ = false;
}

void BoxCartLink::release() {
    if (holders_ > 0 && --holders_ == 0) {
        volume_ = -1;
        volumePending_ = false;
    }
}

void BoxCartLink::setConnected(bool connected) {
    if (connected && !connected_) {
        volumePending_ = true;  // every connect again: a fresh game's register is the N8 OS's
        volumeWaiting_ = false;
        // A fresh game, whose link is probed at the next drain that has a back-channel, under a guard that starts
        // afresh.
        s_.guard.reset();
        probe_ = linkAllowed_ ? Probe::Pending : Probe::Done;
        lateProbed_ = false;
    }
    if (!connected && connected_) {
        discard();
        s_.guard.reset();
        probe_ = Probe::Off;
    }
    connected_ = connected;
}

void BoxCartLink::discard() {
    counts_.notReady += messageCount_;
    // The framed window's messages were written and never acknowledged: the game they were for went with them.
    if (sender_) counts_.notReady += static_cast<std::uint32_t>(sender_->outstanding());
    byteHead_ = byteCount_ = messageHead_ = messageCount_ = frontSent_ = 0;
    sender_ = nullptr;
}

std::int64_t BoxCartLink::nowNs() const {
    return clock_.monotonicUs ? static_cast<std::int64_t>(clock_.monotonicUs(clock_.ctx)) * 1000 : 0;
}

std::uint32_t BoxCartLink::nowUs() const { return clock_.now ? clock_.now(clock_.ctx) : 0; }

bool BoxCartLink::quietForRead() const { return quietAt(nowNs()); }

// N8Link::quietForRead's rule: no guard probe awaiting its answer (the menu's 'k' read inside a reply would be taken
// for its first byte, and lost to the guard), the scan not halfway through an ack (whose tail would be the reply's
// first bytes), and a raw link between two messages, nothing outstanding, or a cart that has said nothing for the
// pause's ack bound since the oldest outstanding frame first went out (one at its menu, or a game that has stopped
// answering). And not while the connect's sync waits for its ack, which the desktop's probe waits out in a call of its
// own and the box's across passes, nor while a probe is booked and not yet written (a late ack's second one).
bool BoxCartLink::quietAt(std::int64_t now) const {
    if (!backChannel_) return true;  // nothing the link reads, so nothing a read can meet
    if (probe_ == Probe::Pending || probe_ == Probe::Waiting) return false;
    if (s_.guard.probeAwaited(now)) return false;
    // A scan halfway through an ack is waiting for its tail, which a cart that has gone silent for the ack bound is not
    // sending: the scan then counts as between acks, so a lone F5 does not hold the reads back for good.
    if (s_.guard.scan().state != 0 && now - lastBackNs_ < kAckWaitNs) return false;
    if (!sender_) return frontSent_ == 0;
    if (sender_->outstanding() == 0) return true;
    const std::int64_t first = sender_->oldestFirstSentNs();
    const std::int64_t heard = sender_->lastHeardNs();
    const std::int64_t spoke = heard > first ? heard : first;
    return spoke != rp::N8ReliableSender::kNever && now - spoke >= kAckWaitNs;
}

bool BoxCartLink::write(const Writer& writer, const std::uint8_t* data, std::uint32_t size, std::uint32_t messages,
                        std::uint32_t firstUs) {
    ++counts_.writes;
    return writer.fifo && writer.fifo(writer.ctx, data, size, messages, firstUs);
}

// The guard's probe on a write of its own: counted apart, so an idle link's probes are not MIDI the stats report.
void BoxCartLink::writeProbe(const Writer& writer) {
    ++counts_.probeOnly;
    write(writer, rp::N8MenuGuard::kProbe, static_cast<std::uint32_t>(sizeof rp::N8MenuGuard::kProbe), 0, nowUs());
}

void BoxCartLink::writeVolume(const Writer& writer, std::uint32_t nowMs) {
    if (!connected_ || !volumePending_ || (volumeWaiting_ && static_cast<std::int32_t>(nowMs - retryAtMs_) < 0))
        return;
    if (volume_ < 0 || (writer.volume && writer.volume(writer.ctx, volume_, volumeWaiting_))) {
        volumePending_ = volumeWaiting_ = false;
    } else {
        volumeWaiting_ = true;
        retryAtMs_ = nowMs + kVolumeRetryMs;
    }
}

void BoxCartLink::drain(const Writer& writer, std::uint32_t nowMs) {
    backChannel_ = connected_ && writer.read != nullptr && clock_.monotonicUs != nullptr;
    if (!backChannel_) {
        // Nothing can be heard, so nothing is framed or guarded: the volume, then the ring, raw.
        writeVolume(writer, nowMs);
        drainRaw(writer, false);
        return;
    }
    const std::int64_t now = nowNs();
    if (probe_ == Probe::Pending) startProbe(writer, now);
    // What the cart has said: the probe's ack, the sender's acks, the menu's 'k'.
    readBackChannel(writer, now);
    if (probe_ == Probe::Waiting && now >= probeUntilNs_) probe_ = Probe::Done;  // nothing answered: raw
    // The ring waits for the probe's verdict (a sync whose write failed goes out again at the next drain).
    if (probe_ != Probe::Done) return;
    const rp::N8MenuGuard::Turn turn = s_.guard.turn(now, sender_, frontSent_ == 0);
    if (turn.enteredMenu) {
        // The menu: what the ring held and the window go, counted. Nothing more is written: the box leaves the link at
        // once, and a probe now would be answered into the boot's own handshake.
        discard();
        return;
    }
    // The guard never turns here at the menu (the main loop disconnects the link the moment it enters, and boots the
    // staged ROM), so it never asks for the config block and never leaves the menu: only a stalled window comes back.
    if (turn.reprobeLink) {
        // A stalled window two probes heard nothing to (a game without the link): the link is probed again at the next
        // drain, and a game with it re-syncs framed.
        discard();
        sender_ = nullptr;
        probe_ = Probe::Pending;
        return;
    }
    // The volume, as N8Link's OnControl runs it: after the guard's turn, whose probe just booked makes the turn not
    // quiet, and only on a quiet one, since its read-back would take an ack or the menu's answer for the register.
    if (quietAt(now)) writeVolume(writer, nowMs);
    if (turn.hold) {
        // Nothing new and no re-send: the window has stopped moving and the guard is asking, or the cart is at its
        // menu. Only the probe, alone.
        if (turn.writeProbe) writeProbe(writer);
        return;
    }
    if (sender_)
        drainFramed(writer, now, turn.writeProbe);
    else
        drainRaw(writer, turn.writeProbe);
}

void BoxCartLink::startProbe(const Writer& writer, std::int64_t now) {
    // Whatever the back-channel already holds goes before the sync, as N8Link::probeLink flushes the port first: bytes
    // that spell an ack's F5 <next> would turn the framed link on for a ROM that reads no frames. Read and dropped
    // without waiting, rather than flushed (the box's flush waits for 20 ms of quiet, which would hold the loop), and
    // the guard's scan starts afresh with the input.
    std::uint8_t stale[64];
    for (int i = 0; i < kBackReads; ++i)
        if (writer.read(writer.ctx, stale, sizeof stale) < sizeof stale) break;
    s_.guard.scan() = {};
    sender_ = nullptr;
    if (!write(writer, kSync, static_cast<std::uint32_t>(sizeof kSync), 0, nowUs())) return;
    probe_ = Probe::Waiting;
    probeSawFrame_ = false;
    probeUntilNs_ = now + kLinkProbeNs;
}

void BoxCartLink::readBackChannel(const Writer& writer, std::int64_t now) {
    std::uint8_t buf[64];
    for (int r = 0; r < kBackReads; ++r) {
        const std::size_t n = writer.read(writer.ctx, buf, sizeof buf);
        if (n == 0) return;
        lastBackNs_ = now;
        if (probe_ == Probe::Waiting) {
            // The probe's answer, F5 <next> (N8Link::probeLink's test): the link is framed from here, with a sender of
            // its own. The rest of this read goes with the ack, as the desktop's probe leaves it unread: the ack's
            // <last> was in it, or is still to come, and then the guard's scan must read it as the ack's (a 0x6B there
            // is not the menu's answer).
            for (std::size_t i = 0; i < n; ++i) {
                if (probeSawFrame_ && buf[i] < 0x80) {
                    rp::N8ReliableSender::Timing t;
                    t.resendDelayNs = 0;  // the real round trip already applies
                    sender_ = new (s_.sender) rp::N8ReliableSender(t);
                    probe_ = Probe::Done;
                    s_.guard.scan().state = i + 1 < n ? 0 : 2;
                    return;
                }
                probeSawFrame_ = buf[i] == rp::N8ReliableSender::FRAME;
            }
        } else {
            if (sender_) sender_->onBackChannel(buf, n, now);
            const std::uint64_t acks = s_.guard.acksHeard();
            s_.guard.hear(buf, n);
            if (!sender_ && probe_ == Probe::Done && linkAllowed_ && !lateProbed_ && frontSent_ == 0 &&
                s_.guard.acksHeard() != acks) {
                // An ack on a link taken for raw: the ROM speaks the link and answered the connect's sync after the
                // probe's window (BlipToaster read it late), and is in its link mode, which drops unframed messages.
                // The link is probed again, once for a connect, so it goes framed; not inside a split SysEx, half of
                // which is already off the ring: the next ack, after the message is finished, does it.
                lateProbed_ = true;
                probe_ = Probe::Pending;
                return;
            }
        }
        if (n < sizeof buf) return;
    }
}

void BoxCartLink::drainRaw(const Writer& writer, bool probe) {
    if (probe && messageCount_ == 0) {
        writeProbe(writer);
        return;
    }
    while (messageCount_ != 0) {
        // One write: the guard's probe first when one is due (a drain starts between two messages, and every message
        // the box writes carries its own status, so none after the probe runs on a status the probe ended), then the
        // messages that fit whole, from the oldest; one longer than a write is split across writes.
        std::uint32_t length = 0, carried = 0, sysex = 0;
        if (probe) {
            for (const std::uint8_t b : rp::N8MenuGuard::kProbe) s_.batch[length++] = b;
            probe = false;
        }
        const std::uint32_t firstUs = s_.messages[messageHead_].at;
        while (messageCount_ != 0 && length < kBatch) {
            const Message&      m = s_.messages[messageHead_];
            const std::uint32_t left = m.size - frontSent_;
            if (frontSent_ == 0 && m.size <= kBatch && length + m.size > kBatch) break;  // whole, in the next write
            const std::uint32_t take = left < kBatch - length ? left : kBatch - length;
            for (std::uint32_t i = 0; i < take; ++i) {
                s_.batch[length++] = s_.bytes[byteHead_];
                byteHead_ = byteHead_ + 1 == kBytes ? 0 : byteHead_ + 1;
            }
            byteCount_ -= take;
            frontSent_ += take;
            if (frontSent_ < m.size) break;  // the rest of it goes in the next write
            ++carried;
            if (m.sysex) ++sysex;
            frontSent_ = 0;
            messageHead_ = messageHead_ + 1 == kMessages ? 0 : messageHead_ + 1;
            --messageCount_;
        }
        const bool ok = write(writer, s_.batch, length, carried, firstUs);
        if (ok) {
            counts_.forwarded += carried;
            counts_.sysex += sysex;
        } else {
            counts_.failed += carried;
        }
        if (carried > counts_.maxBatch) counts_.maxBatch = carried;
        // A write that failed ends the drain: the cart may be gone, and what is left waits in the ring for the next
        // drain (or the disconnect, which counts it not ready), rather than failing one write after another.
        if (!ok) return;
    }
}

void BoxCartLink::drainFramed(const Writer& writer, std::int64_t now, bool probe) {
    Out        out;
    const auto emitTo = [&](const std::uint8_t* data, std::size_t size, int seq) {
        emit(writer, out, data, size, seq);
    };
    if (probe) emitTo(rp::N8MenuGuard::kProbe, sizeof rp::N8MenuGuard::kProbe, -1);
    // The re-sends due at `now`, the guard's own turn time, so none can slip past a hold the guard decided by a later
    // reading of the clock (N8Link::serviceLinkResends).
    const std::uint64_t resends = sender_->resends(), resent = sender_->framesResent();
    sender_->poll(now, emitTo);
    counts_.resends += static_cast<std::uint32_t>(sender_->resends() - resends);
    counts_.resent += static_cast<std::uint32_t>(sender_->framesResent() - resent);
    // Then the ring, each message framed as one of its own: they are whole here (only the raw path splits one, and it
    // finishes it in the drain that began it). No more than the window less one is ever outstanding: acks are read
    // only at a drain's start, so framing a backlog (the ring after a guard hold, a block after the loop was held)
    // to empty would make the sender give its window up at the 49th frame and re-sync, and a frame lost before it
    // would then be gone for good. What does not fit waits in the ring for the next drain, which reads the acks first.
    std::uint8_t message[rp::N8ReliableSender::kMaxMessage];
    while (messageCount_ != 0 && sender_->outstanding() < rp::N8ReliableSender::kWindow - 1) {
        const Message m = s_.messages[messageHead_];
        if (m.size > rp::N8ReliableSender::kMaxMessage) {
            // Longer than a frame carries (BlipToaster's longest SysEx is 37 bytes): dropped, as the sender drops it.
            takeFront(nullptr);
            ++counts_.unframed;
            continue;
        }
        takeFront(message);
        const std::uint64_t sentBefore = sender_->framesSent();
        sender_->sendMessage(message, m.size, emitTo);
        // Framed, or a real-time byte, which goes unframed (the ROM takes it anywhere). Anything else the sender
        // dropped without writing a byte of it (a System Common message, which the ROM ignores).
        if (sender_->framesSent() == sentBefore && !(m.size == 1 && message[0] >= 0xF8)) {
            ++counts_.unframed;
            continue;
        }
        if (out.carried == 0) out.firstUs = m.at;
        ++out.carried;
        if (m.sysex) ++out.sysex;
    }
    flush(writer, out);
}

void BoxCartLink::emit(const Writer& writer, Out& out, const std::uint8_t* data, std::size_t size, int seq) {
    // A frame is never split across writes (a sync and a frame are at most 2 + 43 bytes, far under a write): what does
    // not fit goes out first.
    if (out.length + size > kBatch) flush(writer, out);
    for (std::size_t i = 0; i < size; ++i) s_.batch[out.length++] = data[i];
    if (seq >= 0 && out.seqCount < rp::N8ReliableSender::kWindow)
        out.seqs[out.seqCount++] = static_cast<std::uint8_t>(seq);
}

void BoxCartLink::flush(const Writer& writer, Out& out) {
    if (out.length == 0) return;
    const std::int64_t sentNs = nowNs();
    const bool         ok = write(writer, s_.batch, out.length, out.carried, out.carried ? out.firstUs : nowUs());
    // A frame has left once its write was made, taken or not (N8Link::writeFramed marks them before its write): a
    // failed write's frames are re-sent on the timeout rather than left waiting to be sent.
    for (std::uint32_t i = 0; i < out.seqCount; ++i) sender_->onSent(out.seqs[i], sentNs);
    if (ok) {
        counts_.forwarded += out.carried;
        counts_.sysex += out.sysex;
    } else {
        counts_.failed += out.carried;
    }
    if (out.carried > counts_.maxBatch) counts_.maxBatch = out.carried;
    out = Out{};
}

// The oldest message off the ring, whole, its bytes copied to `to` (null: dropped).
void BoxCartLink::takeFront(std::uint8_t* to) {
    const std::uint32_t size = s_.messages[messageHead_].size;
    for (std::uint32_t i = 0; i < size; ++i) {
        if (to) to[i] = s_.bytes[byteHead_];
        byteHead_ = byteHead_ + 1 == kBytes ? 0 : byteHead_ + 1;
    }
    byteCount_ -= size;
    messageHead_ = messageHead_ + 1 == kMessages ? 0 : messageHead_ + 1;
    --messageCount_;
}

}  // namespace rp::box

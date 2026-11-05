#include "BoxCartLink.hpp"

namespace rp::box {

using rp::n8::NoteEvent;
using rp::n8::noteEvent;

bool BoxCartLink::push(std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush,
                       double sampleRate) {
    // The flush flag is the bytes port's barrier, which a FIFO written in order keeps by itself, and the frame and the
    // rate place nothing on a link that writes after the block: the frame only finds when the message arrived.
    (void)flush;
    (void)sampleRate;
    if (size == 0) return true;  // nothing to lose
    ++counts_.received;
    const NoteEvent note = noteEvent(data, size);
    if (connected_ && note.off && s_.owed.test(note.index)) ++counts_.made;
    bool taken = false;
    if (!connected_) {
        ++counts_.notReady;
    } else {
        std::uint32_t at = 0;
        if (!clock_.arrival || !clock_.arrival(clock_.ctx, frame, data, size, &at))
            at = clock_.now ? clock_.now(clock_.ctx) : 0;
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
    const std::uint32_t at = clock_.now ? clock_.now(clock_.ctx) : 0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes.test(i)) continue;
        s_.sounding.reset(i);
        s_.owed.reset(i);
        const std::uint8_t off[3] = {static_cast<std::uint8_t>(0x80 | (i / 128)), static_cast<std::uint8_t>(i % 128),
                                     0};
        if (!connected_)
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
    }
    if (!connected && connected_) discard();
    connected_ = connected;
}

void BoxCartLink::discard() {
    counts_.notReady += messageCount_;
    byteHead_ = byteCount_ = messageHead_ = messageCount_ = frontSent_ = 0;
}

void BoxCartLink::drain(const Writer& writer, std::uint32_t nowMs) {
    if (connected_ && volumePending_ && (!volumeWaiting_ || static_cast<std::int32_t>(nowMs - retryAtMs_) >= 0)) {
        if (volume_ < 0 || (writer.volume && writer.volume(writer.ctx, volume_, volumeWaiting_))) {
            volumePending_ = volumeWaiting_ = false;
        } else {
            volumeWaiting_ = true;
            retryAtMs_ = nowMs + kVolumeRetryMs;
        }
    }
    while (messageCount_ != 0) {
        // One write: the messages that fit whole, from the oldest; one longer than a write is split across writes.
        std::uint32_t length = 0, carried = 0, sysex = 0;
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
        const bool ok = writer.fifo && writer.fifo(writer.ctx, s_.batch, length, carried, firstUs);
        ++counts_.writes;
        if (ok) {
            counts_.forwarded += carried;
            counts_.sysex += sysex;
        } else {
            counts_.failed += carried;
        }
        if (carried > counts_.maxBatch) counts_.maxBatch = carried;
    }
}

}  // namespace rp::box

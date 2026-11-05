#include "MidiStaging.hpp"

#include <cstring>

namespace rp::box {

void MidiStaging::init(SysexHook hook, void* hookCtx, FrameOf frameOf, void* frameCtx) {
    hook_ = hook;
    hookCtx_ = hookCtx;
    frameOf_ = frameOf;
    frameCtx_ = frameCtx;
    interface_ = rp_device_midi_in{this, &nextFn};
    reset();
}

void MidiStaging::reset() {
    midi_parser_init(&parser_, &onMessage, this);
    midi_parser_set_sysex_sink(&parser_, &onSysex);
    sysexOpen_ = sysexOver_ = false;
    sysexSize_ = 0;
    count_ = used_ = read_ = asked_ = 0;
    sysexHeld_ = false;
    shortSize_ = 0;
}

void MidiStaging::clearStaged() {
    counts_.untaken += count_ - read_ + (sysexHeld_ ? 1u : 0u) + (shortSize_ != 0 ? 1u : 0u);
    count_ = used_ = read_ = asked_ = 0;
    sysexHeld_ = false;
    shortSize_ = 0;
}

void MidiStaging::endBlock() {
    counts_.untaken += count_ - read_;
    count_ = used_ = read_ = asked_ = 0;
    flushPending();
}

bool MidiStaging::feed(std::uint8_t byte, std::uint32_t arrivedUs) {
    if (!flushPending()) return false;
    // Whatever this byte completes: a message of up to three bytes, or (a status byte inside a SysEx) the SysEx and at
    // most one such message after it, the two held while the staging has no room (sysexDone, stage).
    if (!fits(3)) return false;
    now_ = arrivedUs;
    midi_parser_byte(&parser_, byte);
    return true;
}

// A message the parser completed, rebuilt with its status byte: a channel message is 3 bytes but for a program change
// and channel pressure (2), real time 1, and system common as long as its status says (the song position 3, the time
// code and song select 2, the rest 1).
void MidiStaging::onMessage(const midi_message* m, void* user) {
    auto&              self = *static_cast<MidiStaging*>(user);
    const std::uint8_t data[3] = {m->status, m->data0, m->data1};
    std::uint32_t      size = 1;
    if (m->status < 0xF0) {
        ++self.counts_.channelMessages;
        size = m->type == MIDI_PROGRAM_CHANGE || m->type == MIDI_CHANNEL_PRESSURE ? 2 : 3;
    } else if (m->status == 0xF2) {
        size = 3;
    } else if (m->status == 0xF1 || m->status == 0xF3) {
        size = 2;
    }
    self.stage(data, size, self.now_);
}

void MidiStaging::onSysex(std::uint8_t b, void* user) {
    auto& self = *static_cast<MidiStaging*>(user);
    if (b == 0xF0) {
        self.sysexOpen_ = true;
        self.sysexOver_ = false;
        self.sysexSize_ = 0;
    }
    if (!self.sysexOpen_) return;
    if (self.sysexSize_ < kSysexBytes)
        self.sysex_[self.sysexSize_++] = b;
    else
        self.sysexOver_ = true;
    if (b == 0xF7) {
        self.sysexOpen_ = false;
        self.sysexDone();
    }
}

void MidiStaging::sysexDone() {
    if (sysexOver_) {
        ++counts_.tooLong;
        return;
    }
    if (hook_ && hook_(hookCtx_, sysex_, sysexSize_)) {
        ++counts_.consumed;
        return;
    }
    sysexAt_ = now_;
    if (fits(sysexSize_))
        place(sysex_, sysexSize_, sysexAt_);
    else
        sysexHeld_ = true;  // feed() takes nothing more until a block has made room for it
}

// A short message, staged, or held behind a held SysEx (or when a SysEx the same byte completed took the room): feed()
// checked for its room before the byte, and one byte completes at most one short message.
void MidiStaging::stage(const std::uint8_t* data, std::uint32_t size, std::uint32_t at) {
    if (!sysexHeld_ && fits(size)) {
        place(data, size, at);
        return;
    }
    std::memcpy(short_, data, size);
    shortSize_ = size;
    shortAt_ = at;
}

void MidiStaging::place(const std::uint8_t* data, std::uint32_t size, std::uint32_t at) {
    entries_[count_++] = Entry{at, 0, static_cast<std::uint16_t>(used_), static_cast<std::uint16_t>(size)};
    std::memcpy(bytes_ + used_, data, size);
    used_ += size;
}

bool MidiStaging::flushPending() {
    if (sysexHeld_) {
        if (!fits(sysexSize_)) return false;
        place(sysex_, sysexSize_, sysexAt_);
        sysexHeld_ = false;
    }
    if (shortSize_ != 0) {
        if (!fits(shortSize_)) return false;
        place(short_, shortSize_, shortAt_);
        shortSize_ = 0;
    }
    return true;
}

bool MidiStaging::nextFn(void* ctx, std::uint32_t frames, rp_device_midi_message* out) {
    auto& self = *static_cast<MidiStaging*>(ctx);
    if (self.read_ == self.count_) return false;  // the host empties the staging after the block (endBlock)
    Entry&        e = self.entries_[self.read_++];
    std::uint32_t frame = self.frameOf_ ? self.frameOf_(self.frameCtx_, e.at) : 0;
    if (frame >= frames) frame = frames != 0 ? frames - 1 : 0;
    e.frame = frame;
    *out = rp_device_midi_message{frame, e.size, self.bytes_ + e.offset};
    ++self.counts_.handed;
    return true;
}

bool MidiStaging::arrival(std::uint32_t frame, const std::uint8_t* data, std::size_t size, std::uint32_t* us) {
    while (asked_ < read_ && entries_[asked_].frame < frame) ++asked_;
    for (std::uint32_t i = asked_; i < read_ && entries_[i].frame == frame; ++i) {
        const Entry& e = entries_[i];
        if (e.size != size || std::memcmp(bytes_ + e.offset, data, size) != 0) continue;
        *us = e.at;
        asked_ = i + 1;
        return true;
    }
    return false;
}

}  // namespace rp::box

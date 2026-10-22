#include "system/mesen/roles/NesN8FifoRole.hpp"

#include <algorithm>
#include <cstdio>

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesConstants.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/NesMemoryManager.h"

NesN8FifoRole::NesN8FifoRole() = default;
NesN8FifoRole::~NesN8FifoRole() = default;

void NesN8FifoRole::onAttach(NesConsole& console) {
    auto* memMgr = console.GetMemoryManager();
    if (!memMgr) {
        std::fprintf(stderr, "[NesN8FifoRole] no memory manager available; attach failed\n");
        return;
    }
    memMgr->RegisterIODevice(&fifo_);

    // Give the cartridge a sense of time. Two models need it: the DMA handshake's transfer window,
    // and the host wire's delivery rate. Neither is instantaneous on the device, and both show up in
    // ROM-visible timing - the DMA in how long its wait loop spins, the wire in whether a copy loop
    // is ever starved mid-structure. Without a clock both stay instant, and anything measured
    // against them comes out optimistic. The clock rate is read from the console rather than
    // assumed, so a modelled wall duration holds on PAL and NTSC alike.
    if (NesCpu* cpu = console.GetCpu()) {
        setClock([cpu] { return cpu->GetCycleCount(); }, NesConstants::GetClockRate(console.GetRegion()));
    }

    std::fprintf(stderr, "[NesN8FifoRole] FIFO attached at $40F0/$40F1/$40FF\n");
}

void NesN8FifoRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool flush) {
    if (data == nullptr || count == 0) return;
    // A barrier message drops everything still in flight first, in BOTH queues, so it can't be preceded
    // by bytes belonging to the stream position it replaces.
    if (flush) flushAll();
    if (link_) {
        // A link-capable ROM ignores unframed MIDI, and this is how a long SysEx (anything over a MidiEvent's 4
        // bytes) or a multi-message run arrives - so it is framed like onMidi's. Those ROMs speak no raw protocol.
        link_->send(data, count, [&](const std::uint8_t* d, std::size_t n, int seq) {
            for (std::size_t b = 0; b < n; ++b) pending_.push_back({ offset, d[b], b == 0 ? seq : -1 });
        });
        needsSort_ = true;
        return;
    }
    // Queue (don't deliver) each byte at `offset`. Contiguous + ordered, so pumpUntil releases the whole
    // run together once its offset is reached. No framing/cap — the raw twin of onMidi.
    for (std::size_t b = 0; b < count; ++b) {
        pending_.push_back({ offset, data[b] });
    }
    needsSort_ = true;  // a second feed may enqueue after MIDI with an earlier offset — re-sort before draining
}

void NesN8FifoRole::enableLink(rp::N8ReliableSender::Timing timing) {
    link_ = std::make_unique<rp::N8ReliableSender>(timing);
    linkDueCycle_ = kNoDue;
    // The ROM's acks, read the moment it writes them (with the FIFO's lock held - so only the sender is touched
    // here; any re-send it decides on goes out from pumpUntil, on the next instruction).
    fifo_.setTxSink([this](const std::uint8_t* data, std::size_t n) {
        link_->onBackChannel(data, n, nowNs());
        linkDueCycle_ = 0;
    });
}

std::int64_t NesN8FifoRole::nowNs() const {
    if (!cycles_ || clockHz_ <= 0.0) return 0;
    return static_cast<std::int64_t>(static_cast<double>(cycles_()) * 1e9 / clockHz_);
}

void NesN8FifoRole::serviceLink() {
    const std::int64_t now = nowNs();
    // A re-send goes straight into the FIFO: it is due NOW (the sender timed it), and it must not wait behind
    // bytes still queued for later in the block.
    // The cart is still working through what it has: it cannot have answered yet, so the timeout holds off.
    const bool busy = fifo_.rxCount() > 0;
    std::int64_t due = link_->poll(now, [&](const std::uint8_t* data, std::size_t n, int seq) {
        for (std::size_t i = 0; i < n; ++i) fifo_.pushByte(data[i]);
        if (seq >= 0) link_->onSent(seq, now);
    }, busy);
    if (busy && due <= now) due = now + 1'000'000;   // look again in 1 ms rather than on every instruction
    linkDueCycle_ = (due == rp::N8ReliableSender::kNever || clockHz_ <= 0.0)
        ? kNoDue
        : static_cast<std::uint64_t>(static_cast<double>(due) * clockHz_ / 1e9);
}

void NesN8FifoRole::onMidi(const ::MidiEvent* events, std::uint32_t count) {
    if (events == nullptr || count == 0) return;
    if (link_) {
        // Framed: each event's bytes become frames (plus a sync ahead of the first), queued at the event's offset;
        // a frame's first byte carries its seq so pumpUntil can tell the sender when it actually left.
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& ev = events[i];
            const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
            link_->send(ev.data, size, [&](const std::uint8_t* data, std::size_t n, int seq) {
                for (std::size_t b = 0; b < n; ++b)
                    pending_.push_back({ ev.frame, data[b], b == 0 ? seq : -1 });
            });
        }
        needsSort_ = true;
        return;
    }
    // MIDI-framed adapter over pushBytes: each event's bytes share ev.frame (clamped to the inline size).
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& ev = events[i];
        const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
        pushBytes(ev.frame, ev.data, size);
    }
}

void NesN8FifoRole::pumpUntil(std::uint32_t sampleOffset) {
    if (needsSort_) {
        // Stable so bytes of one message (same offset, contiguous) stay together + in order; runs once per
        // block (all enqueuing precedes the step loop), after which this is a plain front-drain.
        std::stable_sort(pending_.begin(), pending_.end(),
                         [](const PendingByte& a, const PendingByte& b) { return a.offset < b.offset; });
        needsSort_ = false;
    }
    while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
        fifo_.pushByte(pending_.front().byte);
        if (link_ && pending_.front().seq >= 0) {
            link_->onSent(pending_.front().seq, nowNs());
            if (linkDueCycle_ == kNoDue) linkDueCycle_ = 0;   // a frame in flight: its timeout needs arming
        }
        pending_.pop_front();
    }
    // Called before every instruction, so this is the link's clock tick: one compare unless work is due.
    if (linkDueCycle_ != kNoDue && cycles_ && cycles_() >= linkDueCycle_) serviceLink();
}

void NesN8FifoRole::rebase(std::uint32_t frames) {
    for (auto& e : pending_) {
        e.offset = (e.offset > frames) ? e.offset - frames : 0;
    }
}

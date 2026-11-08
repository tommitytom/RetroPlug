#include "system/mesen/roles/NesN8FifoRole.hpp"

#include <cstdio>
#include <cstring>

#include "Core/NES/NesConsole.h"
#include "Core/NES/NesConstants.h"
#include "Core/NES/NesCpu.h"
#include "Core/NES/NesMemoryManager.h"

NesN8FifoRole::NesN8FifoRole() { bytes_.reserve(kPendingBytes); }
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

void NesN8FifoRole::queue(std::uint32_t offset, const std::uint8_t* data, std::size_t count) {
    if (count == 0) return;
    const auto start = static_cast<std::uint32_t>(bytes_.size());
    if (bytes_.size() + count > kPendingBytes ||
        !pending_.push({ offset, start, static_cast<std::uint32_t>(count) })) {
        pendingDropped_ += count;
        return;
    }
    bytes_.insert(bytes_.end(), data, data + count);
    acceptedBytes_ += count;
    needsSort_ = true;  // a second feed may enqueue after this one with an earlier offset - re-sort before draining
}

void NesN8FifoRole::pushBytes(std::uint32_t offset, const std::uint8_t* data, std::size_t count, bool flush) {
    if (data == nullptr || count == 0) return;
    // A barrier message drops everything still in flight first, so it can't be preceded by bytes belonging to the
    // stream position it replaces.
    if (flush) flushAll();
    // One run per message, whichever way it is delivered: the bytes go into the FIFO in order as their offset comes due
    // (raw mode), or are framed and sent as one message (the link). A multi-message run survives the framing; a SysEx
    // longer than a frame may carry (kMaxMessage) does not, and the sender drops it rather than split it.
    queue(offset, data, count);
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
        if (rp::fifoTraceEnabled() && seq >= 0) std::fprintf(stderr, "[fifo] RESEND seq %02X\n", seq);
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
    // Each event is one MIDI-framed run at its frame: pushed into the FIFO in order when it comes due (raw mode), or
    // framed as one message then (the link). A message longer than a MidiEvent holds is clamped to kDataSize here, in
    // both modes (the sender would drop a frame over kMaxMessage); the node sends such a run down pushCoreBytes
    // instead.
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& ev = events[i];
        const std::uint32_t size = ev.size > ::MidiEvent::kDataSize ? ::MidiEvent::kDataSize : ev.size;
        queue(ev.frame, ev.data, size);
    }
}

std::size_t NesN8FifoRole::pendingCount() const {
    if (link_) return 0;  // a message waits unframed and unframed bytes are not what a caller counts
    std::size_t bytes = 0;
    for (const auto& run : pending_) bytes += run.count;
    return bytes;
}

void NesN8FifoRole::pumpUntil(std::uint32_t sampleOffset) {
    if (needsSort_) {
        // Stable so two runs at one offset keep their enqueue order (the sender's parser relies on it: running
        // status, a SysEx split across pushes) with any carried in from an earlier block ahead of them, and so a run's
        // own bytes stay together. Runs once per block (all enqueuing precedes the step loop), after which this is a
        // plain front-drain. A split SysEx holds together only at ONE offset: halves at different offsets with a
        // message released between them are cut by the sender's parser (no caller splits one that way).
        sortScratch_.sort(pending_.begin(), pending_.end(),
                          [](const PendingRun& a, const PendingRun& b) { return a.offset < b.offset; });
        needsSort_ = false;
    }
    while (!pending_.empty() && pending_.front().offset <= sampleOffset) {
        const PendingRun run = pending_.front();
        const std::uint8_t* const data = bytes_.data() + run.start;
        if (link_) {
            // A message is framed the moment it is released, so its sequence number is the next one the ROM sees -
            // whichever feed queued it, in whatever order, and however many blocks it was carried by rebase.
            const std::int64_t now = nowNs();
            link_->send(data, run.count, [&](const std::uint8_t* bytes, std::size_t n, int seq) {
                for (std::size_t b = 0; b < n; ++b) fifo_.pushByte(bytes[b]);
                if (seq >= 0) {
                    link_->onSent(seq, now);
                    if (linkDueCycle_ == kNoDue) linkDueCycle_ = 0;   // a frame in flight: its timeout needs arming
                }
            });
        } else {
            // Raw mode: every byte into the FIFO, in order, as the run comes due.
            for (std::uint32_t b = 0; b < run.count; ++b) fifo_.pushByte(data[b]);
        }
        pending_.pop();
    }
    // Called before every instruction, so this is the link's clock tick: one compare unless work is due.
    if (linkDueCycle_ != kNoDue && cycles_ && cycles_() >= linkDueCycle_) serviceLink();
}

void NesN8FifoRole::rebase(std::uint32_t frames) {
    for (auto& run : pending_) {
        run.offset = (run.offset > frames) ? run.offset - frames : 0;
    }
    // Hand back the bytes of the runs that were released. They sit in front of the live ones in `bytes_`, but the
    // live runs are in queue order, which a sort by offset may have permuted, so the arena's bytes are not in queue
    // order: compacting front-to-back in queue order would overwrite a run it has not copied yet. Order the live run
    // indices by where their bytes are instead, and move each run in that order; the index array and the sort's
    // scratch are sized with the queue, so this pass allocates nothing at the block's end.
    const std::size_t liveRuns = pending_.size();
    if (liveRuns == 0) {
        bytes_.clear();
        return;
    }
    for (std::size_t i = 0; i < liveRuns; ++i) rebaseOrder_[i] = static_cast<std::uint32_t>(i);
    rebaseSort_.sort(rebaseOrder_.data(), rebaseOrder_.data() + liveRuns,
                     [this](std::uint32_t a, std::uint32_t b) { return pending_[a].start < pending_[b].start; });
    std::size_t live = 0;
    for (std::size_t i = 0; i < liveRuns; ++i) {
        auto& run = pending_[rebaseOrder_[i]];
        if (run.start != live) std::memmove(bytes_.data() + live, bytes_.data() + run.start, run.count);
        run.start = static_cast<std::uint32_t>(live);
        live += run.count;
    }
    bytes_.resize(live);
}

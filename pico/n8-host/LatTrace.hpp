#pragma once
// The latency trace (-DN8_LAT_TRACE=1, a measurement build: main.cpp includes this only then, and the default image
// holds none of it). It times one isolated message's way through the box, the main loop's passes and what each part
// of a pass costs, and prints a summary on two lines of their own once MIDI has been quiet for a second, a message has
// been timed since the last summary and no timed write is waiting for its acknowledgement:
//
//   [lat] msgs n=<N> us avg/max: rx>drain a/m drain>block a/m block>push a/m push>write a/m write a/m write>ack a/m
//         total a/m over1500=<k> | path <cycles> cyc <misses> miss, block <cycles> cyc <misses> miss,
//         write <misses> miss
//   [lat] passes idle n=<N> <us> us avg, <us> max, <misses> miss avg, <misses> max | block in <p>% of passes,
//         <us> us avg, <us> max | busy <N> | seg max us usb .. stats .. | worst msg <us>: rx in <seg>, ack in <seg>
//
// The message's points: its last byte's UART timestamp (the poll's, or the RX interrupt's), the end of the drain that
// took it into the staging, the start of the block that pushed it, the link's push (BoxCartLink asking the staging when
// it arrived), the FIFO write issued and returned, and the N8's acknowledgement (tuh_cdc_tx_complete_cb). Each is
// timed on the microsecond clock; the message's own processing (from its pass's UART poll to the write) is also
// counted in core cycles (the M33's DWT cycle counter) and in XIP cache misses (the XIP block's hit and access
// counters, read at each point), which is what tells running work from waiting on flash. The image runs from SRAM
// (CMakeLists.txt), so they read 0 unless something it runs is in flash; run from flash, a miss cost about 90 cycles
// on the rig, and an isolated message about 90 of them.
//
// The messages timed are the writes main.cpp's `lat=` times (the oldest write not yet acknowledged), less one led by
// no UART message (a note-off rp.n8.nes owed, which the link timed from now); for isolated messages `total` is `lat=`.
// A pass counts as the one that drained a message when the UART's byte count rose since the last drain, which is the
// interrupt's count of bytes into the ring: a byte the interrupt takes in the few instructions between the drain and
// that read, or one the staging refused and left in the ring, marks the wrong pass, and its write, which then has no
// drain before it, is left out rather than mistimed.
//
// A pass is cut into segments (usb: the watchdog and tuh_task; uart: the poll and drain; block: the plan's block and
// the staging's end; link: the cart link's drain and its writes; n8: the probe, boot and connect edge; diag: sniff and
// health; watch: link_watch and the LED; stats: stats_report and its line), each timed. An idle pass is one that took
// no byte, wrote nothing and ran under 1 ms (no USB transfer, no console line); the rest are busy. `rx in <seg>` names
// the segment the worst message's last byte arrived in, so a byte the RX interrupt took while a console line or a USB
// read held the loop says which; `ack in <seg>` names where its acknowledgement was seen (usb: the pass's own
// tuh_task, or inside an Edio wait). A byte that arrived while the trace printed its own summary is `rx in report`.
//
// Its own cost: about 400 instructions a pass (counted in an emulator; an idle pass is about 480 without it), so a
// trace build's latency reads a few microseconds above the plain image's. It also changes the loop's block cadence: on
// the rig a traced idle pass took about 21 us, a block included, past the 20.8 us a frame lasts at 48 kHz, so a traced
// image runs a block in nearly every pass where the plain image runs one in about three, and `drain>block` reads 0
// where the plain image can wait up to a frame for its block. The summary itself goes through the SDK's console, which
// waits for the UART: its two lines of about 300 characters hold the loop about 56 ms, and a message that arrives
// meanwhile waits for them (`rx in report`).
//
// The counter arithmetic as it stands (separate access and hit totals, every difference through since()) has run in an
// emulator only: the rig figures above came from an earlier build of this trace, whose miss and write fields could
// wrap (a hit read after its access, an acknowledgement that came inside fifoWR).

#include <stdint.h>
#include <stdio.h>

#include <initializer_list>

#include "hardware/clocks.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "pico/time.h"

namespace lat {

enum Seg : uint8_t { Usb, Uart, Block, Link, N8, Diag, Watch, Stats, kSegs };
// The segments' names, then kSegs's ("-": before the timelines kept) and kReport's (while the summary printed).
inline constexpr uint8_t kReport = kSegs + 1;
inline const char* const kSegNames[kSegs + 2] = {"usb",   "uart", "block", "link", "n8",
                                                 "diag",  "watch", "stats", "-",   "report"};

// A moment: the microsecond clock, the cycle counter and the XIP block's access and hit counts (running totals).
struct Point {
    uint32_t us, cyc, acc, hit;
};

// What passed between two moments. Each figure is a free-running 32-bit count, so a difference is taken modulo 2^32,
// and one that comes out negative (a later read that raced an earlier one by a count or two) is 0, never a wrap.
inline uint32_t since(uint32_t from, uint32_t to) {
    return static_cast<int32_t>(to - from) < 0 ? 0u : to - from;
}
// XIP accesses the cache did not serve between two moments: the accesses less the hits, at least 0.
inline uint32_t missesBetween(const Point& a, const Point& b) {
    const uint32_t acc = b.acc - a.acc, hit = b.hit - a.hit;
    return acc > hit ? acc - hit : 0u;
}

struct Acc {
    uint64_t sum = 0;
    uint32_t max = 0, n = 0;
    void add(uint32_t v) {
        sum += v;
        if (v > max) max = v;
        ++n;
    }
    uint32_t avg() const { return n ? static_cast<uint32_t>(sum / n) : 0; }
    void reset() { *this = Acc{}; }
};

struct State {
    uint32_t cycPerUs = 120;
    bool     cyccnt = false;
    // The XIP counters, folded each pass into running totals and cleared, so neither saturates.
    uint32_t accBase = 0, hitBase = 0;
    // This pass's segment starts, and the last pass's (a byte the RX interrupt took arrived in one of those).
    Point    seg[kSegs + 1] = {};
    uint32_t prevSegUs[kSegs + 1] = {};
    uint32_t reportFrom = 0, reportTo = 0;  // when the last summary was printing (its lines hold the loop)
    uint8_t  cur = Usb;
    uint32_t rxAtDrain = 0;   // the UART's byte count at the last drain
    uint64_t blocksAtPass = 0;
    bool     tookBytes = false, wrote = false, blockRan = false;
    // The last drain that took bytes: when it ended, its pass's poll (the processing's start) and the segment timeline
    // the bytes arrived in (that pass's and the one before).
    Point    drainStart{}, drainEnd{};
    uint32_t drainSegUs[kSegs + 1] = {}, drainPrevSegUs[kSegs + 1] = {};
    // The pushes of the current block, for the write to find its first message's.
    static constexpr int kPushes = 8;
    struct Push {
        uint32_t stamp, us;
    } push[kPushes] = {};
    int      pushes = 0;
    Point    blockStart{};
    uint32_t blockCyc = 0, blockMiss = 0;  // the pass's block: its cycles and misses, kept for the write
    Point    writeStart{};
    // The write being timed (the one tuh_cdc_tx_complete_cb will acknowledge), and the message it carries first.
    bool     armed = false;
    bool     returned = false;  // the write's fifoWR call has returned (its acknowledgement can come first)
    uint32_t stamp = 0, drainUs = 0, blockAt = 0, pushAt = 0, writeAt = 0, writeRet = 0, pathCyc = 0, pathMiss = 0,
             writeMiss = 0, msgBlockCyc = 0, msgBlockMiss = 0;
    uint8_t  rxSeg = kSegs;
    // What the summary reports, since the last one.
    Acc rxDrain, drainBlock, blockPush, pushWrite, write, writeAck, total, path, pathMisses, msgBlock, msgBlockMisses,
        writeMisses, idleUs10, idleMiss, blockUs10;
    uint32_t over1500 = 0, passes = 0, blockPasses = 0, busy = 0;
    uint32_t segMax[kSegs] = {};
    uint32_t worstTotal = 0;
    uint8_t  worstRxSeg = kSegs, worstAckSeg = kSegs;
};
inline State g;

inline uint32_t cycles() { return g.cyccnt ? m33_hw->dwt_cyccnt : time_us_32() * g.cycPerUs; }
// The hits are read before the accesses, so that the accesses a moment counts always cover its hits.
inline Point now() {
    const uint32_t us = time_us_32(), cyc = cycles(), hit = g.hitBase + xip_ctrl_hw->ctr_hit;
    return Point{us, cyc, g.accBase + xip_ctrl_hw->ctr_acc, hit};
}

inline void init() {
    g.cycPerUs = clock_get_hz(clk_sys) / 1000000u;
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    // The cycle counter is optional in an M33 (DWT_CTRL.NOCYCCNT, bit 25, says it is absent): the microsecond clock
    // stands in for it then, at a microsecond's resolution.
    g.cyccnt = (m33_hw->dwt_ctrl & (1u << 25)) == 0;
    if (g.cyccnt) {
        m33_hw->dwt_cyccnt = 0;
        m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;
    }
    xip_ctrl_hw->ctr_acc = 0;
    xip_ctrl_hw->ctr_hit = 0;
    // What one XIP miss costs follows from the flash interface's set-up: the clock divider (TIMING bits 7:0), the read
    // format and command the boot left in QMI window 0.
    printf("[lat] trace on: clk_sys %lu MHz, cycle counter %s, qmi m0 timing 0x%08lx (clkdiv %lu) rfmt 0x%08lx rcmd "
           "0x%08lx, xip ctrl 0x%08lx\n",
           static_cast<unsigned long>(g.cycPerUs), g.cyccnt ? "on" : "absent (us clock)",
           static_cast<unsigned long>(qmi_hw->m[0].timing), static_cast<unsigned long>(qmi_hw->m[0].timing & 0xffu),
           static_cast<unsigned long>(qmi_hw->m[0].rfmt), static_cast<unsigned long>(qmi_hw->m[0].rcmd),
           static_cast<unsigned long>(xip_ctrl_hw->ctrl));
}

inline void mark(Seg s) {
    g.seg[s] = now();
    g.cur = s;
}

inline void passBegin() {
    // Fold the XIP counters into the running totals and clear them, so that a pass never sees one saturate.
    g.hitBase += xip_ctrl_hw->ctr_hit;
    xip_ctrl_hw->ctr_hit = 0;
    g.accBase += xip_ctrl_hw->ctr_acc;
    xip_ctrl_hw->ctr_acc = 0;
    for (int i = 0; i <= kSegs; ++i) g.prevSegUs[i] = g.seg[i].us;
    g.tookBytes = g.wrote = g.blockRan = false;
    mark(Usb);
}

// After the UART's drain: the block segment starts. `rx` is the UART's byte count; any rise since the last drain is
// taken as bytes this drain took into the staging (the header says when it is not).
inline void drained(uint32_t rx, uint64_t blocks) {
    mark(Block);
    g.blocksAtPass = blocks;
    g.pushes = 0;
    g.blockStart = g.seg[Block];
    if (rx != g.rxAtDrain) {
        g.rxAtDrain = rx;
        g.tookBytes = true;
        g.drainStart = g.seg[Uart];
        g.drainEnd = g.seg[Block];
        // This pass's segments up to the drain (the later ones have not started), and the whole pass before.
        for (int i = 0; i <= kSegs; ++i) {
            g.drainSegUs[i] = i <= Block ? g.seg[i].us : 0;
            g.drainPrevSegUs[i] = g.prevSegUs[i];
        }
    }
}

// After the block and the staging's end: the link segment starts.
inline void blockDone(uint64_t blocks) {
    mark(Link);
    g.blockRan = blocks != g.blocksAtPass;
    if (g.blockRan) {
        g.blockCyc = since(g.blockStart.cyc, g.seg[Link].cyc);
        g.blockMiss = missesBetween(g.blockStart, g.seg[Link]);
    }
}

// The link asked when a message it was pushed arrived, and was told `stamp`.
inline void pushed(uint32_t stamp) {
    if (g.pushes < State::kPushes) g.push[g.pushes++] = State::Push{stamp, time_us_32()};
}

// Which segment a moment fell in: kReport while the last summary printed, else on the drain's own pass or the one
// before; kSegs when earlier than both.
inline uint8_t segOf(uint32_t us) {
    if (g.reportTo != 0 && static_cast<int32_t>(us - g.reportFrom) >= 0 && static_cast<int32_t>(g.reportTo - us) >= 0)
        return kReport;
    for (int i = kSegs - 1; i >= 0; --i)
        if (static_cast<int32_t>(us - g.drainSegUs[i]) >= 0 && g.drainSegUs[i] != 0) return static_cast<uint8_t>(i);
    for (int i = kSegs - 1; i >= 0; --i)
        if (static_cast<int32_t>(us - g.drainPrevSegUs[i]) >= 0 && g.drainPrevSegUs[i] != 0)
            return static_cast<uint8_t>(i);
    return kSegs;
}

// A FIFO write is issued, carrying first the message that arrived at `firstUs`; `timed` when it is the write the
// acknowledgement will time (main.cpp arms ack_from_us only while no other write is pending).
inline void writeBegin(uint32_t firstUs, bool timed) {
    g.wrote = true;
    // A write the acknowledgement does not time leaves the one it does armed: main.cpp times the oldest.
    if (!timed) return;
    // Only a write led by a message off the UART: one the link timed from now (a note-off rp.n8.nes owed, which never
    // arrived) is no UART message, and has no drain before it. The acknowledgement then times this write, so nothing
    // is armed.
    if (static_cast<int32_t>(g.drainEnd.us - firstUs) < 0) {
        g.armed = false;
        return;
    }
    const Point p = now();
    g.armed = true;
    g.stamp = firstUs;
    g.drainUs = g.drainEnd.us;
    g.blockAt = g.blockStart.us;
    g.pushAt = p.us;
    for (int i = 0; i < g.pushes; ++i)
        if (g.push[i].stamp == firstUs) {
            g.pushAt = g.push[i].us;
            break;
        }
    g.writeAt = p.us;
    g.pathCyc = since(g.drainStart.cyc, p.cyc);
    g.pathMiss = missesBetween(g.drainStart, p);
    g.writeStart = p;
    g.returned = false;
    g.msgBlockCyc = g.blockCyc;
    g.msgBlockMiss = g.blockMiss;
    g.rxSeg = segOf(firstUs);
}

inline void writeEnd(bool ok) {
    if (!g.armed) return;
    if (!ok) {
        g.armed = false;
        return;
    }
    if (g.returned) return;  // acknowledged inside the call (acked)
    const Point p = now();
    g.returned = true;
    g.writeRet = p.us;
    g.writeMiss = missesBetween(g.writeStart, p);
}

// The N8 acknowledged the timed write (main.cpp's tuh_cdc_tx_complete_cb, on the same arming).
inline void acked() {
    if (!g.armed) return;
    g.armed = false;
    // fifoWR pumps tuh_task, so the acknowledgement can come before the call returns: the write then lasted until it.
    const Point t = now();
    if (!g.returned) {
        g.returned = true;
        g.writeRet = t.us;
        g.writeMiss = missesBetween(g.writeStart, t);
    }
    const uint32_t total = since(g.stamp, t.us);
    g.rxDrain.add(since(g.stamp, g.drainUs));
    g.drainBlock.add(since(g.drainUs, g.blockAt));
    g.blockPush.add(since(g.blockAt, g.pushAt));
    g.pushWrite.add(since(g.pushAt, g.writeAt));
    g.write.add(since(g.writeAt, g.writeRet));
    g.writeAck.add(since(g.writeRet, t.us));
    g.total.add(total);
    g.path.add(g.pathCyc);
    g.pathMisses.add(g.pathMiss);
    g.msgBlock.add(g.msgBlockCyc);
    g.msgBlockMisses.add(g.msgBlockMiss);
    g.writeMisses.add(g.writeMiss);
    if (total > 1500) ++g.over1500;
    if (total > g.worstTotal) {
        g.worstTotal = total;
        g.worstRxSeg = g.rxSeg;
        g.worstAckSeg = g.cur;
    }
}

// Formats and prints the summary: called from passEnd, after a second of quiet. Out of line, as passEnd is, so that
// neither its fields nor passEnd's bookkeeping sit in main's frame.
static void __noinline report();

static void __noinline passEnd(uint32_t lastRxUs) {
    const Point end = now();
    g.seg[kSegs] = end;
    uint32_t       longest = 0;
    for (int i = 0; i < kSegs; ++i) {
        const uint32_t d = since(g.seg[i].us, g.seg[i + 1].us);
        if (d > g.segMax[i]) g.segMax[i] = d;
        if (d > longest) longest = d;
    }
    const uint32_t passUs = since(g.seg[Usb].us, end.us);
    ++g.passes;
    if (g.blockRan) {
        ++g.blockPasses;
        g.blockUs10.add(g.blockCyc * 10u / g.cycPerUs);
    }
    if (!g.tookBytes && !g.wrote && passUs < 1000) {
        // Tenths of a microsecond from the cycle counter: an idle pass is a few microseconds.
        g.idleUs10.add(since(g.seg[Usb].cyc, end.cyc) * 10u / g.cycPerUs);
        g.idleMiss.add(missesBetween(g.seg[Usb], end));
    } else {
        ++g.busy;
    }
    if (g.total.n != 0 && !g.armed && since(lastRxUs, end.us) > 1000000u) report();
}

static void __noinline report() {
    g.reportFrom = time_us_32();
    const auto am = [](const Acc& a, char* out, size_t cap) {
        snprintf(out, cap, "%lu/%lu", static_cast<unsigned long>(a.avg()), static_cast<unsigned long>(a.max));
    };
    char f[12][24];
    am(g.rxDrain, f[0], sizeof f[0]);
    am(g.drainBlock, f[1], sizeof f[1]);
    am(g.blockPush, f[2], sizeof f[2]);
    am(g.pushWrite, f[3], sizeof f[3]);
    am(g.write, f[4], sizeof f[4]);
    am(g.writeAck, f[5], sizeof f[5]);
    am(g.total, f[6], sizeof f[6]);
    am(g.path, f[7], sizeof f[7]);
    am(g.pathMisses, f[8], sizeof f[8]);
    am(g.msgBlock, f[9], sizeof f[9]);
    am(g.msgBlockMisses, f[10], sizeof f[10]);
    am(g.writeMisses, f[11], sizeof f[11]);
    printf("[lat] msgs n=%lu us avg/max: rx>drain %s drain>block %s block>push %s push>write %s write %s write>ack %s "
           "total %s over1500=%lu | path %s cyc %s miss, block %s cyc %s miss, write %s miss\n",
           static_cast<unsigned long>(g.total.n), f[0], f[1], f[2], f[3], f[4], f[5], f[6],
           static_cast<unsigned long>(g.over1500), f[7], f[8], f[9], f[10], f[11]);
    const uint32_t idle = g.idleUs10.avg(), idleMax = g.idleUs10.max, blk = g.blockUs10.avg(), blkMax = g.blockUs10.max;
    printf("[lat] passes idle n=%lu %lu.%lu us avg, %lu.%lu max, %lu miss avg, %lu max | block in %lu%% of passes, "
           "%lu.%lu us avg, %lu.%lu max | busy %lu | seg max us usb %lu uart %lu block %lu link %lu n8 %lu diag %lu "
           "watch %lu stats %lu | worst msg %lu: rx in %s, ack in %s\n",
           static_cast<unsigned long>(g.idleUs10.n), static_cast<unsigned long>(idle / 10),
           static_cast<unsigned long>(idle % 10), static_cast<unsigned long>(idleMax / 10),
           static_cast<unsigned long>(idleMax % 10), static_cast<unsigned long>(g.idleMiss.avg()),
           static_cast<unsigned long>(g.idleMiss.max),
           static_cast<unsigned long>(g.passes ? static_cast<uint64_t>(g.blockPasses) * 100u / g.passes : 0),
           static_cast<unsigned long>(blk / 10), static_cast<unsigned long>(blk % 10),
           static_cast<unsigned long>(blkMax / 10), static_cast<unsigned long>(blkMax % 10),
           static_cast<unsigned long>(g.busy), static_cast<unsigned long>(g.segMax[Usb]),
           static_cast<unsigned long>(g.segMax[Uart]), static_cast<unsigned long>(g.segMax[Block]),
           static_cast<unsigned long>(g.segMax[Link]), static_cast<unsigned long>(g.segMax[N8]),
           static_cast<unsigned long>(g.segMax[Diag]), static_cast<unsigned long>(g.segMax[Watch]),
           static_cast<unsigned long>(g.segMax[Stats]), static_cast<unsigned long>(g.worstTotal),
           kSegNames[g.worstRxSeg], kSegNames[g.worstAckSeg]);
    g.reportTo = time_us_32();
    // A fresh window: the counters only (the clock set-up, the XIP totals and the timelines carry on).
    for (Acc* a : {&g.rxDrain, &g.drainBlock, &g.blockPush, &g.pushWrite, &g.write, &g.writeAck, &g.total, &g.path,
                   &g.pathMisses, &g.msgBlock, &g.msgBlockMisses, &g.writeMisses, &g.idleUs10, &g.idleMiss,
                   &g.blockUs10})
        a->reset();
    g.over1500 = g.passes = g.blockPasses = g.busy = 0;
    for (uint32_t& m : g.segMax) m = 0;
    g.worstTotal = 0;
    g.worstRxSeg = g.worstAckSeg = kSegs;
}

}  // namespace lat

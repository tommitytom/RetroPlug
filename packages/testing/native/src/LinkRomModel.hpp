#pragma once

// The two halves of BlipToaster's reliable link test (transport/N8ReliableLink.hpp): the ROM's own rules as a model the
// sender can be driven against, and the little frame arithmetic a case compares the wire with. The sender's half of the
// link is the N8 pack's, and the emulated cart FIFO that speaks it from the other side is the Mesen package's
// (system/mesen/roles/NesN8FifoRole.hpp), so the model lives here, in the test helper library, and both test binaries
// link it: the pack's own and the Mesen package's, which declares n8.
//
// Why HERE and not in the N8 pack: the model sits on both sides of the mesen/n8 edge, and a door in n8 for it would be a
// second mechanism for one header (n8's tests reach it through n8, mesen's through testing, and a test-support target in
// n8 would also have to be declared a devDependency by mesen beside the n8 dependency it needs for the FIFO role). One
// helper library both test binaries already link is the smaller thing.
//
// The ROM half is pinned in the BlipToaster repo (retroplug-cli/tests/link.test.ts); here the model stands in for it so
// the sender can be driven through a LOSSY channel shaped like the real one: on NTSC a DMC-DMA halt on `lda $40F0`
// repeats the read and the N8 FIFO pops a byte per read, so 1-3 consecutive bytes vanish. The property that matters is
// exact: whatever is lost, the ROM ends up applying every message once, in order.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace rp::n8::test {

using Bytes = std::vector<std::uint8_t>;

// The ROM's link rules (bliptoaster src/midi/main.c), as a byte-at-a-time parser. `applied` collects the messages it
// would act on; ack() reports what it would write on the back-channel when its FIFO runs empty.
struct RomModel {
    bool         linkOn = false;
    std::uint8_t next   = 0;
    bool         dirty  = false;
    enum { Idle, WantSync, WantSeq, InMsg, WantSum } st = Idle;
    std::uint8_t seq = 0;
    std::uint8_t last = 0x7F;   // the last frame header read
    unsigned     sum = 0;
    std::size_t  need = 0;   // message length incl. status; 0 = a SysEx (to F7)
    Bytes        buf;
    std::vector<Bytes> applied;

    // A frame damaged after its seq: the ROM reports the frame it expects as the last one read, since a damaged
    // frame's seq cannot be trusted (frame_hdr's damaged exit).
    void bad() { if (st == InMsg || st == WantSum) last = next; st = Idle; dirty = true; }

    void byte(std::uint8_t b) {
        if (b >= 0xF8) return;                                          // real-time: skipped (0xFF panic not modelled)
        switch (st) {
            case WantSync:
                if (b & 0x80) { st = Idle; byte(b); return; }
                linkOn = true;
                next = b; last = static_cast<std::uint8_t>((b - 1) & 0x7F); dirty = true; st = Idle;
                return;
            case WantSeq:
                // header cut short: the expected frame, damaged
                if (b & 0x80) { last = next; bad(); byte(b); return; }
                seq = b; last = b; sum = b; buf.clear(); st = InMsg; return;
            case InMsg:
                if (buf.empty()) {
                    if (!(b & 0x80)) { bad(); return; }                 // the status was lost
                    if (b == 0xF4 || b == 0xF5) { bad(); byte(b); return; }
                    need = (b == 0xF0) ? 0 : (((b & 0xE0) == 0xC0) ? 2 : 3);
                } else if ((b & 0x80) && !(need == 0 && b == 0xF7)) {
                    bad(); byte(b); return;                             // a status where data was due: cut
                }
                if (buf.size() == 40) { bad(); return; }
                buf.push_back(b); sum += b;
                if ((need && buf.size() == need) || b == 0xF7) st = WantSum;
                return;
            case WantSum:
                if (b & 0x80) { bad(); byte(b); return; }
                if (b != (sum & 0x7F)) { bad(); return; }              // the sum does not match: damaged
                st = Idle;
                if (!linkOn) { linkOn = true; next = seq; }
                if (seq != next) { dirty = true; return; }
                applied.push_back(buf);
                next = static_cast<std::uint8_t>((next + 1) & 0x7F);
                dirty = true;
                return;
            case Idle:
                if (b == 0xF4) st = WantSync;
                else if (b == 0xF5) st = WantSeq;
                else if (linkOn) { last = next; dirty = true; }          // orphaned bytes: the expected frame, lost
                return;
        }
    }

    bool ack(Bytes& out) {
        if (!dirty) return false;
        dirty = false;
        out = { 0xF5, next, last };
        return true;
    }
};

inline Bytes noteOn(std::uint8_t ch, std::uint8_t n, std::uint8_t v) { return { std::uint8_t(0x90 | ch), n, v }; }

// A frame as the sender writes it: F5 seq message sum.
inline Bytes frame(std::uint8_t seq, const Bytes& msg) {
    Bytes f = { 0xF5, seq };
    unsigned sum = seq;
    for (auto b : msg) { f.push_back(b); sum += b; }
    f.push_back(static_cast<std::uint8_t>(sum & 0x7F));
    return f;
}
inline Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes o;
    for (auto& p : parts) o.insert(o.end(), p.begin(), p.end());
    return o;
}

}  // namespace rp::n8::test

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "transport/N8ReliableLink.hpp"

// The N8 menu guard's decisions (docs/n8-menu-guard-reset-design.md, docs/pico-box-framed-link-design.md section 2):
// whether the cart a stream goes to is at its menu, which reads the FIFO a game takes MIDI from as its own commands
// (0x2A 0x73 is '*s'), so a stream into it must stop. It asks with a probe, kProbe: '*t' inside a SysEx. The N8 menu
// skips every byte that is not a '*', so it reads the '*t' and answers 'k' (kAnswer) on the back-channel, after a
// console RESET too, which leaves the FPGA config block's map_idx at the game's mapper; a MIDI game ignores a SysEx it
// does not know, and BlipToaster, on the real cart, ignores this one. The answer is a 'k' outside an ack (Scan), which
// only the menu writes.
//
// The decisions alone: no I/O, no clock of its own and no allocation, so the desktop's N8Link (packages/n8) and the
// Pico box's cart link (pico/n8-host's BoxCartLink) run the one guard over their own transports. A driver hands it the
// back-channel as it reads it (hear(), after the reliable sender's onBackChannel on a framed link), then takes the
// turn() its stream is at, with the time (int64 nanoseconds of any monotonic clock, never 0: 0 is "no probe awaited",
// and "no settle" for the config watch's settle clock) and its sender (null on a raw link), and does what the turn
// says, in this order: drop what it holds when the menu answered (enteredMenu); probe the link again when the menu
// was left (leftMenu) or a stalled window given up (reprobeLink, what the stream holds and the window dropped first);
// write kProbe when writeProbe says; and, unless the turn holds, the sender's re-sends at the same `now` and the
// stream's new messages. Where the turn holds and readConfig says, it reads the config block and hands it to config().
// Not thread-safe: one thread drives it.
//
// When it asks:
// - raw, every 250 ms between two messages, as part of the stream: no read and no wait, the answer is heard on a later
//   turn. A raw link has no acks, so nothing else tells a menu the console's reset reached from a game.
// - framed, only once the window has STOPPED MOVING (the oldest frame unacknowledged past the link's own timeout, which
//   is the console reset), holding new frames and re-sends until a 'k' (the menu) or an ack (a game whose acks are
//   merely lost or damaged: its stream goes on, and a stall is not asked about again for a second, a trusted second
//   that a window which drains ends), and asking again each second that hears neither. A framed game never sees the
//   probe outside a stall. A live BlipToaster drops the probe's bytes as unframed and acknowledges F5 <next> <next> for
//   them (bliptoaster src/midi/parse_asm.s), so a game whose own acks were lost still answers the stall. Two stall
//   probes that hear nothing at all are neither the menu nor this link's game: a game without the link, started from
//   the cart's browser after a reset the guard never saw (a framed link asks only in a stall). The turn then asks for
//   the window and what the stream held for it to go, and the link to be probed again: a game with the link re-syncs
//   framed, any other is streamed raw.
// - at the menu, never. The N8 OS reads its MCU's status replies from the FIFO the host writes into (edn8-pro-pub
//   edio/everdrive.c, ed_check_status), so a byte arriving while it works - a game started from its browser above all
//   - is read as a status and stops it at ERROR:40 (measured: docs/n8-menu-guard-config-watch-design.md). The turn
//   asks for the FPGA config block instead (readConfig, every 20 ms), which the MCU answers without the FIFO: every
//   start from the browser clears the block, then writes the game's, so a non-zero block that reads the same for
//   half a second is a game when it differs from the one read on entering or an all-zero block was read since (the
//   same game started again ends on the very same bytes), and the menu is left (the driver probes the link again). A
//   single garbled read followed by the unchanged block is neither, and never leaves. A start the reads miss leaves
//   the guard at the menu until a reconnect.

namespace rp {

class N8MenuGuard {
public:
    static constexpr std::uint8_t kProbe[4] = {0xF0, 0x2A, 0x74, 0xF7};
    static constexpr std::uint8_t kAnswer   = 0x6B;
    // How often it asks (raw); how long the menu's answer may take before the cart is taken for a game, which ignores
    // the probe; and how long a framed hold waits for an answer or an ack before it asks again, which is also the
    // trusted second. The lab's menu answers in about 20 ms (27 ms the worst of 8, at a power-on menu), so the answer
    // window has room to spare.
    static constexpr std::int64_t kProbeNs  = 250'000'000;
    static constexpr std::int64_t kAnswerNs = 100'000'000;
    static constexpr std::int64_t kHoldNs   = 1'000'000'000;
    // How often the config block is read at the menu, how long a changed block must read the same before the menu is
    // left (the OS is still at work when the block changes, and the link probe that leaving sets off is a FIFO write),
    // and the block's size (16 bytes at 0x1800020).
    static constexpr std::int64_t kConfigNs    = 20'000'000;
    static constexpr std::int64_t kSettleNs    = 500'000'000;
    static constexpr std::size_t  kConfigBytes = 16;
    // How many stall probes a framed link may hear nothing at all to before it gives the window up.
    static constexpr int kStallMisses = 2;

    // The back-channel, a byte at a time: the menu's answer is a 'k' OUTSIDE an ack. An ack is F5 <next> <last>, both
    // 7-bit (N8ReliableSender::onBackChannel), so either can be a 0x6B, and the scan follows the same states to tell.
    // Its state runs on across reads, since an ack can be split between two: a driver that drops input (a flush)
    // starts it afresh, and one whose link probe stopped reading halfway through an ack sets where it stopped.
    struct Scan {
        enum class Heard { Nothing, Ack, MenuAnswer };
        int   state = 0;  // 0 between acks, 1 after F5, 2 after <next>
        Heard feed(std::uint8_t b) {
            if (b == N8ReliableSender::FRAME) {
                state = 1;
                return Heard::Nothing;
            }
            if (b & 0x80) {
                state = 0;
                return Heard::Nothing;
            }
            if (state == 1) {
                state = 2;
                return Heard::Nothing;
            }
            if (state == 2) {
                state = 0;
                return Heard::Ack;
            }
            return b == kAnswer ? Heard::MenuAnswer : Heard::Nothing;
        }
    };

    // What a turn tells its driver (see the top of this file for the order it acts in).
    struct Turn {
        bool hold        = false;  // nothing new, no re-send and no note release goes out on this turn
        bool writeProbe  = false;  // write kProbe now (on a raw link the turn was told it is between two messages)
        bool readConfig  = false;  // at the menu: read the FPGA config block and hand it to config()
        bool reprobeLink = false;  // drop what the stream holds and the window, and probe the link again
        bool enteredMenu = false;  // the menu answered: drop what the stream holds, the window and any pending release
        bool leftMenu    = false;  // a game started at the menu (the config block): its link is probed again
    };

    // Afresh, as at a connect: not at the menu, no probe awaited or due, nothing heard.
    void reset() { *this = N8MenuGuard{}; }
    // The state a connect's own check found (N8Host's askMenu, before the stream starts): set, with nothing to enter.
    void seed(bool menu) { atMenu_ = menu; }
    bool atMenu() const { return atMenu_; }
    // The cart's whole acks heard since the last reset: on a raw link a driver reads one as the ROM speaking the link.
    std::uint64_t acksHeard() const { return acksHeard_; }
    Scan&       scan() { return scan_; }
    const Scan& scan() const { return scan_; }

    // What the cart has said on the back-channel: its whole acks counted, and the menu's 'k' remembered for the turn.
    void hear(const std::uint8_t* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const Scan::Heard heard = scan_.feed(data[i]);
            if (heard == Scan::Heard::Ack) ++acksHeard_;
            else if (heard == Scan::Heard::MenuAnswer) menuAnswered_ = true;
        }
    }

    // The config block a readConfig turn asked for (kConfigBytes), read at `now`. A read that failed is not handed
    // back: the guard asks again on its cadence, and nothing else changes.
    void config(std::int64_t now, const std::uint8_t* block) {
        if (!atMenu_) return;
        if (!haveRef_) {
            std::memcpy(ref_, block, kConfigBytes);
            haveRef_ = true;
            return;
        }
        bool zero = true;
        for (std::size_t i = 0; i < kConfigBytes && zero; ++i) zero = block[i] == 0;
        if (zero) {  // the OS is between clearing the block and writing the game's
            sawZero_      = true;
            settleFromNs_ = 0;
            return;
        }
        if (settleFromNs_ == 0 || std::memcmp(block, settle_, kConfigBytes) != 0) {
            std::memcpy(settle_, block, kConfigBytes);
            settleFromNs_ = now;
            return;
        }
        // Settled: a game when the block was cleared on the way (the same game started again ends on the very same
        // bytes) or differs from the reference (a different game). One garbled read and the unchanged block again is
        // neither, and never leaves.
        if (now - settleFromNs_ >= kSettleNs && (sawZero_ || std::memcmp(block, ref_, kConfigBytes) != 0)) {
            leaveDue_ = true;
        }
    }

    // Where the cart runs, as a probe outside the guard's own said it (N8Host's for a submission), or as a turn decides
    // it: true when that is a change, which the driver then acts on as a turn's enteredMenu or leftMenu. A change drops
    // any probe still awaited, so none is awaited once at the menu (probeAwaited() is false there), and it starts the
    // config watch afresh: the next block read is the new reference.
    bool report(bool menu) {
        if (menu == atMenu_) return false;
        atMenu_      = menu;
        probeAtNs_   = 0;
        stallMisses_ = 0;
        gameUntilNs_ = 0;
        haveRef_      = false;
        sawZero_      = false;
        leaveDue_     = false;
        settleFromNs_ = 0;
        configDueNs_  = 0;
        return true;
    }

    // The turn at `now`: what the stream may write, and what the driver does first (see the top of this file).
    // `sender` is the reliable link's (null on a raw link); `betweenMessages` whether the stream's own bytes stand
    // between two messages, where the raw link's probe may go.
    Turn turn(std::int64_t now, const N8ReliableSender* sender, bool betweenMessages) {
        Turn t;
        if (menuAnswered_) {
            menuAnswered_ = false;
            probeAtNs_    = 0;
            t.enteredMenu = report(true);
        }
        if (atMenu_) {
            if (leaveDue_) {
                t.leftMenu = report(false);
                return t;
            }
            // The turn that entered waits out the answer window before the first read: a second 'k' from an earlier
            // probe may still come, and a read then would take it for the reply's first byte.
            if (t.enteredMenu) configDueNs_ = now + kAnswerNs;
            else if (now >= configDueNs_) {
                configDueNs_ = now + kConfigNs;
                t.readConfig = true;
            }
            t.hold = true;  // nothing goes out while it says menu, not even a probe
            return t;
        }
        if (sender && sender->progressOverdue(now) && now >= gameUntilNs_) {
            if (probeAtNs_ != 0 && acksHeard_ != acksAtProbe_) {
                // An ack since the stall's probe: a game, whose own acks were lost or damaged. It streams on.
                probeAtNs_   = 0;
                stallMisses_ = 0;
                gameUntilNs_ = now + kHoldNs;
                return t;
            }
            if (probeAtNs_ != 0 && now - probeAtNs_ >= kHoldNs && ++stallMisses_ >= kStallMisses) {
                probeAtNs_    = 0;
                stallMisses_  = 0;
                t.reprobeLink = true;
                return t;
            }
            if (probeAtNs_ == 0 || now - probeAtNs_ >= kHoldNs) probe(now, t);
            t.hold = true;  // hold the re-send until the cart has answered
            return t;
        }
        if (sender) {
            // The window moves (or a stall's ack was heard within the second): no probe of a stall is awaited any
            // more, and a miss left from an earlier stall does not count toward the next one's drop. A window that has
            // drained ends the trusted second too: it vouched for the game that was stalled, and the next frame's stall
            // (a RESET since) is asked about at the link's own timeout, which on a lossy link is otherwise a second
            // away.
            probeAtNs_   = 0;
            stallMisses_ = 0;
            if (sender->outstanding() == 0) gameUntilNs_ = 0;
            return t;
        }
        if (probeAtNs_ != 0 && now - probeAtNs_ >= kAnswerNs) probeAtNs_ = 0;  // unanswered: a game
        if (probeAtNs_ == 0 && now >= probeDueNs_ && betweenMessages) probe(now, t);
        return t;
    }

    // A probe's answer may still be coming: a read of a reply now could take the menu's 'k' for its first byte.
    bool probeAwaited(std::int64_t now) const { return probeAtNs_ != 0 && now - probeAtNs_ < kAnswerNs; }

private:
    // The probe goes out on this turn: booked here, so a driver whose write then fails has the state a probe written
    // leaves (the failure ends its stream either way).
    void probe(std::int64_t now, Turn& t) {
        probeAtNs_   = now;
        probeDueNs_  = now + kProbeNs;
        acksAtProbe_ = acksHeard_;
        t.writeProbe = true;
    }

    bool          atMenu_       = false;
    // When the last probe went out (0: none awaiting its answer), when the next is due, and the moment before which a
    // stopped window is not probed again, because the last probe's stall heard an ack (a game).
    std::int64_t  probeAtNs_    = 0;
    std::int64_t  probeDueNs_   = 0;
    std::int64_t  gameUntilNs_  = 0;
    // Stall probes in a row that heard nothing at all (the link is given up at kStallMisses).
    int           stallMisses_  = 0;
    // The config watch at the menu: the block read on entering, whether an all-zero block has been read since, the
    // block a settle is timing and since when (0: none), when the next read is due, and a settle that has run its
    // course.
    std::uint8_t  ref_[kConfigBytes]    = {};
    std::uint8_t  settle_[kConfigBytes] = {};
    bool          haveRef_      = false;
    bool          sawZero_      = false;
    bool          leaveDue_     = false;
    std::int64_t  settleFromNs_ = 0;
    std::int64_t  configDueNs_  = 0;
    // What the back-channel has said: the menu's 'k', and how many whole acks, against the count when the probe went.
    bool          menuAnswered_ = false;
    Scan          scan_;
    std::uint64_t acksHeard_    = 0;
    std::uint64_t acksAtProbe_  = 0;
};

}  // namespace rp

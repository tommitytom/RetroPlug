#include "n8/desktop/N8Link.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "n8/CartLink.hpp"  // NoteEvent: the release tracker's books

namespace retroplug {

namespace {
using steady = std::chrono::steady_clock;
// How often the cart's at-menu state is read on a quiet turn while streaming (raw, framed and idle, or already at the
// menu); how long a stream whose window has stopped moving waits for a quiet turn before it is read anyway (a menu that
// chatters on the back-channel keeps the quiet rule fresh for ever, so only this bound ends it); and how long a read
// that did not answer holds the next one off, so a port that keeps timing out costs the stream 100 ms a second, not
// 100 ms in every 250. The bound is also the job probe's own deadline (N8Host's kMenuProbeMs).
constexpr std::int64_t kMenuReadNs = 250'000'000;
constexpr std::int64_t kMenuHoldNs = 1'000'000'000;
constexpr std::int64_t kMenuReadFailNs = 1'000'000'000;
// RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
bool traceOn() {
    static const bool on = std::getenv("RP_N8LINK_TRACE") != nullptr;
    return on;
}
std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(steady::now().time_since_epoch()).count();
}
// How long a cart may say nothing while a frame is outstanding before it is taken for one acknowledging nothing: the
// pause's bounded wait for acks, and the silence after which a config read may go ahead of an outstanding frame.
constexpr std::int64_t kAckWaitNs = 250'000'000;
// How long a message the cart's parser is still inside may hold a leaving node's release: long enough for the next
// chunk of a SysEx the bytes port split, short enough that a stream that died mid-message still releases its notes.
constexpr std::int64_t kMidMessageNs = 250'000'000;
}  // namespace

N8Link::N8Link(PortFactory factory) : factory_(std::move(factory)) {}

N8Link::~N8Link() { disconnect(); }

std::string N8Link::portName() const {
    std::lock_guard<std::mutex> l(meta_);
    return portName_;
}

std::string N8Link::lastError() const {
    std::lock_guard<std::mutex> l(meta_);
    return error_;
}

void N8Link::setError(const std::string& msg) {
    std::lock_guard<std::mutex> l(meta_);
    error_ = msg;
}

void N8Link::check(const rp::n8::Status& status) {
    if (!status.ok()) throw std::runtime_error(failureText(status, *serialPort_));
}

std::size_t N8Link::readPort(std::uint8_t* buffer, std::size_t size, std::uint32_t timeoutMs) {
    const std::size_t n = serialPort_->read(buffer, size, timeoutMs);
    if (std::string said = serialPort_->failure(); !said.empty()) throw std::runtime_error(said);
    return n;
}

void N8Link::flushPort() {
    serialPort_->flushInput();
    if (std::string said = serialPort_->failure(); !said.empty()) throw std::runtime_error(said);
}

bool N8Link::connect(const std::string& port) {
    disconnect();  // tear down any prior connection first
    try {
        auto sp   = factory_(port);
        auto edio = std::make_unique<rp::n8::Edio>(*sp);
        if (const rp::n8::Status s = edio->connect(); !s.ok())  // the handshake: a bad or absent reply
            throw std::runtime_error(failureText(s, *sp));
        serialPort_ = std::move(sp);
        edio_       = std::move(edio);
    } catch (const std::exception& e) {
        setError(e.what());
        serialPort_.reset();
        edio_.reset();
        connected_.store(false, std::memory_order_release);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(meta_);
        portName_ = port;
        error_.clear();
    }
    // The link is up and nothing else is on the port yet (the serial thread starts below), so this is where a
    // one-shot device write belongs - see OnConnected. Best-effort by design: the only plausible failure is a
    // read timeout on a port whose first fifoWR would fail anyway, and losing the MIDI link over a volume
    // register would be a worse outcome than the register staying where the N8 OS left it.
    if (onConnected_) {
        try {
            onConnected_(*edio_);
        } catch (const std::exception&) {
        }
    }
    // Quiescent here (connected_ is false, so no producer is pushing): clear any stale items from a prior
    // connection, then start the consumer thread BEFORE allowing producers.
    { TimedChunk drop; while (ring_.tryPop(drop)) {} }
    chunksWritten_ = chunksPushed_.load(std::memory_order_acquire);  // the ring is empty: nothing pushed is still to go
    std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);     // a fresh connection's cart holds no note of ours
    // Nor is a prior connection's message still open on this one's wire: a port that ended mid-SysEx would otherwise
    // hold the release gate open until an F7 that is never coming.
    wireExpect_      = 0;
    wireSysex_       = false;
    wireOpenSinceNs_ = 0;
    wireStatus_      = 0;
    // The cart's at-menu state, on the fresh port and before any producer may push: a menu reads the FIFO as commands,
    // so the state decides whether anything may be streamed at all. A check that fails to answer leaves the state as it
    // was (not the menu), which streams as the link always did.
    atMenu_.store(false, std::memory_order_relaxed);
    menuHoldSince_  = 0;
    menuReadDueNs_  = 0;
    menuGameUntilNs_ = 0;
    if (menuCheck_) {
        try {
            // What the cart said after the handshake is not this read's reply: a flush keeps a chattering cart's byte
            // out of the block's first byte, which would read as a game (see readMenuState).
            flushPort();
            atMenu_.store(menuCheck_(*edio_) == 1, std::memory_order_relaxed);
        } catch (const std::exception&) {
        }
    }
    bytesForwarded_.store(0, std::memory_order_relaxed);
    controlRetryAtNs_ = 0;  // a prior connection's failed control write does not hold this one's first turn back
    backChannelQuiet_.store(true, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> l(jobMutex_);
        acceptingJobs_ = true;
    }
    thread_ = std::thread(&N8Link::serialLoop, this);
    connected_.store(true, std::memory_order_release);  // now the audio thread may push
    return true;
}

void N8Link::disconnect() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();  // the serial thread stops touching edio_ before we reset it
    // Where the OLD cart ran says nothing about the next one: a status read while the link is down must not keep saying
    // "at the menu" for a port that has been closed.
    atMenu_.store(false, std::memory_order_relaxed);
    menuHoldSince_   = 0;
    menuGameUntilNs_ = 0;
    rel_.reset();
    reliable_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    edio_.reset();
    serialPort_.reset();
}

bool N8Link::push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate) {
    if (n == 0) return true;
    if (!connected_.load(std::memory_order_acquire) || atMenu_.load(std::memory_order_relaxed)) {
        // The menu reads the same FIFO a game takes MIDI from, so a message is a command there: dropped, not held (a
        // game booted later starts fresh, and a console reset's game held the notes, not this one).
        pushesDropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const std::int64_t offNs  = sampleRate > 0.0 ? static_cast<std::int64_t>(sampleOffset * 1e9 / sampleRate) : 0;
    const std::int64_t target = nowNs() + offNs + lookaheadNs_.load(std::memory_order_relaxed);
    std::size_t i = 0;
    while (i < n) {
        const std::size_t take = std::min<std::size_t>(n - i, sizeof(TimedChunk::data));  // <= 8
        TimedChunk c;
        c.targetNs = target;
        c.len      = static_cast<std::uint8_t>(take);
        std::memcpy(c.data, data + i, take);
        if (!ring_.tryPush(c)) {  // ring full: drop (never block the audio thread), and say so
            pushesDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        chunksPushed_.fetch_add(1, std::memory_order_release);
        i += take;
    }
    return true;
}

void N8Link::releaseNotes(const NoteSet& notes) {
    releaseNotes(notes, chunksPushed_.load(std::memory_order_acquire));
}

void N8Link::releaseNotes(const NoteSet& notes, std::uint64_t mark) {
    if (notes.none() || atMenu_.load(std::memory_order_relaxed)) return;  // a menu holds no notes of ours
    std::lock_guard<std::mutex> l(jobMutex_);
    if (!acceptingJobs_) return;  // no serial thread to write them: the link is down
    releasePending_ |= notes;
    // Behind the caller's own last push (the mark it recorded), not behind every push so far: a cart node's destroy
    // runs after the plan swap, so the node taking over has already pushed, and a release that waited for those would
    // go out behind a note-on it has to precede. Several leaving nodes' releases take the latest mark: a note-on
    // between two of their marks belongs to a node that is leaving too.
    releaseAfter_ = std::max(releaseAfter_, mark);
    releaseWanted_.store(true, std::memory_order_release);
}

void N8Link::writeReleases(bool stopping) {
    if (!releaseWanted_.load(std::memory_order_acquire)) return;
    // At the menu nothing goes, stopping or not: a menu holds no notes of ours, and its FIFO reads a note-off as its
    // own commands.
    if (atMenu_.load(std::memory_order_relaxed)) return;
    // And, on a raw link, between two messages only: a note-off written inside one the bytes port split across chunks
    // is not a message the cart's parser sees (a SysEx above all - a note-off inside an F0..F7 run is payload). A
    // stream left mid-message holds it only until kMidMessageNs, so a node that dies mid-SysEx still gets its notes
    // released. A framed link needs no gate: the release is framed whole (sendMessage), so it cannot join a message the
    // sender is still collecting.
    if (!stopping && !rel_ && !atMessageBoundary(nowNs())) return;
    NoteSet      notes;
    std::uint64_t after = 0;
    {
        std::lock_guard<std::mutex> l(jobMutex_);
        // A chunk pushed before the release is still to go (a note-on waiting for its release time would otherwise
        // follow its own note-off and sound for good). A stopping link writes no more chunks, so it does not wait.
        if (!stopping && chunksWritten_ < releaseAfter_) return;
        notes = releasePending_;
        // The mark THIS batch was taken at, copied under the lock that writes it: releaseNotes on the control thread
        // can move the field while the loop below runs, and a newer mark must not decide about an older batch.
        after = releaseAfter_;
        releasePending_.reset();
        releaseAfter_ = 0;  // this batch's mark is spent: a later one waits behind its own, or behind every push
        releaseWanted_.store(false, std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!notes.test(i)) continue;
        // A key whose last note-on went out at or past the mark is the node that took over's, and a note-off here would
        // cut it. (A key it struck and released again is kNoChunk, so this is only ever the safe direction.)
        if (lastOn_[i] != kNoChunk && lastOn_[i] >= after) continue;
        TimedChunk off;
        off.len     = 3;
        off.data[0] = static_cast<std::uint8_t>(0x80 | (i / 128));
        off.data[1] = static_cast<std::uint8_t>(i % 128);
        off.data[2] = 0;
        writeChunk(off, true);
    }
}

bool N8Link::runPaused(PausedJob job) {
    if (!job.run || !job.done) return false;
    std::lock_guard<std::mutex> l(jobMutex_);
    if (!acceptingJobs_ || jobPending_.load(std::memory_order_relaxed) || !connected_.load(std::memory_order_acquire))
        return false;
    job_ = std::move(job);
    jobPending_.store(true, std::memory_order_release);
    return true;
}

void N8Link::probeLink() {
    // F4 00 is the link's sync: a BlipToaster with the link answers it with an ack (F5 00) on the back-channel the
    // moment its FIFO runs empty; any other ROM ignores an undefined System Common status. ~100 ms covers a busy
    // ROM and the USB round trip with room to spare - and costs nothing after connect.
    rel_.reset();
    reliable_.store(false, std::memory_order_release);
    const std::uint8_t sync[2] = { rp::N8ReliableSender::SYNC, 0 };
    check(edio_->fifoWR(sync, 2));
    const std::int64_t until = nowNs() + 100'000'000;
    bool sawFrame = false;
    while (running_.load(std::memory_order_acquire) && nowNs() < until) {
        std::uint8_t buf[32];
        const std::size_t n = readPort(buf, sizeof buf, 2);
        for (std::size_t i = 0; i < n; ++i) {
            if (sawFrame && buf[i] < 0x80) {
                rp::N8ReliableSender::Timing t;
                t.resendDelayNs   = 0;           // the real round trip already applies
                rel_ = std::make_unique<rp::N8ReliableSender>(t);
                reliable_.store(true, std::memory_order_release);
                return;
            }
            sawFrame = (buf[i] == rp::N8ReliableSender::FRAME);
        }
    }
}

void N8Link::writeFramed(std::vector<std::uint8_t>& out, const std::vector<int>& seqs, std::int64_t now, bool resend) {
    if (out.empty()) return;
    // A frame has left once it is written - whether or not the test hook below loses some of it on the way.
    for (int s : seqs) rel_->onSent(s, now);
    const int p = testLossPermille_.load(std::memory_order_relaxed);
    if (p > 0) {
        std::vector<std::uint8_t> kept;
        kept.reserve(out.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            lossRng_ = lossRng_ * 1664525u + 1013904223u;
            if (static_cast<int>((lossRng_ >> 8) % 1000) < p) {   // a 1-3 byte burst, like a DMC-DMA halt
                lossRng_ = lossRng_ * 1664525u + 1013904223u;
                i += (lossRng_ >> 8) % 3;
                continue;
            }
            kept.push_back(out[i]);
        }
        out.swap(kept);
        if (out.empty()) return;
    }
    if (traceOn()) {
        std::fprintf(stderr, "[n8link] %.3f tx", static_cast<double>(nowNs()) / 1e6);
        for (auto b : out) std::fprintf(stderr, " %02X", b);
        std::fprintf(stderr, "\n");
    }
    if (writeLog_) writeLog_(out.data(), out.size(), nowNs(), resend);
    check(edio_->fifoWR(out.data(), out.size()));
    bytesForwarded_.fetch_add(out.size(), std::memory_order_relaxed);
}

void N8Link::serviceLink(std::int64_t now) {
    serviceLinkAcks();
    serviceLinkResends(now);
}

// Acks in, and nothing out: the quiet test a turn's read is given must see what the cart has actually said, and a
// reply read before it can have an ack land inside.
void N8Link::serviceLinkAcks() {
    const std::int64_t now = nowNs();
    std::uint8_t buf[64];
    const std::size_t n = readPort(buf, sizeof buf, 0);   // whatever the cart has said: never waits
    if (n && traceOn()) {
        std::fprintf(stderr, "[n8link] %.3f rx", static_cast<double>(now) / 1e6);
        for (std::size_t i = 0; i < n; ++i) std::fprintf(stderr, " %02X", buf[i]);
        std::fprintf(stderr, "\n");
    }
    if (n) rel_->onBackChannel(buf, n, now);
}

// Whatever the reliable link is due to re-send at `now`, out. Held while the menu guard looks (menuHold): a cart at its
// menu reads a stale frame as a command. `now` is the turn's timestamp, the one the guard judged: the poll's timeout
// and the guard's own threshold are the same instant for a frame that was never re-sent, so a second reading of the
// clock here would let a turn just short of the threshold re-send on the far side of it, into the menu.
void N8Link::serviceLinkResends(std::int64_t now) {
    std::vector<std::uint8_t> out;
    std::vector<int> seqs;
    rel_->poll(now, [&](const std::uint8_t* d, std::size_t k, int seq) {
        if (seq >= 0) seqs.push_back(seq);
        out.insert(out.end(), d, d + k);
    });
    if (!out.empty()) writeFramed(out, seqs, now, true);
    linkResends_.store(rel_->resends(), std::memory_order_relaxed);
    linkOutstanding_.store(rel_->outstanding(), std::memory_order_relaxed);
    linkRttMinNs_.store(rel_->rttMinNs(), std::memory_order_relaxed);
}

// The cart's at-menu state for this turn (see MenuCheck). Two things ask for a read: the cadence, on a turn quiet
// enough for one; and a framed window that has STOPPED MOVING - the oldest frame unacknowledged past the link's own
// timeout - which is the console reset, and whose re-send a menu would read as one of its commands. The second holds
// the stream until the read answers. A cart that chatters cannot postpone that for ever: the quiet rule stamps the last
// heard on any byte, so its bound, not the silence, is what ends it (kMenuHoldNs), after a flush of what it said.
bool N8Link::menuHold(std::int64_t now, bool quiet) {
    if (!menuCheck_) return false;
    if (atMenu_.load(std::memory_order_relaxed)) {
        menuHoldSince_ = 0;
        // Still look, every cadence: a game the user starts from the cart's own browser is only visible in the config
        // block, and nothing is outstanding at a menu, so a turn is always quiet for the read.
        if (now >= menuReadDueNs_) readMenuState(now);
        return atMenu_.load(std::memory_order_relaxed);  // nothing new goes out while it says menu
    }
    if (rel_ && rel_->progressOverdue(now) && now >= menuGameUntilNs_) {
        const bool forced = menuHoldSince_ != 0 && now - menuHoldSince_ >= kMenuHoldNs;
        if (quiet || forced) {
            const bool answered = readMenuState(now);
            menuHoldSince_ = 0;
            if (atMenu_.load(std::memory_order_relaxed)) return true;
            // The cart answered as a game: its acks are merely lost or damaged, and the stream goes on as it always
            // did, at one read a second rather than one a turn. A read that did NOT answer waits the same (its own
            // failure bound), so a window that keeps timing out costs the stream one read a second, not 100 ms a turn.
            menuGameUntilNs_ = now + (answered ? kMenuHoldNs : kMenuReadFailNs);
            return false;
        }
        if (menuHoldSince_ == 0) menuHoldSince_ = now;
        return true;  // hold the re-send until the guard has looked
    }
    menuHoldSince_ = 0;
    if (quiet && now >= menuReadDueNs_) readMenuState(now);
    return atMenu_.load(std::memory_order_relaxed);
}

// One read of where the cart runs. True when it answered; a read that did not leaves the state as it was and holds the
// next one off for a second (see kMenuReadFailNs).
bool N8Link::readMenuState(std::int64_t now) {
    menuReadDueNs_ = now + kMenuReadNs;
    // Whatever the cart has already said goes first: the reply to this read must not begin with bytes a chattering cart
    // left in the input, which would take the place of the reply's own (map_idx 255 behind one status byte reads as a
    // game, and a game re-enables the stream into the menu). Every path flushes, not only the forced read: a raw link
    // has nothing else draining its input between turns.
    try {
        flushPort();
    } catch (const std::exception&) {
        menuReadDueNs_ = now + kMenuReadFailNs;  // as a read that did not answer: the state stands, and the next waits
        return false;
    }
    int answer = -1;
    try {
        answer = menuCheck_(*edio_);
    } catch (const std::exception&) {
        return false;  // the port's failure is the stream's to report at its next write; the state stands
    }
    if (answer < 0) {
        menuReadDueNs_ = now + kMenuReadFailNs;
        return false;
    }
    reportMenuState(answer != 0);
    return true;
}

void N8Link::reportMenuState(bool menu) {
    // A link with no MenuCheck has no guard - a fake device's, or a case that turned it off on purpose - so there is
    // nothing for a probe's answer to feed.
    if (!menuCheck_) return;
    if (menu == atMenu_.load(std::memory_order_relaxed)) return;
    atMenu_.store(menu, std::memory_order_relaxed);
    if (menu)
        enterMenu();
    else
        leaveMenu();
}

void N8Link::enterMenu() {
    // Drop, not hold: a console reset left the game that held these notes, the menu holds none, and a game booted later
    // starts fresh. The ring, the reliable window, any pending release and the books on what the cart's parser was
    // inside go with it: a menu's FIFO is a command parser, not the game's stream.
    discardRing();
    std::fill(std::begin(lastOn_), std::end(lastOn_), kNoChunk);  // a menu holds no note of ours to skip a release for
    wireExpect_      = 0;
    wireSysex_       = false;
    wireOpenSinceNs_ = 0;
    wireStatus_      = 0;
    if (rel_) {
        rel_->reset();
        reliable_.store(false, std::memory_order_release);
        linkOutstanding_.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> l(jobMutex_);
        releasePending_.reset();
        releaseAfter_ = 0;
        releaseWanted_.store(false, std::memory_order_relaxed);
    }
    menuHoldSince_   = 0;
    menuGameUntilNs_ = 0;
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart is at its menu: the stream is dropped\n");
}

void N8Link::leaveMenu() {
    if (traceOn()) std::fprintf(stderr, "[n8link] the cart runs a game: streaming, and probing the link\n");
    menuHoldSince_   = 0;
    menuGameUntilNs_ = 0;
    // A game started from the cart's own browser may speak the reliable link, and the connect-time probe could not have
    // told: the frames it would carry start from a sync here.
    if (linkAllowed_) probeLink();
}

void N8Link::discardRing() {
    TimedChunk drop;
    while (ring_.tryPop(drop)) {
        // Taken off the ring is gone: a release's mark is a push count, so a dropped chunk counts as written, or every
        // later release would wait for one nobody will ever push.
        ++chunksWritten_;
        chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
        pushesDropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool N8Link::quietForRead(std::int64_t now) const {
    // Nothing outstanding, so no ack is owed (an ack the ROM repeats for a frame it already acknowledged is the one
    // exception, see OnControl). Or a cart that has said nothing for the pause's ack bound since the oldest outstanding
    // frame first went out: one at its menu (a console reset keeps USB and the link up, and the menu acknowledges
    // nothing), or a game that has stopped answering, the same risk the pause's drain takes when it gives up waiting.
    if (!rel_ || rel_->outstanding() == 0) return true;
    const std::int64_t spoke = std::max(rel_->lastHeardNs(), rel_->oldestFirstSentNs());
    return spoke != rp::N8ReliableSender::kNever && now - spoke >= kAckWaitNs;
}

void N8Link::serialLoop() {
    streamLoop();
    // The thread is going (a disconnect, a write that failed, a job that stopped streaming): no job is taken from here
    // on, and one taken that never ran is run now with no port, so its node hears it failed rather than waiting
    // forever.
    PausedJob orphan;
    {
        std::lock_guard<std::mutex> l(jobMutex_);
        acceptingJobs_ = false;
        if (jobPending_.load(std::memory_order_relaxed)) orphan = std::move(job_);
        // A release the stream could not write goes with it (see releaseNotes).
        releasePending_.reset();
        releaseWanted_.store(false, std::memory_order_relaxed);
    }
    if (orphan.run) {
        try {
            orphan.run(nullptr);
        } catch (const std::exception&) {
        }
        jobPending_.store(false, std::memory_order_release);
        try {
            orphan.done();
        } catch (const std::exception&) {
        }
    }
}

void N8Link::streamLoop() {
    try {
        // Not into a menu: the seed above knows where the cart runs, and the probe's sync is bytes its parser reads.
        if (linkAllowed_ && !atMenu_.load(std::memory_order_relaxed)) probeLink();
    } catch (const std::exception& e) {
        setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
        connected_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return;
    }
    TimedChunk chunk;
    bool have = false;
    while (running_.load(std::memory_order_acquire)) {
        try {
            // Acks in first, then the quiet test, then the menu guard, then the re-sends it may hold: the control turn
            // below can read a reply, so an ack that answered an outstanding frame must be behind us before it does,
            // and the guard's own read must not have one land inside it.
            if (rel_) serviceLinkAcks();
            const std::int64_t turnNs = nowNs();
            const bool         quiet  = quietForRead(turnNs);
            backChannelQuiet_.store(quiet, std::memory_order_relaxed);
            const bool menuHeld = menuHold(turnNs, quiet);
            if (rel_ && !menuHeld) serviceLinkResends(turnNs);
            // A setting that lives in an atomic (see OnControl) is applied between chunks, on the thread that owns the
            // port. Best-effort, like the connect-time write: a config register must not take the MIDI link down. A
            // wake is spent only on a quiet turn, the only kind its caller's read can use: a busy turn whose write
            // re-armed the retry wait would otherwise leave that read a second behind it.
            const bool woken = quiet && controlWanted_.exchange(false, std::memory_order_acquire);
            if (onControl_ && (woken || turnNs >= controlRetryAtNs_)) {
                try {
                    onControl_(*edio_, quiet);
                    controlRetryAtNs_ = 0;
                } catch (const std::exception&) {
                    // A write that keeps failing is retried once a second, not once a turn: a wedged-but-open port
                    // would otherwise spend the link's turns on a register write, and the volume's Auto answer is a
                    // config read on top of it, which the MIDI stream would wait behind.
                    controlRetryAtNs_ = turnNs + 1'000'000'000;
                }
            }
            // A leaving cart node's held notes (releaseNotes), once what was pushed before them has gone: on a
            // streaming turn, never inside a job's pause, which only the job's own traffic may use, and never at a
            // menu, which reads them as commands.
            if (!menuHeld) writeReleases(false);
            // Jobs still run at the menu: the battery files and an upload are exactly what its file API is for.
            if (jobPending_.load(std::memory_order_acquire)) {
                runJob(chunk, have);
                continue;
            }
            if (atMenu_.load(std::memory_order_relaxed)) {
                // The cart is at its menu, so nothing the ring holds may go - now or when a game boots later - and the
                // trip's own discard cannot see a push that raced it. Drop it here, and keep dropping until a game.
                if (have) {
                    have = false;
                    ++chunksWritten_;  // gone with the rest of the ring: a release's mark counts pushes, not writes
                    chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
                    pushesDropped_.fetch_add(1, std::memory_order_relaxed);
                }
                discardRing();
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            if (menuHeld) {
                // A re-send is due and the guard has not been able to look yet. The chunk in hand is NOT lost: the cart
                // may be a game that has been quiet for an ack bound, and its note-off must still go.
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            if (!have) have = ring_.tryPop(chunk);
            if (!have) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));  // ring empty
                continue;
            }
            // Wait until this chunk's release time, re-checking running_ so disconnect() stays responsive.
            const std::int64_t now = nowNs();
            if (now < chunk.targetNs) {
                const std::int64_t remNs = chunk.targetNs - now;
                std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<std::int64_t>(remNs, rel_ ? 200'000 : 2'000'000)));
                continue;
            }
            have = false;
            writeChunk(chunk);
            ++chunksWritten_;
        } catch (const std::exception& e) {
            setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
            connected_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);  // stop; the UI can reconnect
            return;
        }
    }
    // Asked to stop (a disconnect, the owner's destructor) with the port still open: a release waiting behind the ring
    // goes now, since the ring will never be written, so a plugin closed while the cart plays leaves no note sounding.
    // A stream a job stopped has closed the port already (an upload booted a fresh ROM, which holds no notes).
    if (!edio_) return;
    try {
        writeReleases(true);
    } catch (const std::exception&) {
        // A port that fails here (one whose failure inside a job is what stopped the stream) is closed by the
        // disconnect anyway.
    }
}

void N8Link::writeChunk(const TimedChunk& chunk, bool wholeMessage) {
    // The tracker's books, before the bytes go: this chunk's index is chunksWritten_, which the caller bumps after this
    // returns. A note-on is what a handover's release must not cut; a note-off leaves the key unheld. Only the first
    // message of a chunk is read (noteEvent), which is every message a node pushes: pushMidi sends one at a time. A
    // multi-message push is the BYTES port's, and its notes are not in this table, so a handover through that port
    // cannot tell the new node's key from the old one's.
    const rp::n8::NoteEvent note = rp::n8::noteEvent(chunk.data, chunk.len);
    if (note.on) lastOn_[note.index] = chunksWritten_;
    else if (note.off) lastOn_[note.index] = kNoChunk;
    const std::int64_t now = nowNs();
    if (rel_) {
        std::vector<std::uint8_t> out;
        std::vector<int> seqs;
        const auto emit = [&](const std::uint8_t* d, std::size_t k, int seq) {
            if (seq >= 0) seqs.push_back(seq);
            out.insert(out.end(), d, d + k);
        };
        // A release (`wholeMessage`) is framed as a message of its own, whatever the sender is still collecting: it
        // may be halfway through a SysEx the bytes port split, and a note-off read into it would cut it short and frame
        // its tail as note-offs of its own (N8ReliableSender::sendMessage). A streamed chunk is fed as it comes.
        if (wholeMessage) rel_->sendMessage(chunk.data, chunk.len, emit);
        else rel_->send(chunk.data, chunk.len, emit);
        // The cart's parser sees frames, which never leave a message open, so the byte-stream tracker is the raw path's
        // alone: what a frame's payload spells is no more a message to the cart than it is to the tracker.
        writeFramed(out, seqs, now, false);
    } else {
        noteWireBytes(chunk.data, chunk.len, now);
        if (writeLog_) writeLog_(chunk.data, chunk.len, now, false);
        check(edio_->fifoWR(chunk.data, chunk.len));
        bytesForwarded_.fetch_add(chunk.len, std::memory_order_relaxed);
    }
}

void N8Link::noteWireBytes(const std::uint8_t* data, std::size_t n, std::int64_t now) {
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b = data[i];
        // A real-time byte (0xF8 and up) may stand between any two bytes of a message and changes nothing: read as a
        // status it would look like the message ended, and a release written there would be its payload.
        if (b >= 0xF8) continue;
        if (wireSysex_) {
            if (b == 0xF7) {
                wireSysex_ = false;
                continue;
            }
            if (b < 0x80) continue;  // its payload, which is any byte below 0x80
            // A non-real-time status cuts the SysEx short, as it does on the wire and in the framed sender's own feed
            // (a tail that is never coming, a bytes port that handed over only a head): the tracker is at a boundary
            // again, and the message this status begins is read from here rather than swallowed for the rest of the
            // connection. The open's stamp goes with it, so the new message's bound measures from now.
            wireSysex_       = false;
            wireOpenSinceNs_ = 0;
        }
        if (wireExpect_ > 0 && b < 0x80) {
            --wireExpect_;
            continue;
        }
        if (b < 0x80) {
            // Running status: a data byte where a message's status would stand continues the last channel message (a
            // bytes port may hand over part of one, and need not repeat the status). Only a channel status runs on, so
            // a data byte behind a System Common or SysEx status opens nothing.
            if (wireStatus_ == 0) continue;
            wireExpect_ = wireDataBytes(wireStatus_) - 1;  // this byte is the first of its data
            continue;
        }
        if (b == 0xF0) {
            wireSysex_  = true;
            wireStatus_ = 0;  // a SysEx cancels running status, as it does on the wire
            continue;
        }
        wireStatus_ = (b & 0xF0) < 0xF0 ? b : 0;
        wireExpect_ = wireDataBytes(b);
    }
    const bool open = wireSysex_ || wireExpect_ > 0;
    if (!open) wireOpenSinceNs_ = 0;
    else if (wireOpenSinceNs_ == 0) wireOpenSinceNs_ = now;
}

// One complete MIDI message's data bytes, from its status: 2 for most channel messages, 1 for programme and channel
// pressure, and 0 for the System Common ones that carry none (a real-time byte is one on its own, and F0 is the SysEx
// open this tracker follows instead; noteWireBytes steps over real-time bytes, so none of them arrives here).
std::size_t N8Link::wireDataBytes(std::uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
        case 0xC0: case 0xD0: return 1;
        default: break;
    }
    switch (status) {
        case 0xF1: case 0xF3: return 1;
        case 0xF2: return 2;
        default: return 0;
    }
}

bool N8Link::atMessageBoundary(std::int64_t now) const {
    if (!wireSysex_ && wireExpect_ == 0) return true;
    // A stream left open this long is one nothing is going to finish (a node that died mid-SysEx): its notes are
    // released anyway, since a tone held on the console is the worse of the two.
    return wireOpenSinceNs_ != 0 && now - wireOpenSinceNs_ >= kMidMessageNs;
}

void N8Link::drainForJob(TimedChunk& chunk, bool& have) {
    // At the menu nothing the ring still holds may go: the cart reads it as commands. The job's own transfers use the
    // port directly, which is what a menu is for.
    if (atMenu_.load(std::memory_order_relaxed)) {
        if (have) ++chunksWritten_;  // a chunk taken off the ring and dropped has still gone (see discardRing)
        have = false;
        discardRing();
        return;
    }
    // What was pushed before the pause goes out first, each chunk at its own release time: at most the lookahead and a
    // block away, and a note-off among it must reach the cart.
    while (running_.load(std::memory_order_acquire)) {
        if (rel_) serviceLink(nowNs());
        if (!have) have = ring_.tryPop(chunk);
        if (!have) break;
        const std::int64_t now = nowNs();
        if (now < chunk.targetNs) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<std::int64_t>(chunk.targetNs - now, 200'000)));
            continue;
        }
        have = false;
        writeChunk(chunk);
        ++chunksWritten_;
    }
    // And with the reliable link, until the ROM has acknowledged every frame, so the job does not sit on a frame a
    // DMC-DMA halt ate and only a re-send would deliver. Bounded: a cart that has stopped answering gets its re-sends
    // after the job, from the sequence state the pause kept.
    if (!rel_) return;
    const std::int64_t until = nowNs() + kAckWaitNs;
    while (running_.load(std::memory_order_acquire) && rel_->outstanding() > 0 && nowNs() < until) {
        serviceLink(nowNs());
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

void N8Link::runJob(TimedChunk& chunk, bool& have) {
    PausedJob job;
    {
        std::lock_guard<std::mutex> l(jobMutex_);
        job = std::move(job_);
    }
    // From here the job owns the port: a producer drops (and counts) what it pushes until streaming resumes. A push
    // that read the flag just before this store can still land in the ring after the drain below; it goes out late,
    // after the job, which for a note-off is the better of the two.
    connected_.store(false, std::memory_order_release);
    bool portOk = true;
    try {
        drainForJob(chunk, have);
    } catch (const std::exception& e) {
        setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
        portOk = false;
    }
    AfterJob  after   = AfterJob::Resume;  // a throw out of `run` decides nothing: only `decided` says one did
    bool      started = false, decided = false;
    if (portOk && running_.load(std::memory_order_acquire)) {
        try {
            // Whatever the back-channel still holds (a late ack) would read as the first bytes of the job's reply.
            flushPort();
            started = true;
            after   = job.run(edio_.get());
            decided = true;
            flushPort();
        } catch (const std::exception& e) {
            // A throw this far into a job is the port's: the jobs report their own failures and return (a flush that
            // fails, or an edio call outside a job's own catch - only a bad_alloc gets out of a `run`). The port cannot
            // be trusted with another chunk, so the link stops exactly as a failed write stops it, with the reason kept
            // for the UI (lastError), and a job that never started hears that the link went down rather than failing
            // with no reason at all. A job whose own `run` threw did run: re-running it would report a boot that
            // happened as the link going down, so it is left with what it recorded, which for one that threw before
            // recording anything is a failure with an empty reason (only a bad_alloc gets there).
            setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
            if (!started) {
                try {
                    job.run(nullptr);
                } catch (const std::exception&) {
                }
            }
            portOk = false;
        }
    } else {
        try {
            job.run(nullptr);
        } catch (const std::exception&) {
        }
    }
    if (after == AfterJob::Resume && portOk && running_.load(std::memory_order_acquire)) {
        connected_.store(true, std::memory_order_release);
    } else if (portOk || (decided && after == AfterJob::Stop)) {
        // The job asked for streaming to stop (a ROM boot, whose result says so). It closes the port even when the
        // job's own trailing flush threw: an upload frees the port for the CLI's tools, and a dead port cannot deliver
        // another chunk either way.
        stopStreaming();
    } else {
        running_.store(false, std::memory_order_release);  // a failed write, as streamLoop's: the UI can reconnect
    }
    jobPending_.store(false, std::memory_order_release);
    try {
        job.done();
    } catch (const std::exception&) {
        // A throw here would end the serial thread through its entry point, taking the process with it.
    }
}

void N8Link::stopStreaming() {
    // The job asked for streaming to stop (a ROM boot), or a disconnect came during it: the thread ends here, and
    // closes the port itself, so another process (the CLI's tools) can open it while nothing streams. disconnect()
    // joins and finds it closed.
    running_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    // A push that read `connected` just before the pause began reached the ring after its drain; the stream it was for
    // is gone, so it is thrown away here, and counted (the next connect would clear it uncounted).
    TimedChunk left;
    while (ring_.tryPop(left)) {
        chunksDiscarded_.fetch_add(1, std::memory_order_relaxed);
        pushesDropped_.fetch_add(1, std::memory_order_relaxed);
    }
    rel_.reset();
    reliable_.store(false, std::memory_order_release);
    edio_.reset();
    serialPort_.reset();
}

}  // namespace retroplug

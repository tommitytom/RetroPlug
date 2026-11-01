#include "host/n8/N8Link.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace retroplug {

namespace {
using steady = std::chrono::steady_clock;
// RP_N8LINK_TRACE=1 logs every byte the link writes and every back-channel byte it reads (stderr) - read once.
bool traceOn() {
    static const bool on = std::getenv("RP_N8LINK_TRACE") != nullptr;
    return on;
}
std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(steady::now().time_since_epoch()).count();
}
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

bool N8Link::connect(const std::string& port) {
    disconnect();  // tear down any prior connection first
    try {
        auto sp   = factory_(port);
        auto edio = std::make_unique<Edio>(*sp);
        edio->connect();  // handshake; throws on a bad / absent reply
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
    // one-shot device write belongs - see OnConnected. Best-effort by design: the only plausible throw is a
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
    bytesForwarded_.store(0, std::memory_order_relaxed);
    controlRetryAtNs_ = 0;  // a prior connection's failed control write does not hold this one's first turn back
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
    rel_.reset();
    reliable_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    edio_.reset();
    serialPort_.reset();
}

bool N8Link::push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate) {
    if (n == 0) return true;
    if (!connected_.load(std::memory_order_acquire)) {
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
        i += take;
    }
    return true;
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
    edio_->fifoWR(sync, 2);
    const std::int64_t until = nowNs() + 100'000'000;
    bool sawFrame = false;
    while (running_.load(std::memory_order_acquire) && nowNs() < until) {
        std::uint8_t buf[32];
        const std::size_t n = serialPort_->read(buf, sizeof buf, 2);
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
    edio_->fifoWR(out.data(), out.size());
    bytesForwarded_.fetch_add(out.size(), std::memory_order_relaxed);
}

void N8Link::serviceLink() {
    const std::int64_t now = nowNs();
    std::uint8_t buf[64];
    const std::size_t n = serialPort_->read(buf, sizeof buf, 0);   // whatever the cart has said: never waits
    if (n && traceOn()) {
        std::fprintf(stderr, "[n8link] %.3f rx", static_cast<double>(now) / 1e6);
        for (std::size_t i = 0; i < n; ++i) std::fprintf(stderr, " %02X", buf[i]);
        std::fprintf(stderr, "\n");
    }
    if (n) rel_->onBackChannel(buf, n, now);
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
        if (linkAllowed_) probeLink();
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
            // Acks in, re-sends out, before anything else this turn: the control turn below can read a reply, and an
            // ack that answered an outstanding frame must be behind us before it does.
            if (rel_) serviceLink();
            // A setting that lives in an atomic (see OnControl) is applied between chunks, on the thread that owns the
            // port. Best-effort, like the connect-time write: a config register must not take the MIDI link down.
            const bool woken = controlWanted_.exchange(false, std::memory_order_acquire);
            if (onControl_ && (woken || nowNs() >= controlRetryAtNs_)) {
                const std::int64_t now = nowNs();
                try {
                    onControl_(*edio_, !rel_ || rel_->outstanding() == 0);
                    controlRetryAtNs_ = 0;
                } catch (const std::exception&) {
                    // A write that keeps failing is retried once a second, not once a turn: a wedged-but-open port
                    // would otherwise spend the link's turns on a register write, and the volume's Auto answer is a
                    // config read on top of it, which the MIDI stream would wait behind.
                    controlRetryAtNs_ = now + 1'000'000'000;
                }
            }
            if (jobPending_.load(std::memory_order_acquire)) {
                runJob(chunk, have);
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
        } catch (const std::exception& e) {
            setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
            connected_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);  // stop; the UI can reconnect
            break;
        }
    }
}

void N8Link::writeChunk(const TimedChunk& chunk) {
    if (rel_) {
        std::vector<std::uint8_t> out;
        std::vector<int> seqs;
        rel_->send(chunk.data, chunk.len, [&](const std::uint8_t* d, std::size_t k, int seq) {
            if (seq >= 0) seqs.push_back(seq);
            out.insert(out.end(), d, d + k);
        });
        writeFramed(out, seqs, nowNs(), false);
    } else {
        if (writeLog_) writeLog_(chunk.data, chunk.len, nowNs(), false);
        edio_->fifoWR(chunk.data, chunk.len);
        bytesForwarded_.fetch_add(chunk.len, std::memory_order_relaxed);
    }
}

void N8Link::drainForJob(TimedChunk& chunk, bool& have) {
    // What was pushed before the pause goes out first, each chunk at its own release time: at most the lookahead and a
    // block away, and a note-off among it must reach the cart.
    while (running_.load(std::memory_order_acquire)) {
        if (rel_) serviceLink();
        if (!have) have = ring_.tryPop(chunk);
        if (!have) break;
        const std::int64_t now = nowNs();
        if (now < chunk.targetNs) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<std::int64_t>(chunk.targetNs - now, 200'000)));
            continue;
        }
        have = false;
        writeChunk(chunk);
    }
    // And with the reliable link, until the ROM has acknowledged every frame, so the job does not sit on a frame a
    // DMC-DMA halt ate and only a re-send would deliver. Bounded: a cart that has stopped answering gets its re-sends
    // after the job, from the sequence state the pause kept.
    if (!rel_) return;
    const std::int64_t until = nowNs() + 250'000'000;
    while (running_.load(std::memory_order_acquire) && rel_->outstanding() > 0 && nowNs() < until) {
        serviceLink();
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
            serialPort_->flushInput();
            started = true;
            after   = job.run(edio_.get());
            decided = true;
            serialPort_->flushInput();
            edio_->setReadTimeout(2000);  // a menu command raises it; streaming's own reads never wait on it
        } catch (const std::exception& e) {
            // A throw this far into a job is the port's: the jobs report their own failures and return (a flush that
            // fails, or an edio call outside a job's own catch - only a bad_alloc gets out of a `run`). The port cannot
            // be trusted with another chunk, so the link stops exactly as a failed write stops it, with the reason kept
            // for the UI (lastError), and a job that never started hears that the link went down rather than failing
            // with no reason at all. A job whose own `run` threw did run: re-running it would report a boot that
            // happened as the link going down, so it is left with what it recorded.
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

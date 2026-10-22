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
    running_.store(true, std::memory_order_release);
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
    // Drop anything posted but never run: it described the device we just let go of, and the next connect
    // applies the current config from scratch.
    std::lock_guard<std::mutex> l(control_);
    pendingControl_ = nullptr;
}

void N8Link::postControl(std::function<void(Edio&)> fn) {
    if (!connected_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> l(control_);
    pendingControl_ = std::move(fn);
}

void N8Link::push(std::uint32_t sampleOffset, const std::uint8_t* data, std::size_t n, double sampleRate) {
    if (!connected_.load(std::memory_order_acquire) || n == 0) return;
    const std::int64_t offNs  = sampleRate > 0.0 ? static_cast<std::int64_t>(sampleOffset * 1e9 / sampleRate) : 0;
    const std::int64_t target = nowNs() + offNs + lookaheadNs_.load(std::memory_order_relaxed);
    std::size_t i = 0;
    while (i < n) {
        const std::size_t take = std::min<std::size_t>(n - i, sizeof(TimedChunk::data));  // <= 8
        TimedChunk c;
        c.targetNs = target;
        c.len      = static_cast<std::uint8_t>(take);
        std::memcpy(c.data, data + i, take);
        if (!ring_.tryPush(c)) return;  // ring full: drop (never block the audio thread)
        i += take;
    }
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
                t.adaptiveCondemn = true;        // an ack can be written while a younger frame is still on the wire
                t.condemnAfterNs  = 500'000;     // slack on top of the fastest round trip seen
                t.resendDelayNs   = 0;           // the real round trip already applies
                rel_ = std::make_unique<rp::N8ReliableSender>(t);
                reliable_.store(true, std::memory_order_release);
                return;
            }
            sawFrame = (buf[i] == rp::N8ReliableSender::FRAME);
        }
    }
}

void N8Link::writeFramed(std::vector<std::uint8_t>& out, const std::vector<int>& seqs, std::int64_t now) {
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
    if (!out.empty()) writeFramed(out, seqs, now);
    linkResends_.store(rel_->resends(), std::memory_order_relaxed);
    linkOutstanding_.store(rel_->outstanding(), std::memory_order_relaxed);
    linkRttMinNs_.store(rel_->rttMinNs(), std::memory_order_relaxed);
}

void N8Link::serialLoop() {
    try {
        probeLink();
    } catch (const std::exception& e) {
        setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
        connected_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return;
    }
    TimedChunk chunk;
    bool have = false;
    while (running_.load(std::memory_order_acquire)) {
        // A posted control write (see postControl) runs between chunks, on the thread that owns the port.
        // Best-effort, like the connect-time one: a config register must not take the MIDI link down.
        {
            std::function<void(Edio&)> control;
            {
                std::lock_guard<std::mutex> l(control_);
                control = std::move(pendingControl_);
                pendingControl_ = nullptr;
            }
            if (control) {
                try {
                    control(*edio_);
                } catch (const std::exception&) {
                }
            }
        }
        try {
            if (rel_) serviceLink();   // acks in, re-sends out - between every chunk and while waiting for one
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
            if (rel_) {
                std::vector<std::uint8_t> out;
                std::vector<int> seqs;
                rel_->send(chunk.data, chunk.len, [&](const std::uint8_t* d, std::size_t k, int seq) {
                    if (seq >= 0) seqs.push_back(seq);
                    out.insert(out.end(), d, d + k);
                });
                writeFramed(out, seqs, nowNs());
            } else {
                edio_->fifoWR(chunk.data, chunk.len);
                bytesForwarded_.fetch_add(chunk.len, std::memory_order_relaxed);
            }
        } catch (const std::exception& e) {
            setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
            connected_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);  // stop; the UI can reconnect
            break;
        }
    }
}

}  // namespace retroplug

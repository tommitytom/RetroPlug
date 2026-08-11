#include "host/n8/N8Link.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace retroplug {

namespace {
using steady = std::chrono::steady_clock;
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
    connected_.store(false, std::memory_order_release);
    edio_.reset();
    serialPort_.reset();
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

void N8Link::serialLoop() {
    TimedChunk chunk;
    while (running_.load(std::memory_order_acquire)) {
        if (!ring_.tryPop(chunk)) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));  // ring empty
            continue;
        }
        // Wait until this chunk's release time, re-checking running_ so disconnect() stays responsive.
        while (running_.load(std::memory_order_acquire)) {
            const std::int64_t now = nowNs();
            if (now >= chunk.targetNs) break;
            const std::int64_t remNs = chunk.targetNs - now;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<std::int64_t>(remNs, 2'000'000)));
        }
        if (!running_.load(std::memory_order_acquire)) break;
        try {
            edio_->fifoWR(chunk.data, chunk.len);
            bytesForwarded_.fetch_add(chunk.len, std::memory_order_relaxed);
        } catch (const std::exception& e) {
            setError(std::string("serial write failed (N8 unplugged?): ") + e.what());
            connected_.store(false, std::memory_order_release);
            running_.store(false, std::memory_order_release);  // stop; the UI can reconnect
            break;
        }
    }
}

}  // namespace retroplug

#include "host/launchpad/LaunchpadScanner.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace retroplug {

LaunchpadScanner::~LaunchpadScanner() {
    if (thread_.joinable()) thread_.join();
}

bool LaunchpadScanner::start(std::vector<std::uint8_t> probe, std::vector<std::string> inputs,
                             std::vector<std::string> outputs, unsigned windowMs) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;  // a scan is already in flight
    if (thread_.joinable()) thread_.join();  // reap the previous (finished) scan's thread

    {
        std::lock_guard<std::mutex> lk(meta_);
        probing_.clear();
        phase_.clear();
        error_.clear();
        replies_.clear();
    }
    skipped_.store(0, std::memory_order_relaxed);
    done_.store(false, std::memory_order_relaxed);
    bump();

    thread_ = std::thread([this, probe = std::move(probe), inputs = std::move(inputs),
                           outputs = std::move(outputs), windowMs]() {
        try {
            run(probe, inputs, outputs, windowMs);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(meta_);
            error_ = e.what();
        } catch (...) {
            std::lock_guard<std::mutex> lk(meta_);
            error_ = "unknown error";
        }
        {   // no window is open once the scan is over: a straggler must not land in the next scan's results
            std::lock_guard<std::mutex> lk(meta_);
            probing_.clear();
        }
        done_.store(true, std::memory_order_release);
        busy_.store(false, std::memory_order_release);
        bump();
    });
    return true;
}

void LaunchpadScanner::run(const std::vector<std::uint8_t>& probe, const std::vector<std::string>& inputs,
                           const std::vector<std::string>& outputs, unsigned windowMs) {
    if (!opener_) throw std::runtime_error("no MIDI backend");

    const auto setPhase = [this](std::string p) {
        { std::lock_guard<std::mutex> lk(meta_); phase_ = std::move(p); }
        bump();
    };

    // Every input is opened ONCE and kept open for the whole scan, so a device is heard whichever output it
    // turns out to be paired with. A port that will not open is counted and skipped: on Windows a MIDI input
    // is exclusive, so anything else already holding it (including our own musical stream, which the host
    // suspends around a scan) shows up here rather than failing the scan.
    setPhase("Listening");
    std::vector<std::unique_ptr<IMidiPort>> listeners;
    listeners.reserve(inputs.size());
    for (const std::string& name : inputs) {
        try {
            auto port = opener_(true, name, [this, name](const std::uint8_t* data, std::size_t n) {
                onReply(name, data, n);
            });
            if (port) listeners.push_back(std::move(port));
            else skipped_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception&) {
            skipped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    for (const std::string& name : outputs) {
        std::unique_ptr<IMidiPort> out;
        try {
            out = opener_(false, name, nullptr);
        } catch (const std::exception&) {
            out.reset();
        }
        if (!out) {
            skipped_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        setPhase("Probing " + name);
        { std::lock_guard<std::mutex> lk(meta_); probing_ = name; }  // opens this output's answer window
        if (!probe.empty()) out->send(probe.data(), probe.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(windowMs));
        { std::lock_guard<std::mutex> lk(meta_); probing_.clear(); }
        bump();  // a reply may have landed; let the UI see it as each port finishes rather than at the end
    }
    setPhase("");
}

void LaunchpadScanner::onReply(const std::string& inName, const std::uint8_t* data, std::size_t n) {
    if (!data || n == 0) return;
    // System Real-Time (0xF8..0xFF) is a stream within the stream - clock, sense, start/stop. It can never be
    // an answer to a probe, and a device with a running sequencer on it produces a steady drizzle of it, so
    // dropping it here is what keeps a busy rig from filling the reply list with noise.
    if (data[0] >= 0xF8) return;
    std::lock_guard<std::mutex> lk(meta_);
    if (probing_.empty()) return;  // arrived outside any answer window - see the member comment
    if (replies_.size() >= kMaxReplies) return;
    replies_.push_back({ probing_, inName, std::vector<std::uint8_t>(data, data + n) });
}

LaunchpadScanStatusDto LaunchpadScanner::status() const {
    LaunchpadScanStatusDto s;
    s.busy    = busy_.load(std::memory_order_acquire);
    s.done    = done_.load(std::memory_order_acquire);
    s.skipped = skipped_.load(std::memory_order_relaxed);
    s.version = version_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lk(meta_);
    s.phase   = phase_;
    s.error   = error_;
    s.replies = replies_;
    return s;
}

}  // namespace retroplug
